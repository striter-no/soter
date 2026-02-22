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
} p2p_state_peer;
#pragma pack(pop)

p2p_state_peer p2p_state_info2peer(naddr_t addr, uint32_t uid){
    return (p2p_state_peer){
        .ip   = naddr_to_uint32(addr),
        .port = addr.ip.v4.port,
        .uid  = uid
    };
}

void p2p_state_peer2info(p2p_state_peer peer, naddr_t *addr, uint32_t *uid){
    *addr = naddr_from_uint32(peer.ip, peer.port);
    *uid  = peer.uid;
}

int evfd_wait(int evfd, int events, int timeout){
    struct pollfd fd[1] = {{.fd = evfd, .events = events}};
    int r = poll(fd, 1, timeout);
    if (r <= 0) return r;

    uint64_t u;
    read(evfd, &u, sizeof(u));
    return r;
}

int p2p_udp_stateserv(
    p2p_listener  *list,
    naddr_t   state_addr,
    naddr_t  *other_addr,
    uint32_t *other_uid
){
    struct timespec dt = {
        .tv_nsec = 0,
        .tv_sec  = STATESERV_DT
    };

    p2p_udp *client = list->p_client;
    nnet_fd sfd = netfdq(state_addr);
    while (true){
        // printf("[p2pnp][sserv] sending packet...\n");
        udp_packet *pack = udp_make_pack(0, client->UID, 0, P2P_PACK_STATE, &client->UID, sizeof(client->UID));
        udp_pack_send(client, pack, sfd);

        if (0 >= evfd_wait(list->pack_eventfd, POLLIN, 100)) continue;
        // printf("[p2pnp][sserv] got incoming packet...\n");
        udp_packet *pkt = NULL;
        if (prot_queue_pop(&list->packets, (void**)&pkt) != 0){
            perror("[p2pnp][sserv] failed to get packet, something went wrong\n");
            continue;
        }

        if (pkt->packtype != P2P_PACK_STATE) {
            printf("[p2pnp][sserv] ignoring packtype %u\n", pkt->packtype);
            goto end;
        }

        p2p_state_peer state;

        if (pkt->d_size != sizeof(state)){
            if (pkt->d_size != 1) printf("[p2pnp][sserv] ignoring corrupted packet\n");
            // otherwise it is "0", answer when no peers available
        } else {
            memcpy(&state, pkt->data, sizeof(state));
            p2p_state_peer2info(state, other_addr, other_uid);
            free(pkt);
            return 0;
        }

        nanosleep(&dt, NULL);
end:
        free(pkt);
    }
    return 0;
}

#endif
#define P2P_NATPUNCH_SSERVER