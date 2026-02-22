#include <p2p/peers.h>
#include <base/prot_queue.h>
#include <p2p/listener.h>
#include <stdint.h>
#include <sys/types.h>

#ifndef P2P_RUDP_METHODS

typedef enum {
    P2P_RUDP_STATE_INITED,
    P2P_RUDP_STATE_SENT,
    P2P_RUDP_STATE_ACKED,
    P2P_RUDP_STATE_LOST
} p2p_rudp_pkt_state;

typedef struct {
    uint32_t            seq;
    uint32_t            timestamp;
    int                 retransmit_count;
    // udp_packet         *real_pack;
    p2p_rudp_pkt_state  state;
} p2p_rudp_pending_pkt;

typedef struct {
    uint32_t next_seq;
    uint32_t last_ack_received;
    uint32_t last_ack_sent;
    prot_array pending_queue; // from user
    prot_queue network_queue; // from net
    prot_array reorder_buffer;
    prot_queue reoredered_queue; // from net, already reordered

    int        reordered_fd;
    int        netpack_fd;
    int        sended_fd;
    uint32_t   UID;
    nnet_fd    nfd;
} p2p_rudp_channel;

typedef struct {
    p2p_udp      *client;
    p2p_peers_system *psys;

    prot_table    channels;
    prot_queue    passed_packs;
    prot_queue    outgoing_packs;

    atomic_bool   is_active;
    pthread_t     incoming_thread;
    pthread_t     outgoing_thread;
    int           outgoing_fd;
    int           newpack_fd;
    int           newchan_fd;
} p2p_rudp_dispatcher;

typedef struct {
    p2p_rudp_dispatcher disp;
    int                 newpack_evfd;
} p2p_rudp_system;

int p2p_rudpdisp_waitchan(
    p2p_rudp_dispatcher  *disp,
    uint32_t              peer_uid
){
    p2p_rudp_channel *ch = prot_table_get(&disp->channels, &peer_uid);
    if (ch != NULL) return 0;

    while (true){
        int r = evfd_wait(disp->newchan_fd, POLLIN, -1);
        if (r <= 0) 
            return r;

        if (prot_table_get(&disp->channels, &peer_uid) == NULL)
            continue;

        return 0;
    }
}

int p2p_rudpdisp_getchan(
    p2p_rudp_dispatcher  *syst,
    p2p_rudp_channel **channel,
    uint32_t          peer_uid
){
    p2p_rudp_channel *ch = prot_table_get(&syst->channels, &peer_uid);
    *channel = ch;

    if (ch == NULL)
        return -1;
    return 0;
}

void p2p_rudp_chaninit(p2p_rudp_channel *out, uint32_t UID, nnet_fd nfd){
    out->next_seq = 0;
    out->last_ack_received = 0;
    out->last_ack_sent     = 0;
    
    out->pending_queue    = prot_array_create(sizeof(p2p_rudp_pending_pkt));
    out->network_queue    = prot_queue_create(sizeof(udp_packet*));
    out->reoredered_queue = prot_queue_create(sizeof(udp_packet*));
    out->reorder_buffer   = prot_array_create(sizeof(udp_packet*));

    out->reordered_fd     = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    out->netpack_fd       = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    out->sended_fd        = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    out->UID = UID;
    out->nfd = nfd;
}

int p2p_rudpdisp_newchan(
    p2p_rudp_dispatcher  *syst,
    uint32_t              peer_uid,
    nnet_fd               nfd,
    p2p_rudp_channel    **out
){
    p2p_rudp_channel chan;
    p2p_rudp_chaninit(&chan, peer_uid, nfd);

    prot_table_set(&syst->channels, &peer_uid, &chan);
    write(syst->newchan_fd, &(uint64_t){1}, 8);
    
    *out = prot_table_get(&syst->channels, &peer_uid);
    return 0;
}

#endif
#define P2P_RUDP_METHODS