#include <netcore/udp_proto.h>
#include "stun.h"

#ifndef P2P_NATPUNCH_PUNCH

int p2pnp_udp_setup(
    p2p_udp *client
){
    srand(time(NULL));
    return udp_bind(client,
        naddr_make4(nipv4("0.0.0.0", 1024+rand()%60000))
    );
}

udp_packet *p2pnp_punch_pack(
    uint32_t hash_from,
    uint32_t hash_to,
    naddr_t  my_addr
){
    return udp_make_pack(0, hash_from, hash_to, P2P_PACK_PUNCH, &my_addr, sizeof(my_addr));
}

udp_packet *p2pnp_ack_pack(
    uint32_t hash_from,
    uint32_t hash_to,
    naddr_t  my_addr
){
    return udp_make_pack(0, hash_from, hash_to, P2P_PACK_ACK, &my_addr, sizeof(my_addr));
}

udp_packet *p2pnp_ping_pack(
    uint32_t hash_from,
    uint32_t hash_to,
    naddr_t  my_addr
){
    return udp_make_pack(0, hash_from, hash_to, P2P_PACK_PING, &my_addr, sizeof(my_addr));
}

udp_packet *p2pnp_pong_pack(
    uint32_t hash_from,
    uint32_t hash_to,
    naddr_t  my_addr
){
    return udp_make_pack(0, hash_from, hash_to, P2P_PACK_PONG, &my_addr, sizeof(my_addr));
}

// -- sending

int p2pnp_udp_ack(
    p2p_udp        *client,
    naddr_t         other_ip,
    uint32_t        other_uid,

    nnet_fd *remote_fd
){
    *remote_fd = netfdq(other_ip);
    udp_packet *ack = p2pnp_ack_pack(client->UID, other_uid, client->addr);
    return 0 < udp_pack_send(client, ack, *remote_fd);
}

int p2pnp_udp_punch(
    p2p_udp  *client,
    naddr_t   other_ip,
    uint32_t  other_uid,

    nnet_fd  *remote_fd
){
    *remote_fd = netfdq(other_ip);
    udp_packet *punch = p2pnp_punch_pack(client->UID, other_uid, client->addr);
    return 0 < udp_pack_send(client, punch, *remote_fd);
}

int p2pnp_udp_ping(
    p2p_udp  *client,
    naddr_t   other_ip,
    uint32_t  other_uid,

    nnet_fd  *remote_fd
){
    *remote_fd = netfdq(other_ip);
    udp_packet *ping = p2pnp_ping_pack(client->UID, other_uid, client->addr);
    return 0 < udp_pack_send(client, ping, *remote_fd);
}

int p2pnp_udp_pong(
    p2p_udp  *client,
    naddr_t   other_ip,
    uint32_t  other_uid,

    nnet_fd  *remote_fd
){
    *remote_fd = netfdq(other_ip);
    udp_packet *ping = p2pnp_pong_pack(client->UID, other_uid, client->addr);
    return 0 < udp_pack_send(client, ping, *remote_fd);
}

#endif
#define P2P_NATPUNCH_PUNCH