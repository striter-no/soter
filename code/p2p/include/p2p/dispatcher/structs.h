#include <p2p/listener.h>
#include <p2p/peers.h>
#include <p2p/gossip.h>

#ifndef P2P_DISPATCHER_STRUCTS

#define DISPATCHER_METHOD (bool (*)(udp_packet *, p2p_dispatcher *, p2p_udp*))

typedef struct {
    pthread_t    main_thread;
    atomic_bool  is_active;
    p2p_udp     *p_client;

    
    p2p_listener     *listener;
    p2p_peers_system *psys;
    gossip_system    *gossip;

    uint32_t          last_gossiping;
} p2p_dispatcher;

typedef enum {
    SANITY_OK,
    SANITY_UNCONNECTED,
    SANITY_CORRUPTED,
    SANITY_DEAD
} p2p_disp_sanity;

#endif
#define P2P_DISPATCHER_STRUCTS