#include <stdint.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include "methods.h"

#ifndef RUDP_TIMEOUT
#define RUDP_TIMEOUT 5
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
    udp_packet       **out,
    int              timeout
){
    if (!chan) return -1;

    if (0 >= evfd_wait(chan->netpack_fd, POLLIN, timeout))
        return -1;

    udp_packet *pack;
    if (0 > prot_queue_pop(&chan->network_queue, &pack))
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

    printf("[rudpdisp] PUSH: %p\n", pkt);
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
            printf("[rudpdisp] retransmission cap hit\n");
            continue;
        }

        if (currt - pkt->timestamp >= RUDP_TIMEOUT){
            uint32_t peer_id = pkt->copy_pack->h_to;
            p2p_peer *p = p2p_psystem_peer(psys, peer_id);
            if (!p) {
                printf("[rudisp] PSYS does not know about peer: %u\n", peer_id);
                free(pkt->copy_pack);
                dyn_array_remove(&chan->pending_queue.array, i);
                continue;
            }
            udp_pack_send(cli, retranslate_udp(pkt->copy_pack, 1), p->fd);
            pkt->timestamp = currt;
            pkt->retransmit_count++;
            i++;
            printf("[rudpdisp] rentrasmissing packet...\n");
            continue;
        } else if (pkt->retransmit_count == 0){
            // first sending ever
            
            uint32_t peer_id = pkt->copy_pack->h_to;
            p2p_peer *p = p2p_psystem_peer(psys, peer_id);
            if (!p) {
                printf("[rudisp] PSYS does not know about peer: %u\n", peer_id);
                free(pkt->copy_pack);
                dyn_array_remove(&chan->pending_queue.array, i);
                continue;
            }

            pkt->seq = chan->next_seq;
            udp_pack_send(cli, retranslate_udp(pkt->copy_pack, 1), p->fd);
            chan->next_seq++;
            i++;
        }
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

static void *p2p_rudpdisp_senderworker(void *_args){
    p2p_rudp_dispatcher *disp = _args;
    
    printf("[thread] p2p_rudpdisp_senderworker start\n");
    while (atomic_load(&disp->is_active)){
        int r = evfd_wait(disp->outgoing_fd, POLLIN, 100);

        p2p_rudp_check_timeouts(disp);

        if (r <= 0) continue;

        udp_packet *pkt;
        printf("disp->outgoing_packs: %zu\n", disp->outgoing_packs.arr.array.len);
        if (0 > prot_queue_pop(&disp->outgoing_packs, (void**)&pkt)){
            perror("prot_queue_pop()");
            continue;
        }

        if (!pkt) continue;

        printf("[rudisp] new outgoing packet\n");
        printf("[rudisp] outgoing for %u\n", ntohl(pkt->h_to));
        uint32_t peer_id = ntohl(pkt->h_to);
        p2p_rudp_channel *chan = NULL;
        if (0 > p2p_rudpdisp_getchan(disp, &chan, peer_id)){
            printf("[rudisp] made new chan %u\n", peer_id);

            p2p_peer *p = p2p_psystem_peer(disp->psys, peer_id);
            if (!p) {
                printf("[rudisp] PSYS does not know about peer: %u\n", peer_id);
                printf("[thread] FREEING packet: %p\n", pkt); free(pkt);
                continue;
            }

            p2p_rudpdisp_newchan(disp, peer_id, p->fd, &chan);
        }

        p2p_peer *p = p2p_psystem_peer(disp->psys, peer_id);
        if (!p) {
            printf("[rudisp] PSYS does not know about peer: %u\n", peer_id);
            printf("[thread] FREEING packet: %p\n", pkt); free(pkt);
            continue;
        }

        // теперь пакет в pending_queue, sended_fd тригернут
        printf("[rudisp] just SENT packet with SEQ %u\n", chan->next_seq);
        p2p_rudp_chan_newpack(chan, pkt, peer_id);
        free(pkt);
    }
    printf("[thread] p2p_rudpdisp_senderworker end\n");

    return NULL;
}

// serves incoming messages
static void *p2p_rudpdisp_worker(void *_args){
    p2p_rudp_dispatcher *disp = _args;

    printf("[thread] p2p_rudpdisp_worker start\n");
    while (atomic_load(&disp->is_active)){
        int ret = evfd_wait(disp->newpack_fd, POLLIN, 100);
        if (ret == 0) continue;
        else if (ret < 0) {
            perror("rudisp: poll()");
            continue;
        }

        udp_packet *pkt;
        if (0 > prot_queue_pop(&disp->passed_packs, (void**)&pkt)){
            perror("prot_queue_pop()");
            break;
        }

        if (!pkt) continue;

        printf("[rudisp] new incoming packet\n");
        
        uint32_t peer_id = pkt->h_from;
        p2p_rudp_channel *chan = NULL;
        if (0 > p2p_rudpdisp_getchan(disp, &chan, peer_id)){
            printf("[rudisp] made new chan %u\n", peer_id);
            p2p_peer *p = p2p_psystem_peer(disp->psys, peer_id);
            if (!p) {
                printf("[rudisp] PSYS does not know about peer: %u\n", peer_id);
                goto end;
            }
            p2p_rudpdisp_newchan(disp, peer_id, p->fd, &chan);
        }

        if (pkt->packtype == P2P_PACK_DATA){
            uint32_t seq = pkt->seq;
            printf("[rudisp] just READ packet with SEQ %u\n", seq);
            
            prot_queue_push(&chan->network_queue, &pkt);
            write(chan->netpack_fd, &(uint64_t){1}, 8);

            p2p_rudp_sendack(disp->client, chan, seq);
            chan->last_ack_sent = seq;

            // no free(pkt);
            continue;
        } else if (pkt->packtype == P2P_PACK_RACK){
            prot_array_lock(&chan->pending_queue);
            
            bool was_ack = false;
            for (size_t i = 0; i < chan->pending_queue.array.len;){
                p2p_rudp_pending_pkt *ppkt = prot_array_at(&chan->pending_queue, i);
                if (ppkt->seq == pkt->seq){
                    printf("[rudisp][ack] acked in channel %u, seq %u\n", peer_id, ppkt->seq);
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
                printf("[rudisp][warn] ACK was recved, but no suitable SEQ was found: %u\n", pkt->seq);
            }

            prot_array_unlock(&chan->pending_queue);
        }

end:
        printf("[thread] FREEING packet: %p\n", pkt); free(pkt);
    }
    printf("[thread] p2p_rudpdisp_worker end\n");
    
    return NULL;
}

void p2p_rudpdisp_stop(
    p2p_rudp_dispatcher *disp
){
    atomic_store(&disp->is_active, false);
    pthread_join(disp->incoming_thread, NULL);
    pthread_join(disp->outgoing_thread, NULL);
    disp->incoming_thread = 0;
    disp->outgoing_thread = 0;
}

int p2p_rudpdisp_run(
    p2p_rudp_dispatcher *disp
){
    if (!disp) return -1;
    atomic_store(&disp->is_active, true);
    disp->newchan_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    int r;
    r = pthread_create(
        &disp->outgoing_thread,
        NULL,
        &p2p_rudpdisp_senderworker,
        disp
    );

    if (r != 0) return r;

    r = pthread_create(
        &disp->incoming_thread,
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

    for (size_t i = 0; i < disp->channels.table.array.len; i++){
        dyn_pair *p = dyn_array_at(&disp->channels.table.array, i);
        p2p_rudp_channel *chan = p->second;
        close(chan->netpack_fd);
        close(chan->sended_fd);
        close(chan->reordered_fd);

        for (size_t i = 0; i < chan->network_queue.arr.array.len; i++){
            udp_packet *pack;
            prot_queue_pop(&chan->network_queue, &pack);

            printf("[end] freeing unhandled passed packet\n");
            free(pack);
        }

        prot_array_end(&chan->pending_queue);
        prot_queue_end(&chan->network_queue);
        prot_queue_end(&chan->reoredered_queue);
    }

    for (size_t i = 0; i < disp->passed_packs.arr.array.len; i++){
        udp_packet *pack;
        prot_queue_pop(&disp->passed_packs, &pack);

        printf("[end] freeing unhandled passed packet\n");
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