#include "methods.h"

#ifndef P2P_RUDP_SENDER

typedef struct {
    p2p_udp *client;

    prot_queue       pending_packets;
    p2p_rudp_channel *channel;
} p2p_rudp_sender;

int p2p_rudp_sender_init(
    p2p_rudp_sender *sender,
    p2p_udp         *client
){
    if (!sender || !client) return -1;

    sender->client = client;
    sender->pending_packets = prot_queue_create(sizeof(p2p_rudp_pending_pkt));
    sender->channel = NULL;

    return 0;
}

int p2p_rudp_sender_end(
    p2p_rudp_sender *sender
){
    if (!sender) return -1;
    prot_queue_end(&sender->pending_packets);
    return 0;
}

int p2p_rudp_sender_push(
    p2p_rudp_sender *sender,
    udp_packet      *pack,
    uint32_t         addr_pid
){
    return 0;
}

int p2p_rudp_sender_iter(
    p2p_rudp_sender *sender
){
    return 0;
}

#endif
#define P2P_RUDP_SENDER