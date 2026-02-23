#include <netcore/udp_proto.h>
#include <p2p/listener.h>
#include <sys/poll.h>
#include <time.h>

#ifndef STATESERV_DT
#define STATESERV_DT 2
#endif

#ifndef P2P_NATPUNCH_SSERVER

#pragma pack(push, 1)
typedef struct {
    uint32_t ip;
    uint16_t port;
    uint32_t uid;
    unsigned char pubkey[SOTER_PUBKEY_BYTES];
} p2p_state_peer;
#pragma pack(pop)

p2p_state_peer p2p_state_info2peer(
    naddr_t addr, 
    uint32_t uid,
    unsigned char pubkey[SOTER_PUBKEY_BYTES]
){
    p2p_state_peer state = {
        .ip   = naddr_to_uint32(addr),
        .port = addr.ip.v4.port,
        .uid  = uid
    };
    memcpy(state.pubkey, pubkey, SOTER_PUBKEY_BYTES);

    return state;
}

void p2p_state_peer2info(
    p2p_state_peer peer, 
    naddr_t *addr, 
    uint32_t *uid, 
    unsigned char pubkey[SOTER_PUBKEY_BYTES]
){
    *addr = naddr_from_uint32(peer.ip, peer.port);
    *uid  = peer.uid;
    if (pubkey)
        memcpy(pubkey, peer.pubkey, SOTER_PUBKEY_BYTES);
}


// int p2p_udp_stateserv(
//     p2p_listener  *list,
//     naddr_t   state_addr,
//     naddr_t  *other_addr,
//     uint32_t *other_uid,
//     unsigned char other_pubkey[SOTER_PUBKEY_BYTES]
// ){
//     struct timespec dt = {
//         .tv_nsec = 0,
//         .tv_sec  = STATESERV_DT
//     };

//     p2p_udp *client = list->p_client;
//     nnet_fd sfd = netfdq(state_addr);
//     while (true){
//         udp_packet *pack = udp_make_pack(0, client->UID, 0, P2P_PACK_STATE, &client->UID, sizeof(client->UID));
//         udp_pack_send(client, pack, sfd);

//         if (0 >= evfd_wait(list->pack_eventfd, POLLIN, 100)) continue;
//         udp_packet *pkt = NULL;
//         if (prot_queue_pop(&list->packets, (void**)&pkt) != 0){
//             SLOG_ERROR("[p2pnp][sserv] failed to get packet, something went wrong");
//             continue;
//         }

//         if (pkt->packtype != P2P_PACK_STATE) {
//             SLOG_WARNING("[p2pnp][sserv] ignoring packtype %u", pkt->packtype);
//             goto end;
//         }

//         p2p_state_peer state;

//         if (pkt->d_size != sizeof(state)){
//             if (pkt->d_size != 1) SLOG_WARNING("[p2pnp][sserv] ignoring corrupted packet");
//             // otherwise it is "0", answer when no peers available
//         } else {
//             memcpy(&state, pkt->data, sizeof(state));
//             p2p_state_peer2info(state, other_addr, other_uid, other_pubkey);
//             free(pkt);
//             return 0;
//         }

//         nanosleep(&dt, NULL);
// end:
//         free(pkt);
//     }
//     return 0;
// }

#endif
#define P2P_NATPUNCH_SSERVER