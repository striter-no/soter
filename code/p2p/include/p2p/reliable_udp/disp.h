#include <stdint.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include "methods.h"

#ifndef RUDP_TIMEOUT
#define RUDP_TIMEOUT 5
#endif

#ifndef REORDER_WINDOW
#define REORDER_WINDOW 5
#endif

#ifndef RUDP_RETRANSMISSION_CAP
#define RUDP_RETRANSMISSION_CAP 10
#endif

#ifndef P2P_RUDP_DISPATCHER

int p2p_rudp_chan_newpack(
    p2p_rudp_channel *chan,
    
    udp_packet      *packet,
    uint32_t         peer_id
){
    if (!chan || !packet) return -1;

    p2p_rudp_pending_pkt pkt = {
        .seq = chan->next_seq,
        .copy_pack = udp_copy_pack(packet, true),
        .retransmit_count = 0,
        .state = P2P_RUDP_STATE_INITED,
        .timestamp = get_timestump()
    };

    prot_array_push(&chan->pending_queue, &pkt);
    write(chan->sended_fd, &(uint64_t){1}, sizeof(uint64_t));

    return 0;
}

int p2p_rudp_chan_awaitpack(
    p2p_rudp_channel *chan,
    int              timeout
){
    if (!chan) return -1;

    if (0 >= evfd_wait(chan->reordered_fd, POLLIN, timeout))
        return -1;
    return 0;
}

int p2p_rudp_chan_getpack(
    p2p_rudp_channel *chan,
    udp_packet       **out
){
    udp_packet *pack = NULL;
    if (0 > prot_queue_pop(&chan->reoredered_queue, &pack))
        return -1;

    *out = udp_copy_pack(pack, false);
    free(pack);
    return 0;
}

int p2p_rudpdisp_pass(
    p2p_rudp_dispatcher *disp,
    udp_packet *pkt
){
    int r = prot_queue_push(&disp->passed_packs, &pkt);
    if (r < 0) return -1;

    write(disp->newpack_fd, &(uint64_t){1}, sizeof(uint64_t));
    return 0;
}

int p2p_rudp_sendack(
    p2p_udp          *client,
    p2p_rudp_channel *chan,
    uint32_t          seq
){
    udp_packet *pack = udp_make_pack(
        seq, client->UID, chan->UID, P2P_PACK_RACK, NULL, 0
    );

    return udp_pack_send(client, pack, chan->nfd);
}

int p2p_rudpdisp_init(
    p2p_rudp_dispatcher *disp,
    p2p_udp             *client,
    p2p_peers_system    *psys
){
    if (!disp || !client || !psys) return -1;

    disp->client = client;
    disp->psys   = psys;

    disp->newpack_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    disp->outgoing_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    disp->passed_packs = prot_queue_create(sizeof(udp_packet*));
    disp->outgoing_packs = prot_queue_create(sizeof(udp_packet*));

    disp->channels = prot_table_create(
        sizeof(uint32_t), sizeof(p2p_rudp_channel), 
        DYN_OWN_BOTH
    );

    atomic_store(&disp->is_active, false);
    return 0;
}

static void p2p_rudp_check_chtimeouts(
    p2p_udp *cli, 
    p2p_rudp_channel *chan, 
    p2p_peers_system *psys
){
    prot_array_lock(&chan->pending_queue);
    
    uint32_t currt = get_timestump();
    for (size_t i = 0; i < chan->pending_queue.array.len;){
        p2p_rudp_pending_pkt *pkt = dyn_array_at(
            &chan->pending_queue.array, i
        );

        if (pkt->retransmit_count >= RUDP_RETRANSMISSION_CAP){
            free(pkt->copy_pack);
            dyn_array_remove(&chan->pending_queue.array, i);
            SLOG_WARNING("[rudpdisp] retransmission cap hit");
            continue;
        }

        if (currt - pkt->timestamp >= RUDP_TIMEOUT){
            uint32_t peer_id = pkt->copy_pack->h_to;
            p2p_peer *p = p2p_psystem_peer(psys, peer_id);
            if (!p) {
                SLOG_ERROR("[rudpdisp] PSYS does not know about peer: %u", peer_id);
                free(pkt->copy_pack);
                dyn_array_remove(&chan->pending_queue.array, i);
                continue;
            }
            udp_pack_send(cli, retranslate_udp(pkt->copy_pack, 1), p->fd);
            pkt->timestamp = currt;
            pkt->retransmit_count++;
            i++;
            SLOG_DEBUG("[rudpdisp] rentrasmissing packet...");
            continue;
        } else if (pkt->retransmit_count == 0) {
            uint32_t peer_id = pkt->copy_pack->h_to;
            p2p_peer *p = p2p_psystem_peer(psys, peer_id);
            if (!p) {
                SLOG_ERROR("[rudpdisp] PSYS does not know about peer: %u", peer_id);
                free(pkt->copy_pack);
                dyn_array_remove(&chan->pending_queue.array, i);
                continue;
            }

            pkt->seq = chan->next_seq;
            udp_pack_send(cli, retranslate_udp(pkt->copy_pack, 1), p->fd);
            chan->next_seq++;
            pkt->retransmit_count++;
        }
        i++;
    }

    prot_array_unlock(&chan->pending_queue);
}

static void p2p_rudp_check_timeouts(p2p_rudp_dispatcher *disp){
    prot_table_lock(&disp->channels);
    for (size_t i = 0; i < disp->channels.table.array.len; i++){
        dyn_pair *p = dyn_array_at(&disp->channels.table.array, i);
        p2p_rudp_check_chtimeouts(disp->client, p->second, disp->psys);
    }
    prot_table_unlock(&disp->channels);
}

static void __sender(p2p_rudp_dispatcher *disp){
    udp_packet *pkt;
    if (0 > prot_queue_pop(&disp->outgoing_packs, (void**)&pkt)){
        SLOG_ERROR("[rudpdisp][senderworker] prot_queue_pop()");
        return;
    }

    if (!pkt) return;

    SLOG_DEBUG("[rudpdisp] new outgoing packet");
    SLOG_DEBUG("[rudpdisp] outgoing for %u", ntohl(pkt->h_to));
    uint32_t peer_id = ntohl(pkt->h_to);
    p2p_rudp_channel *chan = NULL;
    if (0 > p2p_rudpdisp_getchan(disp, &chan, peer_id)){
        SLOG_DEBUG("[rudpdisp] made new chan %u", peer_id);

        p2p_peer *p = p2p_psystem_peer(disp->psys, peer_id);
        if (!p) {
            SLOG_ERROR("[rudpdisp] PSYS does not know about peer: %u", peer_id);
            SLOG_DEBUG("[thread] FREEING packet: %p", pkt); 
            free(pkt);
            return;
        }

        p2p_rudpdisp_newchan(disp, peer_id, p->fd, &chan);
    }

    p2p_peer *p = p2p_psystem_peer(disp->psys, peer_id);
    if (!p) {
        SLOG_ERROR("[rudpdisp] PSYS does not know about peer: %u", peer_id);
        SLOG_DEBUG("[thread] FREEING packet: %p", pkt); 
        free(pkt);
        return;
    }

    SLOG_DEBUG("[rudpdisp] just SENT packet with SEQ %u", chan->next_seq);
    p2p_rudp_chan_newpack(chan, pkt, peer_id);
    free(pkt);
}

static void p2p_rudpdisp_chreordering(
    p2p_rudp_dispatcher *disp,
    p2p_rudp_channel    *chan,
    p2p_peers_system    *psys
){
    // SLOG_DEBUG("waiting network q");
    prot_queue_lock(&chan->network_queue);
    udp_packet *pack;

    // SLOG_DEBUG("unlocked");
    while (0 == prot_queue_pop(&chan->network_queue, &pack)){
        
        if (!pack) {
            // SLOG_DEBUG("skipping NULL reord");
            break;
        }

        uint32_t expected = (chan->last_recved_seq == UINT32_MAX) ? 0 : chan->last_recved_seq + 1;
        uint32_t diff = pack->seq - expected;

        if (diff > REORDER_WINDOW && diff < (0xFFFFFFFF - REORDER_WINDOW)) {
            free(pack);
            continue;
        }

        prot_array_lock(&chan->reorder_buffer);
        
        size_t pos = 0;
        bool duplicate = false;
        size_t len = chan->reorder_buffer.array.len;

        for (pos = 0; pos < len; pos++) {
            udp_packet **existing = dyn_array_at(&chan->reorder_buffer.array, pos);
            if ((*existing)->seq == pack->seq) {
                duplicate = true; 
                break;
            }
            if ((*existing)->seq > pack->seq) {
                break;
            }
        }

        if (!duplicate) {
            dyn_array_insert(&chan->reorder_buffer.array, pos, &pack);
        } else {
            free(pack);
        }
        
        prot_array_unlock(&chan->reorder_buffer);
    }
    prot_queue_unlock(&chan->network_queue);

    prot_array_lock(&chan->reorder_buffer);
    
    while (chan->reorder_buffer.array.len > 0) {
        udp_packet **p_ptr = dyn_array_at(&chan->reorder_buffer.array, 0);
        udp_packet *p = *p_ptr;

        uint32_t next_needed = (chan->last_recved_seq == UINT32_MAX) ? 0 : chan->last_recved_seq + 1;

        if (p->seq == next_needed) {
            prot_queue_push(&chan->reoredered_queue, &p);
            chan->last_recved_seq = p->seq;

            dyn_array_remove(&chan->reorder_buffer.array, 0);
            
            write(chan->reordered_fd, &(uint64_t){1}, 8);
        } else {
            break; 
        }
    }
    
    prot_array_unlock(&chan->reorder_buffer);
}

static void p2p_rudpdisp_reordering(p2p_rudp_dispatcher *disp){
    // SLOG_DEBUG("reorder lock");
    prot_table_lock(&disp->channels);
    // SLOG_DEBUG("reorder lock in");
    for (size_t i = 0; i < disp->channels.table.array.len; i++){
        // SLOG_DEBUG("reordering channel %zu\n", i);
        dyn_pair *p = dyn_array_at(&disp->channels.table.array, i);
        // SLOG_DEBUG("chreordering", i);
        p2p_rudpdisp_chreordering(disp, p->second, disp->psys);
    }
    prot_table_unlock(&disp->channels);
}


static void __worker(p2p_rudp_dispatcher *disp){
    udp_packet *pkt;
    if (0 > prot_queue_pop(&disp->passed_packs, (void**)&pkt)){
        SLOG_ERROR("[rudpdisp][worker] prot_queue_pop()");
        return;
    }

    if (!pkt) return;

    SLOG_DEBUG("[rudpdisp] new incoming packet");
    
    uint32_t peer_id = pkt->h_from;
    p2p_rudp_channel *chan = NULL;
    if (0 > p2p_rudpdisp_getchan(disp, &chan, peer_id)){
        SLOG_DEBUG("[rudpdisp] made new chan %u", peer_id);
        p2p_peer *p = p2p_psystem_peer(disp->psys, peer_id);
        if (!p) {
            SLOG_ERROR("[rudpdisp] PSYS does not know about peer: %u", peer_id);
            goto end;
        }
        p2p_rudpdisp_newchan(disp, peer_id, p->fd, &chan);
    }

    if (pkt->packtype == P2P_PACK_DATA){
        uint32_t seq = pkt->seq;
        SLOG_DEBUG("[rudpdisp] just READ packet with SEQ %u", seq);
        
        prot_queue_push(&chan->network_queue, &pkt);
        write(chan->netpack_fd, &(uint64_t){1}, 8);

        p2p_rudp_sendack(disp->client, chan, seq);
        chan->last_ack_sent = seq;

        // no free(pkt);
        return;
    } else if (pkt->packtype == P2P_PACK_RACK){
        prot_array_lock(&chan->pending_queue);
        
        bool was_ack = false;
        for (size_t i = 0; i < chan->pending_queue.array.len;){
            p2p_rudp_pending_pkt *ppkt = prot_array_at(&chan->pending_queue, i);
            if (ppkt->seq == pkt->seq){
                SLOG_DEBUG("[rudpdisp][ack] acked in channel %u, seq %u", peer_id, ppkt->seq);
                chan->last_ack_received = pkt->seq;

                free(ppkt->copy_pack);
                prot_array_remove(&chan->pending_queue, i);
                was_ack = true;
                break;
            } else {
                i++;
            }
        }

        if (!was_ack){
            SLOG_WARNING("[rudpdisp][warn] ACK was recved, but no suitable SEQ was found: %u", pkt->seq);
        }

        prot_array_unlock(&chan->pending_queue);
    }

end:
    free(pkt);
}

// serves incoming messages
static void *p2p_rudpdisp_worker(void *_args){
    p2p_rudp_dispatcher *disp = _args;

    struct pollfd fds[2] = {
        {.fd = disp->newpack_fd, .events = POLLIN},
        {.fd = disp->outgoing_fd, .events = POLLIN}
    };

    while (atomic_load(&disp->is_active)){
        int ret = poll(fds, 2, 100);

        p2p_rudpdisp_reordering(disp);
        p2p_rudp_check_timeouts(disp);
        
        if (ret == 0) continue;
        else if (ret < 0) {
            perror("rudpdisp: poll()");
            continue;
        }

        uint64_t r;
        if (fds[0].revents & POLLIN){
            __worker(disp);
            read(fds[0].fd, &r, 8);
        }

        if (fds[1].revents & POLLIN){
            __sender(disp);
            read(fds[1].fd, &r, 8);
        }
    }
    
    return NULL;
}

void p2p_rudpdisp_stop(
    p2p_rudp_dispatcher *disp
){
    atomic_store(&disp->is_active, false);
    SLOG_DEBUG("joining main thread");
    pthread_join(disp->main_thread, NULL);
}

int p2p_rudpdisp_run(
    p2p_rudp_dispatcher *disp
){
    if (!disp) return -1;
    atomic_store(&disp->is_active, true);
    disp->newchan_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

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

    SLOG_DEBUG("dispatcher ending");
    p2p_rudpdisp_stop(disp);

    SLOG_DEBUG("closing channels");
    for (size_t i = 0; i < disp->channels.table.array.len; i++){
        dyn_pair *p = dyn_array_at(&disp->channels.table.array, i);
        p2p_rudp_channel *chan = p->second;
        close(chan->netpack_fd);
        close(chan->sended_fd);
        close(chan->reordered_fd);

        for (size_t i = 0; i < chan->network_queue.arr.array.len; i++){
            udp_packet *pack;
            prot_queue_pop(&chan->network_queue, &pack);

            SLOG_DEBUG("[end] freeing unhandled passed packet");
            free(pack);
        }

        for (size_t i = 0; i < chan->pending_queue.array.len; i++){
            free(((p2p_rudp_pending_pkt*)prot_array_at(&chan->pending_queue, i))->copy_pack);
        }

        prot_array_end(&chan->pending_queue);
        prot_queue_end(&chan->network_queue);
        prot_array_end(&chan->reorder_buffer);
        prot_queue_end(&chan->reoredered_queue);
    }

    for (size_t i = 0; i < disp->passed_packs.arr.array.len; i++){
        udp_packet *pack;
        prot_queue_pop(&disp->passed_packs, &pack);

        SLOG_DEBUG("[end] freeing unhandled passed packet");
        free(pack);
    }

    prot_queue_end(&disp->passed_packs);
    prot_queue_end(&disp->outgoing_packs);
    prot_table_end(&disp->channels);
    close(disp->newpack_fd);
    close(disp->newchan_fd);
}

#endif
#define P2P_RUDP_DISPATCHER