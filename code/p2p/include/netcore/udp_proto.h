#include "udp_sock.h"
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef P2P_UDP_PROTO
#define P2P_UDP_MAGIC 0x015432

static uint32_t crc32(const void *data, size_t n_bytes) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *byte_ptr = (const uint8_t *)data;

    for (size_t i = 0; i < n_bytes; i++) {
        crc ^= byte_ptr[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

typedef enum: uint8_t {
    P2P_PACK_DATA,      // RUDP
    P2P_PACK_ACK,       // no RUDP (natpunching module)
    P2P_PACK_PING,      // no RUDP (integrety doesnt matter)
    P2P_PACK_PONG,      // no RUDP (integrety doesnt matter)
    P2P_PACK_PUNCH,     // no RUDP (natpunching module)
    P2P_PACK_GOSSIP,    // no RUDP (integrety doesnt matter)
    P2P_PACK_HELLO,     // RUDP
    P2P_PACK_REJECT,    // RUDP
    P2P_PACK_ACCEPT,    // RUDP
    P2P_PACK_STATE,     // no RUDP
    P2P_PACK_RACK       // RUDP ACK
} udp_pack_type;

bool udp_is_RUDP_req(udp_pack_type type){
    switch (type) {
        case P2P_PACK_DATA:      return true;
        case P2P_PACK_HELLO:     return true;
        case P2P_PACK_REJECT:    return true;
        case P2P_PACK_ACCEPT:    return true;
        case P2P_PACK_RACK:      return true;
        default:                 return false;
    }
}

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t chsum;
    uint32_t d_size;
    uint32_t h_from; // from who
    uint32_t h_to;   // to whom
    uint8_t  packtype;
    uint8_t  data[];
} udp_packet;
#pragma pack(pop)

udp_packet *udp_make_pack(
    uint32_t  sequence_n,
    uint32_t  hash_from,
    uint32_t  hash_to,
    udp_pack_type type,

    void     *payload,
    size_t    payload_size
){
    size_t total_size = sizeof(udp_packet) + payload_size;
    udp_packet *pack = malloc(total_size);
    if (!pack) return NULL;
    memset(pack, 0, total_size);

    pack->chsum  = 0;
    pack->seq    = htonl(sequence_n);
    pack->magic  = htonl(P2P_UDP_MAGIC);
    pack->d_size = htonl(payload_size);
    pack->h_from = htonl(hash_from);
    pack->h_to   = htonl(hash_to);
    pack->packtype = (uint8_t)(type);
    
    if (payload)
        memcpy(pack->data, payload, payload_size);

    pack->chsum  = htonl(crc32(pack, total_size));

    return pack;
}

udp_packet *udp_copy_pack(udp_packet *pk, bool apply_ntoh){
    udp_packet *out = malloc(sizeof(udp_packet) + (apply_ntoh ? ntohl(pk->d_size): pk->d_size));
    out->chsum    = apply_ntoh ? ntohl(pk->chsum): pk->chsum;
    out->magic    = apply_ntoh ? ntohl(pk->magic): pk->magic;
    out->seq      = apply_ntoh ? ntohl(pk->seq): pk->seq;
    out->d_size   = apply_ntoh ? ntohl(pk->d_size): pk->d_size;
    out->h_from   = apply_ntoh ? ntohl(pk->h_from): pk->h_from;
    out->h_to     = apply_ntoh ? ntohl(pk->h_to): pk->h_to;
    out->packtype = pk->packtype;
    if (out->d_size != 0) memcpy(out->data, pk->data, out->d_size);

    return out;
}

udp_packet *retranslate_udp(udp_packet *pk, int to_net){
    udp_packet *out = udp_copy_pack(pk, to_net == 0);
    if (to_net){
        out->chsum    = htonl(pk->chsum);
        out->magic    = htonl(pk->magic);
        out->seq      = htonl(pk->seq);
        out->d_size   = htonl(pk->d_size);
        out->h_from   = htonl(pk->h_from);
        out->h_to     = htonl(pk->h_to);
        out->packtype = pk->packtype;
    }
    return out;
}

ssize_t udp_pack_send(
    p2p_udp    *udp,
    udp_packet *pack,
    nnet_fd     to
){
    size_t total_size = sizeof(udp_packet) + ntohl(pack->d_size);
    ssize_t sent = udp_send(udp, pack, total_size, to);
    
    free(pack);
    return sent;
}

udp_packet *udp_pack_recv(
    p2p_udp  *udp,
    nnet_fd  *from
){
    uint8_t buffer[2048] = {0};
    ssize_t recved = udp_recv(udp, buffer, sizeof(buffer), from);
    
    if (recved < sizeof(udp_packet)) {
        return NULL;
    }

    udp_packet *recv_pkt = (udp_packet *)buffer;

    if (ntohl(recv_pkt->magic) != P2P_UDP_MAGIC) {
        return NULL; 
    }

    uint32_t received_chsum = ntohl(recv_pkt->chsum);
    recv_pkt->chsum = 0;
    uint32_t calculated_chsum = crc32(buffer, recved);
    
    if (received_chsum != calculated_chsum) {
        SLOG_ERROR("[udpproto][recv error] checksum mismatch");
        return NULL;
    }

    udp_packet *final_pkt = malloc(recved);
    if (final_pkt) {
        memcpy(final_pkt, buffer, recved);
        final_pkt->chsum = htonl(received_chsum); 
    }

    final_pkt->seq    = ntohl(final_pkt->seq);
    final_pkt->d_size = ntohl(final_pkt->d_size);
    final_pkt->h_from = ntohl(final_pkt->h_from);
    final_pkt->h_to   = ntohl(final_pkt->h_to);
    return final_pkt;
}

#endif
#define P2P_UDP_PROTO