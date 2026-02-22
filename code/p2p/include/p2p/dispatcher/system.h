#include "structs.h"
#include "methods.h"

#ifndef P2P_DISPATCHER

int p2p_dispatcher_init(
    p2p_dispatcher      *dispatcher,
    p2p_listener        *listener,
    p2p_peers_system    *psyst,
    gossip_system       *gossip,
    p2p_rudp_dispatcher *rudp_disp
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
            printf("[disp][liveness] removing timeouted peer %u\n", peer->peer_id);
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

    // printf("[gossip] starting gossiping\n");
    prot_table_lock(&psys->peers);
    for (size_t i = 0; i < psys->peers.table.array.len; i++){
        dyn_pair *p = dyn_array_at(&psys->peers.table.array, i);
        p2p_peer *peer = p->second;
        udp_packet *gs_pck = gossip_make_packet(
            gsip, 
            cli->UID, 
            peer->peer_id
        );
        
        udp_pack_send(cli, gs_pck, peer->fd);
    }
    prot_table_unlock(&psys->peers);
    // printf("[gossip] end\n");

    disp->last_gossiping = get_timestump();
}

void *p2p_dispatcher_worker(void *_args){
    
    void *methods[] = {
        NULL, // disp_method_data     // 0
        disp_method_ack,              // 1
        disp_method_ping,             // 2
        disp_method_pong,             // 3
        disp_method_punch,            // 4
        disp_method_gossip,           // 5
        NULL, // disp_method_hello    // 6
        NULL, // disp_method_reject   // 7
        NULL, // disp_method_accept   // 8
        NULL  // (STATE)              // 9
    };
    
    p2p_dispatcher *disp = _args;
    p2p_listener   *list = disp->listener;
    p2p_udp        *cli  = list->p_client;

    struct pollfd newpack_fd[1] = {{.fd = list->pack_eventfd, .events = POLLIN}};
    while (atomic_load(&disp->is_active)){
        int r = poll(newpack_fd, 1, 100);
        p2p_dispatcher_liveness_check(disp);
        p2p_dispatcher_gossiping(disp);
        
        if (r == 0) { continue; }
        else if (r < 0) {
            perror("[disp] dispatcher:poll()");
            continue;
        }

        uint64_t pack;
        read(list->pack_eventfd, &pack, sizeof(uint64_t));

        udp_packet *pkt = NULL;
        if (prot_queue_pop(&list->packets, (void**)&pkt) != 0){
            perror("[disp] failed to get packet, something went wrong");
            continue;
        }

        // printf("[disp] POP: %p\n", pkt);
        
        if (!pkt) continue;
        // printf("[disp] packtype: %u\n", (uint8_t)pkt->packtype);
        
        if (udp_is_RUDP_req(pkt->packtype)){
            printf("[disp] passing packet to RUDP dispatcher\n");
            p2p_rudpdisp_pass(disp->rudp_disp, pkt);
            continue;
        }
        
        void *method = methods[(uint8_t)pkt->packtype];
        if (method){
            (DISPATCHER_METHOD method)(pkt, disp, cli);
        } else {
            printf("[disp] no method for this type of packet (%u)\n", pkt->packtype);
        }
        
        free(pkt);
    }
    
    return NULL;
}

void p2p_dispatcher_end(p2p_dispatcher *dispatcher){
    p2p_rudpdisp_end(dispatcher->rudp_disp);
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

    p2p_rudpdisp_run(dispatcher->rudp_disp);

    return r;
}

#endif
#define P2P_DISPATCHER