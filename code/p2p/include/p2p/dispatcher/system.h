#include "structs.h"
#include "methods.h"
#include <stdint.h>
#include <sys/eventfd.h>

#ifndef P2P_STATE_SKIPTICKS 
#define P2P_STATE_SKIPTICKS 20 /*2 seconds*/
#endif

#ifndef P2P_DISPATCHER

int p2p_dispatcher_init(
    p2p_dispatcher      *dispatcher,
    p2p_listener        *listener,
    p2p_peers_system    *psyst,
    gossip_system       *gossip,
    p2p_rudp_dispatcher *rudp_disp,
    naddr_t              state_server
){
    if (!dispatcher || !listener || 
        !psyst      || !gossip   ||
        !rudp_disp                 ) return -1;

    dispatcher->listener  = listener;
    dispatcher->p_client  = listener->p_client;
    dispatcher->psys      = psyst;
    dispatcher->gossip    = gossip;
    dispatcher->rudp_disp = rudp_disp;
    dispatcher->last_gossiping = 0;
    dispatcher->sstate_evfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    dispatcher->state_evfds = prot_array_create(sizeof(p2p_state));
    dispatcher->state_nfd   = netfdq(state_server);

    return 0;
}

static void p2p_dispatcher_liveness_check(p2p_dispatcher *disp){
    p2p_peers_system *psys = disp->psys;
    p2p_udp          *cli  = disp->p_client;

    prot_table_lock(&psys->peers);
    for (size_t i = 0; i < psys->peers.table.array.len;){
        dyn_pair *p = dyn_array_at(&psys->peers.table.array, i);
        p2p_peer *peer = p->second;

        if (get_timestump() - peer->last_seen >= P2P_PEER_DEAD_DT){
            peer->status = P2P_STAT_TIMEOUT;
        }
        
        if (get_timestump() - peer->last_seen == P2P_PEER_PING_DT){
            p2pnp_udp_ping(cli, naddr_nfd2str(peer->fd), peer->peer_id, &peer->fd);
            peer->last_seen = get_timestump();
        }

        if (peer->status == P2P_STAT_TIMEOUT){
            SLOG_DEBUG("[disp][liveness] removing timeouted peer %u", peer->peer_id);
            prot_table_remove(&psys->peers, &peer->peer_id);
        } else i++;
    }
    prot_table_unlock(&psys->peers);
}

static void p2p_dispatcher_gossiping(p2p_dispatcher *disp){
    gossip_system *gsip = disp->gossip;
    p2p_udp       *cli  = disp->p_client;
    p2p_peers_system *psys = disp->psys;

    if (get_timestump() - disp->last_gossiping < GOSSIP_DT)
        return;

    prot_table_lock(&psys->peers);
    for (size_t i = 0; i < psys->peers.table.array.len; i++){
        dyn_pair *p = dyn_array_at(&psys->peers.table.array, i);
        p2p_peer *peer = p->second;
        udp_packet *gs_pck = gossip_make_packet(
            gsip, 
            peer
        );
        
        udp_pack_send(cli, gs_pck, peer->fd);
    }
    prot_table_unlock(&psys->peers);

    disp->last_gossiping = get_timestump();
}

static void p2p_dispatcher_stateserving(p2p_dispatcher *disp){
    // SLOG_DEBUG("p2p_dispatcher_stateserving performed");
    
    unsigned char data[sizeof(uint32_t) + SOTER_PUBKEY_BYTES] = {0};
    memcpy(data, &disp->p_client->UID, sizeof(uint32_t));
    memcpy(data + sizeof(uint32_t), &disp->psys->kp.public_key, SOTER_PUBKEY_BYTES);

    udp_packet *pack = udp_make_pack(
        0, disp->p_client->UID, 
        0, P2P_PACK_STATE, data, sizeof(data)
    );

    udp_pack_send(disp->p_client, pack, disp->state_nfd);
}

void *p2p_dispatcher_worker(void *_args){
    
    void *methods[] = {
        NULL, // RUDP DATA            // 0
        disp_method_ack,              // 1
        disp_method_ping,             // 2
        disp_method_pong,             // 3
        disp_method_punch,            // 4
        disp_method_gossip,           // 5
        NULL, // disp_method_hello    // 6
        NULL, // disp_method_reject   // 7
        NULL, // disp_method_accept   // 8
        disp_method_state,            // 9
        NULL, // RUDP RACK            // 10
    };
    
    p2p_dispatcher *disp = _args;
    p2p_listener   *list = disp->listener;
    p2p_udp        *cli  = list->p_client;

    int skipped_ticks = 0;

    struct pollfd newpack_fd[1] = {{.fd = list->pack_eventfd, .events = POLLIN}};
    while (atomic_load(&disp->is_active)){
        int r = poll(newpack_fd, 1, 100);
        p2p_dispatcher_liveness_check(disp);
        p2p_dispatcher_gossiping(disp);
        
        if (skipped_ticks % P2P_STATE_SKIPTICKS == 0){
            p2p_dispatcher_stateserving(disp);
        }
        skipped_ticks++;

        if (r == 0) { continue; }
        else if (r < 0) {
            perror("[disp] dispatcher:poll()");
            continue;
        }

        uint64_t pack;
        read(list->pack_eventfd, &pack, sizeof(uint64_t));

        udp_packet *pkt = NULL;
        if (prot_queue_pop(&list->packets, (void**)&pkt) != 0){
            SLOG_ERROR("[disp] failed to get packet, something went wrong");
            continue;
        }

        
        if (!pkt) continue;
        
        if (udp_is_RUDP_req(pkt->packtype)){
            p2p_rudpdisp_pass(disp->rudp_disp, pkt);
            continue;
        }
        
        void *method = methods[(uint8_t)pkt->packtype];
        if (method){
            (DISPATCHER_METHOD method)(pkt, disp, cli);
        } else {
            SLOG_WARNING("[disp] no method for this type of packet (%u)", pkt->packtype);
        }
        
        free(pkt);
    }
    
    return NULL;
}

void p2p_dispatcher_end(p2p_dispatcher *dispatcher){
    atomic_store(&dispatcher->is_active, false);
    pthread_join(dispatcher->main_thread, NULL);

    prot_array_end(&dispatcher->state_evfds);
    close(dispatcher->sstate_evfd);
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