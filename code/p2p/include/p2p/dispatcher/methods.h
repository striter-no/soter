#include "structs.h"
#include <natpunch/sserver.h>

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
        SLOG_ERROR("[disp][error] get PUNCH from unknown peer (%u UID)", pkt->h_from);
        return false;
    }
    
    if (!p2p_psystem_Ppeer_check(disp->psys, pkt->h_from, P2P_STAT_PUNCHING)){
        SLOG_WARNING("[disp][warn] peer wanted to PUNCH, but he is in different mode (%u): %s:%u", peer->status, other_ip.ip.v4.ip, other_ip.ip.v4.port);
        
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
    SLOG_DEBUG("[disp] changing mode for %u to ACK", peer->peer_id);
    SLOG_DEBUG("[disp] punch from: %s:%u, sended ack", other_ip.ip.v4.ip, other_ip.ip.v4.port);
    return true;
}

bool disp_method_ack(udp_packet *pkt, p2p_dispatcher *disp, p2p_udp *cli){
    naddr_t other_ip = {0};
    memcpy(&other_ip, pkt->data, pkt->d_size);
    
    p2p_peer *peer = p2p_psystem_Ppeer(disp->psys, pkt->h_from);
    if (!peer) {
        SLOG_WARNING("[disp] got unexcpected ACK from unknown %u", pkt->h_from);
        return false;
    }

    if (peer->peer_id != pkt->h_from){
        SLOG_WARNING("[disp] got corrupted ACK from %u (!= %u)", peer->peer_id, pkt->h_from);
        return false;
    }
    
    if (peer->status != P2P_STAT_PUNCHING && peer->status != P2P_STAT_ACK){
        SLOG_WARNING("[disp] got unexcpected ACK from %u with %d status", peer->peer_id, peer->status);
        return false;
    }

    if (peer->status == P2P_STAT_PUNCHING){
        SLOG_DEBUG("[disp] sending ACK");
        nnet_fd remote_fd = netfdq(other_ip);
        p2pnp_udp_ack(
            cli, other_ip, pkt->h_from, 
            &remote_fd
        );

        peer->status = P2P_STAT_ACK;
    }
    
    SLOG_DEBUG("[disp] got ACK, alive peer: %s:%u", other_ip.ip.v4.ip, other_ip.ip.v4.port); 
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
        SLOG_ERROR("[disp][ping] sanity check failed for %u: %u", pkt->h_from, sanity);
        return false;
    }

    naddr_t other_ip = {0};
    memcpy(&other_ip, pkt->data, pkt->d_size);

    nnet_fd remote_fd = netfdq(other_ip);
    p2pnp_udp_pong(cli, other_ip, pkt->h_from, &remote_fd);
    peer->last_seen = get_timestump();

    return true;
}

bool disp_method_pong(udp_packet *pkt, p2p_dispatcher *disp, p2p_udp *cli){
    p2p_disp_sanity sanity;
    p2p_peer *peer = p2p_sanity_check(pkt, disp, &sanity);

    if (!peer){
        SLOG_ERROR("[disp][pong] sanity check failed for %u: %u", pkt->h_from, sanity);
        return false;
    }

    peer->last_seen = get_timestump();
    return true;
}

bool disp_method_gossip(udp_packet *pkt, p2p_dispatcher *disp, p2p_udp *cli){
    p2p_disp_sanity sanity;
    p2p_peer *peer = p2p_sanity_check(pkt, disp, &sanity);

    if (!peer){
        SLOG_ERROR("[disp][gossip] sanity check failed for %u: %u", pkt->h_from, sanity);
        return false;
    }

    if (0 > gossip_system_update(disp->gossip, pkt->data, pkt->d_size)){
        SLOG_ERROR("[disp][gossip] failed to update system");
        return false;
    }
    return true;
}

bool disp_method_state(udp_packet *pkt, p2p_dispatcher *disp, p2p_udp *cli){
    // no sanity checks
    if (pkt->packtype != P2P_PACK_STATE) {
        SLOG_WARNING("[p2pnp][sserv] ignoring packtype %u", pkt->packtype);
        return false;
    }

    p2p_state_peer state;

    if (pkt->d_size != sizeof(state)){
        if (pkt->d_size != 1) SLOG_WARNING("[p2pnp][sserv] ignoring corrupted packet");
        // otherwise it is "0", answer when no peers available
        return false;
    } else {
        memcpy(&state, pkt->data, sizeof(state));
        naddr_t other_addr;
        uint32_t other_uid;

        p2p_state_peer2info(state, &other_addr, &other_uid);
        gossip_entry entry = (gossip_entry){
            .ip = naddr_to_uint32(other_addr),
            .uid = other_uid,
            .port = other_addr.ip.v4.port
        };

        if (gossip_entry_check(disp->gossip, entry)){
            return false;
        }

        int stfd = p2p_peer_register(disp->psys, other_addr, other_uid, P2P_STAT_PUNCHING, -1);
        SLOG_DEBUG("[p2pnp][sserv] stfd: %i (%s:%u %u)", stfd, other_addr.ip.v4.ip, other_addr.ip.v4.port, other_uid);
        
        gossip_new_entry(disp->gossip, entry);
        SLOG_DEBUG("[p2pnp][gossip] new entry (%zu in total): %u %u %u", disp->gossip->gossips.array.len, entry.ip, entry.port, entry.uid);
        
        p2p_state gstate = {
            .stfd = stfd,
            .ip = other_addr,
            .UID = other_uid
        };
        
        prot_array_push(&disp->state_evfds, &gstate);
        write(disp->sstate_evfd, &(uint64_t){1}, 8);
    }
    return true;
}

#endif
#define P2P_DISPATCHER_METHODS