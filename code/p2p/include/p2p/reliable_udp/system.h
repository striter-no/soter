#include "methods.h"
#include "disp.h"
#include <stdint.h>
#include <sys/eventfd.h>

#ifndef P2P_RUDP_SYSTEM

int p2p_rudp_systeminit(
    p2p_rudp_system *sys,
    p2p_udp         *client,
    p2p_peers_system *psys
){
    if (!sys) return -1;

    if (0 > p2p_rudpdisp_init(&sys->disp, client, psys))
        return -1;

    sys->newpack_evfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    return 0;
}

int p2p_rudp_systemrun(
    p2p_rudp_system *sys
){
    if (!sys) return -1;
    p2p_rudpdisp_run(&sys->disp);
    return 0;
}

int p2p_rudp_newpack(
    p2p_rudp_system *sys,
    
    udp_packet      *packet,
    uint32_t         peer_id
){
    if (!sys || !packet) return -1;

    if (0 > prot_queue_push(&sys->disp.outgoing_packs, &packet))
        return -1;

    write(sys->disp.outgoing_fd, &(uint64_t){1}, 8);
    return 0;
}

int p2p_rudp_systemend(
    p2p_rudp_system *sys
){
    if (!sys) return -1;

    p2p_rudpdisp_end(&sys->disp);
    close(sys->newpack_evfd);

    return 0;
}

#endif
#define P2P_RUDP_SYSTEM