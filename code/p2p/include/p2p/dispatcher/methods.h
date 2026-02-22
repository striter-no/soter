#include "structs.h"

#ifndef P2P_DISPATCHER_METHODS

p2p_peer* p2p_sanity_check(
    udp_packet *pkt, 
    p2p_dispatcher *disp, 
    p2p_disp_sanity *stat
){
    p2p_peer *peer = p2p_psystem_peer(disp->psys, pkt->h_from);
    if (!peer){
        *stat = SANITY_UNCONNECTED;
        return NULL;
    }

    if (peer->peer_id != pkt->h_from){
        *stat = SANITY_CORRUPTED;
        return NULL;
    }

    if (peer->status != P2P_STAT_ACTIVE){
        *stat = SANITY_DEAD;
        return NULL;
    }

    *stat = SANITY_OK;
    return peer;
}

bool disp_method_punch(udp_packet *pkt, p2p_dispatcher *disp, p2p_udp *cli){
    naddr_t other_ip = {0};
    memcpy(&other_ip, pkt->data, pkt->d_size);
    
    p2p_peer *peer = p2p_psystem_Ppeer(disp->psys, pkt->h_from);
    if (!peer){
        printf("[disp][error] get PUNCH from unknown peer (%u UID)\n", pkt->h_from);
        return false;
    }
    
    if (!p2p_psystem_Ppeer_check(disp->psys, pkt->h_from, P2P_STAT_PUNCHING)){
        printf("[disp][warn] peer wanted to PUNCH, but he is in different mode (%u): %s:%u\n", peer->status, other_ip.ip.v4.ip, other_ip.ip.v4.port);
        
        if (peer->status == P2P_STAT_ACK){
            nnet_fd remote_fd = netfdq(other_ip);
            p2pnp_udp_ack(
                cli, other_ip, pkt->h_from, 
                &remote_fd
            );
        }
        
        return false;
    }
    
    nnet_fd remote_fd = netfdq(other_ip);
    p2pnp_udp_ack(
        cli, other_ip, pkt->h_from, 
        &remote_fd
    );

    
    peer->status = P2P_STAT_ACK;
    printf("[disp] changing mode for %u to ACK\n", peer->peer_id);
    printf("[disp] punch from: %s:%u, sended ack\n", other_ip.ip.v4.ip, other_ip.ip.v4.port);
    return true;
}

bool disp_method_ack(udp_packet *pkt, p2p_dispatcher *disp, p2p_udp *cli){
    naddr_t other_ip = {0};
    memcpy(&other_ip, pkt->data, pkt->d_size);
    
    p2p_peer *peer = p2p_psystem_Ppeer(disp->psys, pkt->h_from);
    if (!peer) {
        printf("[disp] got unexcpected ACK from unknown %u\n", pkt->h_from);
        return false;
    }

    if (peer->peer_id != pkt->h_from){
        printf("[disp] got corrupted ACK from %u (!= %u)\n", peer->peer_id, pkt->h_from);
        return false;
    }
    
    if (peer->status != P2P_STAT_PUNCHING && peer->status != P2P_STAT_ACK){
        printf("[disp] got unexcpected ACK from %u with %d status\n", peer->peer_id, peer->status);
        return false;
    }

    if (peer->status == P2P_STAT_PUNCHING){
        printf("[disp] sending ACK\n");
        nnet_fd remote_fd = netfdq(other_ip);
        p2pnp_udp_ack(
            cli, other_ip, pkt->h_from, 
            &remote_fd
        );

        peer->status = P2P_STAT_ACK;
    }
    
    printf("[disp] got ACK, alive peer: %s:%u\n", other_ip.ip.v4.ip, other_ip.ip.v4.port); 
    p2p_psystem_makealive(disp->psys, peer->peer_id);
    
    gossip_entry entry = {0};
    entry.ip   = naddr_to_uint32(other_ip);
    entry.port = other_ip.ip.v4.port;
    entry.uid  = peer->peer_id;
    gossip_new_entry(disp->gossip, entry);

    return true;
}

bool disp_method_ping(udp_packet *pkt, p2p_dispatcher *disp, p2p_udp *cli){
    p2p_disp_sanity sanity;
    p2p_peer *peer = p2p_sanity_check(pkt, disp, &sanity);

    if (!peer){
        printf("[disp][ping] sanity check failed for %u: %u\n", pkt->h_from, sanity);
        return false;
    }

    naddr_t other_ip = {0};
    memcpy(&other_ip, pkt->data, pkt->d_size);

    nnet_fd remote_fd = netfdq(other_ip);
    p2pnp_udp_pong(cli, other_ip, pkt->h_from, &remote_fd);
    peer->last_seen = get_timestump();

    printf("[disp] got ping, ponging...\n");
    return true;
}

bool disp_method_pong(udp_packet *pkt, p2p_dispatcher *disp, p2p_udp *cli){
    p2p_disp_sanity sanity;
    p2p_peer *peer = p2p_sanity_check(pkt, disp, &sanity);

    if (!peer){
        printf("[disp][pong] sanity check failed for %u: %u\n", pkt->h_from, sanity);
        return false;
    }

    printf("[disp] got pong\n");
    peer->last_seen = get_timestump();
    return true;
}

bool disp_method_gossip(udp_packet *pkt, p2p_dispatcher *disp, p2p_udp *cli){
    p2p_disp_sanity sanity;
    p2p_peer *peer = p2p_sanity_check(pkt, disp, &sanity);

    if (!peer){
        printf("[disp][gossip] sanity check failed for %u: %u\n", pkt->h_from, sanity);
        return false;
    }

    if (0 > gossip_system_update(disp->gossip, pkt->data, pkt->d_size)){
        printf("[disp][gossip] failed to update system");
        return false;
    }
    return true;
}

#endif
#define P2P_DISPATCHER_METHODS