#include <p2p/listener.h>
#include <p2p/peers.h>
#include <p2p/gossip.h>
#include <p2p/reliable_udp/system.h>
#include <stdint.h>

#ifndef P2P_DISPATCHER_STRUCTS

#define DISPATCHER_METHOD (bool (*)(udp_packet *, p2p_dispatcher *, p2p_udp*))

typedef struct {
    pthread_t    main_thread;
    atomic_bool  is_active;
    p2p_udp     *p_client;
    uint32_t     last_gossiping;

    p2p_rudp_dispatcher *rudp_disp;
    p2p_listener        *listener;
    p2p_peers_system    *psys;
    gossip_system       *gossip;

    nnet_fd              state_nfd;
    prot_array           state_evfds;
    int                  sstate_evfd;
} p2p_dispatcher;

typedef struct {
    naddr_t  ip;
    uint32_t UID;
    int      stfd;
    unsigned char pubkey[SOTER_PUBKEY_BYTES];
} p2p_state;

typedef enum {
    SANITY_OK,
    SANITY_UNCONNECTED,
    SANITY_CORRUPTED,
    SANITY_DEAD
} p2p_disp_sanity;

#endif
#define P2P_DISPATCHER_STRUCTS