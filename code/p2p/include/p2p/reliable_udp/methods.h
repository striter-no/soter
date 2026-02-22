#include <p2p/peers.h>
#include <base/prot_queue.h>

#ifndef P2P_RUDP_METHODS

typedef enum {
    P2P_RUDP_STATE_SENT,
    P2P_RUDP_STATE_ACKED,
    P2P_RUDP_STATE_LOST
} p2p_rudp_pkt_state;

typedef struct {
    uint32_t            seq;
    uint32_t            timestamp;
    int                 retransmit_count;
    udp_packet         *real_pack;
    p2p_rudp_pkt_state  state;
} p2p_rudp_pending_pkt;

typedef struct {
    uint32_t next_seq;
    uint32_t last_ack_received;
    uint32_t last_ack_sent;
    prot_queue pending_queue;
    prot_array reorder_buffer;
    // uint64_t rtt;
    // uint64_t timeout;
} p2p_rudp_channel;

#endif
#define P2P_RUDP_METHODS