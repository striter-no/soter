#include <base/prot_queue.h>
#include <p2p/listener.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include "methods.h"

#ifndef RUDP_TIMEOUT
#define RUDP_TIMEOUT 5
#endif

#ifndef P2P_RUDP_SYSTEM

typedef struct {
    p2p_listener *listener;
    p2p_udp      *client;
    prot_queue    passed_packs;

    prot_table    channels;
    atomic_bool   is_active;
    pthread_t     main_thread;
    int           newpackfd;
} p2p_rudp_dispatcher;

int p2p_rudpdisp_pass(
    p2p_rudp_dispatcher *disp,
    udp_packet *pkt
){

    int r = prot_queue_push(&disp->passed_packs, &pkt);
    if (r < 0) return -1;

    write(disp->newpackfd, &(uint64_t){1}, sizeof(uint64_t));
    return 0;
}

int p2p_rudpdisp_init(
    p2p_rudp_dispatcher *disp,
    p2p_listener *listener,
    p2p_udp      *client
){
    if (!disp || !listener || !client) return -1;

    disp->listener = listener;
    disp->client = client;
    disp->channels = prot_table_create(
        sizeof(uint32_t), sizeof(p2p_rudp_channel), 
        DYN_OWN_BOTH
    );

    disp->newpackfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    atomic_store(&disp->is_active, false);
    return 0;
}

static void *p2p_rudpdisp_worker(void *_args){
    p2p_rudp_dispatcher *disp = _args;
    struct pollfd fds[1] = {{.fd = disp->newpackfd, .events = POLLIN}};

    while (atomic_load(&disp->is_active)){
        int ret = poll(fds, 1, 100);
        if (ret == 0) continue;
        else if (ret < 0) {
            perror("rudisp: poll()");
            continue;
        }

        udp_packet *pkt;
        if (0 > prot_queue_pop(&disp->passed_packs, (void**)&pkt)){
            perror("prot_queue_pop()");
            continue;
        }

        if (!pkt) continue;

        printf("[rudisp] new packet\n");

        free(pkt);
    }
    
    return NULL;
}

void p2p_rudpdisp_stop(
    p2p_rudp_dispatcher *disp
){
    atomic_store(&disp->is_active, false);
    pthread_join(disp->main_thread, NULL);
}

int p2p_rudpdisp_run(
    p2p_rudp_dispatcher *disp
){
    if (!disp) return -1;
    atomic_store(&disp->is_active, true);

    int r = pthread_create(
        &disp->main_thread,
        NULL,
        &p2p_rudpdisp_worker,
        disp
    );

    return r;
}

void p2p_rudpdisp_end(
    p2p_rudp_dispatcher *disp
){
    if (!disp) return;

    p2p_rudpdisp_stop(disp);
    prot_table_end(&disp->channels);
    close(disp->newpackfd);
}

#endif
#define P2P_RUDP_SYSTEM