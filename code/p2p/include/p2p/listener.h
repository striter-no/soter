#include <stdatomic.h>
#include <netcore/udp_proto.h>
#include <base/prot_queue.h>
#include <base/prot_table.h>
#include <sys/eventfd.h>

#include "peers.h"

#ifndef P2P_LISTENER

typedef struct {
    atomic_bool is_running;
    p2p_udp    *p_client;
    prot_queue  packets;

    pthread_t   main_thread;
    int         pack_eventfd;
} p2p_listener;

int p2p_listener_init(
    p2p_listener *listener,
    p2p_udp      *main_client
){
    if (!listener || !main_client) return -1;
    listener->p_client = main_client;
    // stores allocated packets
    listener->packets = prot_queue_create(sizeof(udp_packet*)); 
    listener->pack_eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    // listener->main_thread remains untouched until start
    return 0;
}

static void *p2p_listener_worker(void *_args){
    p2p_listener *list = _args;
    
    uint64_t v = 1;
    while (atomic_load(&list->is_running)){
        if (netfd_wait(list->p_client->fd, POLLIN, 100) <= 0) continue;

        nnet_fd remote = {0};
        udp_packet *incoming = udp_pack_recv(list->p_client, &remote);
        if (!incoming) {
            printf("[listener] aborted packet\n");
            continue;
        }

        // naddr_t addr = naddr_nfd2str(remote);
        // printf("[listener] incoming packet from %s:%u\n", addr.ip.v4.ip, addr.ip.v4.port);
        // printf("[listener] PUSH: %p\n", incoming);
        prot_queue_push(&list->packets, &incoming);
        write(list->pack_eventfd, &v, sizeof(v));
    }

    return NULL;
}

void p2p_listener_end(p2p_listener *listener){
    atomic_store(&listener->is_running, false);
    pthread_join(listener->main_thread, NULL);
    
    prot_queue_end(&listener->packets);
}

int p2p_listener_start(p2p_listener *listener){
    atomic_store(&listener->is_running, true);
    int r = pthread_create(
        &listener->main_thread,
        NULL,
        &p2p_listener_worker,
        listener
    );

    return r;
}

#endif
#define P2P_LISTENER