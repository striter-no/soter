#include "listener.h"
#include "peers.h"
#include <natpunch/punch.h>
#include <stdatomic.h>

#ifndef P2P_DISPATCHER

typedef struct {
    atomic_bool  is_active;
    p2p_udp     *p_client;

    pthread_t     main_thread;
    p2p_listener *listener;
    p2p_peers_system *psyst;
} p2p_dispatcher;

int p2p_dispatcher_init(
    p2p_dispatcher *dispatcher,
    p2p_listener   *listener,
    p2p_peers_system *psyst
){
    if (!dispatcher || !listener || !psyst) return -1;

    dispatcher->listener = listener;
    dispatcher->p_client = listener->p_client;
    dispatcher->psyst    = psyst;

    return 0;
}

static void p2p_dispatcher_liveness_check(p2p_dispatcher *disp){
    p2p_peers_system *psys = disp->psyst;
    p2p_udp          *cli  = disp->p_client;

    prot_table_lock(&psys->peers);
    for (size_t i = 0; i < psys->peers.table.array.len;){
        dyn_pair *p = dyn_array_at(&psys->peers.table.array, i);
        p2p_peer *peer = p->second;

        // p2p_psystem_activity(psys, peer->peer_id, NULL);
        // printf("[disp][liveness] peer %u stat %u: %u dt\n", peer->status, peer->peer_id, get_timestump() - peer->last_seen);
        if (get_timestump() - peer->last_seen >= P2P_PEER_DEAD_DT){
            peer->status = P2P_STAT_TIMEOUT;
        }
        
        if (get_timestump() - peer->last_seen == P2P_PEER_PING_DT){
            p2pnp_udp_ping(cli, naddr_nfd2str(peer->fd), peer->peer_id, &peer->fd);
            peer->last_seen = get_timestump();
        }

        if (peer->status == P2P_STAT_TIMEOUT){
            printf("[disp][liveness] removing timeouted peer %u\n", peer->peer_id);
            prot_table_remove(&psys->peers, &peer->peer_id);
        } else i++;
    }
    prot_table_unlock(&psys->peers);
}

void *p2p_dispatcher_worker(void *_args){
    p2p_dispatcher *disp = _args;
    p2p_listener   *list = disp->listener;
    p2p_udp        *cli  = list->p_client;
    p2p_peers_system *psys = disp->psyst;

    struct pollfd newpack_fd[1] = {{.fd = list->pack_eventfd, .events = POLLIN}};
    while (atomic_load(&disp->is_active)){
        int r = poll(newpack_fd, 1, 100);
        p2p_dispatcher_liveness_check(disp);
        
        if (r == 0) { continue; }
        else if (r < 0) {
            perror("[disp] dispatcher:poll()");
            continue;
        }

        uint64_t pack;
        read(list->pack_eventfd, &pack, sizeof(uint64_t));

        udp_packet **_pkt = NULL;
        if (prot_queue_pop(&list->packets, (void**)&_pkt) != 0){
            perror("[disp] failed to get packet, something went wrong");
            continue;
        }

        udp_packet *pkt = *_pkt;
        if (!pkt) continue;

        switch (pkt->packtype){
            case P2P_PACK_PUNCH:{ 
                naddr_t other_ip = {0};
                memcpy(&other_ip, pkt->data, pkt->d_size);
                
                p2p_peer *peer = p2p_psystem_Ppeer(psys, pkt->h_from);
                if (!peer){
                    printf("[disp][error] get PUNCH from unknown peer (%u UID)\n", pkt->h_from);
                    goto disp_end;
                }
                
                if (!p2p_psystem_Ppeer_check(psys, pkt->h_from, P2P_STAT_PUNCHING)){
                    printf("[disp][warn] peer wanted to PUNCH, but he is in different mode (%u): %s:%u\n", peer->status, other_ip.ip.v4.ip, other_ip.ip.v4.port);
                    goto disp_end;
                }
                
                nnet_fd remote_fd = netfdq(other_ip);
                p2pnp_udp_ack(
                    cli, other_ip, pkt->h_from, 
                    &remote_fd
                );

                
                peer->status = P2P_STAT_ACK;
                printf("[disp] changing mode for %u to ACK\n", peer->peer_id);
                printf("[disp] punch from: %s:%u\n", other_ip.ip.v4.ip, other_ip.ip.v4.port); 
            } break;
            case P2P_PACK_ACK: {  
                naddr_t other_ip = {0};
                memcpy(&other_ip, pkt->data, pkt->d_size);
                
                p2p_peer *peer = p2p_psystem_Ppeer(psys, pkt->h_from);
                if (!peer) {
                    printf("[disp] got unexcpected ACK from unknown %u\n", pkt->h_from);
                    goto disp_end;
                }

                if (peer->peer_id != pkt->h_from){
                    printf("[disp] got corrupted ACK from %u (!= %u)\n", peer->peer_id, pkt->h_from);
                    goto disp_end;
                }
                
                if (peer->status != P2P_STAT_PUNCHING && peer->status != P2P_STAT_ACK){
                    printf("[disp] got unexcpected ACK from %u with %d status\n", peer->peer_id, peer->status);
                    goto disp_end;
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
                p2p_psystem_makealive(psys, peer->peer_id);
            } break;

            case P2P_PACK_DATA: ; break;
            case P2P_PACK_PING: {
                p2p_peer *peer = p2p_psystem_peer(psys, pkt->h_from);
                if (peer == NULL){
                    printf("[disp] got unexpected PING from unconnected %u\n", pkt->h_from);
                    goto disp_end;
                }

                if (peer->peer_id != pkt->h_from){
                    printf("[disp] got corrupted PING from %u (!= %u)\n", pkt->h_from, peer->peer_id);
                    goto disp_end;
                }

                if (peer->status != P2P_STAT_ACTIVE){
                    printf("[disp] got suspicious PING from dead %u\n", pkt->h_from);
                    goto disp_end;
                }

                naddr_t other_ip = {0};
                memcpy(&other_ip, pkt->data, pkt->d_size);

                nnet_fd remote_fd = netfdq(other_ip);
                p2pnp_udp_pong(cli, other_ip, pkt->h_from, &remote_fd);
                peer->last_seen = get_timestump();

                printf("[disp] got ping, ponging...\n");
            } break;
            case P2P_PACK_PONG: {
                p2p_peer *peer = p2p_psystem_peer(psys, pkt->h_from);
                if (!peer){
                    printf("[disp] got unexpected PONG from unconnected %u\n", pkt->h_from);
                    goto disp_end;
                }

                if (peer->peer_id != pkt->h_from){
                    printf("[disp] got corrupted PONG from %u\n", pkt->h_from);
                    goto disp_end;
                }

                if (peer->status != P2P_STAT_ACTIVE){
                    printf("[disp] got suspicious PONG from dead %u\n", pkt->h_from);
                    goto disp_end;
                }

                printf("[disp] got pong\n");
                peer->last_seen = get_timestump();
            } break;
            break;
        }

disp_end:
        free(pkt);
    }
    
    return NULL;
}

void p2p_dispatcher_end(p2p_dispatcher *dispatcher){
    atomic_store(&dispatcher->is_active, false);
    pthread_join(dispatcher->main_thread, NULL);
}

int p2p_dispatcher_start(p2p_dispatcher *dispatcher){
    atomic_store(&dispatcher->is_active, true);
    int r = pthread_create(
        &dispatcher->main_thread,
        NULL,
        &p2p_dispatcher_worker,
        dispatcher
    );

    return r;
}

#endif
#define P2P_DISPATCHER