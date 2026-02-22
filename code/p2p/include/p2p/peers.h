#include <netcore/udp_proto.h>
#include <natpunch/punch.h>
#include <p2p/nat_helper.h>

#include <base/prot_table.h>
#include <base/prot_array.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef P2P_PEER_DEAD_DT
#define P2P_PEER_DEAD_DT 5
#endif

#ifndef P2P_PEER_PING_DT
#define P2P_PEER_PING_DT 2
#endif

#ifndef P2P_PEER

typedef enum {
    P2P_STAT_PUNCHING = 0,
    P2P_STAT_ACK      = 1,
    P2P_STAT_ACTIVE   = 2,
    P2P_STAT_TIMEOUT  = 3
} p2p_status;

typedef struct {
    uint32_t peer_id;
    nnet_fd  fd;

    uint32_t   last_seq;
    uint32_t   last_seen;
    p2p_status status;
} p2p_peer;

typedef struct {
    nat_type    nat_type;
    p2p_udp    *p_client;
    prot_table  peers;

    prot_array  pushing_peers;
} p2p_peers_system;

uint32_t get_timestump(){
    return time(NULL);
}

int p2p_psystem_init(
    p2p_peers_system *sys,
    p2p_udp          *p_client
){
    sys->pushing_peers = prot_array_create(sizeof(p2p_peer));
    sys->peers         = prot_table_create(sizeof(uint32_t), sizeof(p2p_peer), DYN_OWN_BOTH);

    sys->p_client = p_client;
    return 0;
}

int p2p_psystem_gnattype(
    p2p_peers_system *sys,
    naddr_t           stun1,
    naddr_t           stun2,
    unsigned short    port
){
    sys->nat_type = get_nat_type(sys->p_client, stun1, stun2, port);
    return 0;
}

int p2p_psystem_stunperform(
    p2p_peers_system *sys,
    naddr_t           stun,
    unsigned short    port
){
    return p2pnp_udp_stun(sys->p_client, stun);
}

// * send punch message
int p2p_psystem_punchnat(
    p2p_peers_system *sys,
    uint32_t          peer_uid,
    naddr_t           peer_addr,
    p2p_peer         *peer,
    int               packs_n
){
    peer->fd = (nnet_fd){0};
    for (int i = 0; i < packs_n; i++){
        p2pnp_udp_punch(sys->p_client, peer_addr, sys->p_client->UID, &peer->fd);
        sleep(1);
    }

    peer->peer_id   = peer_uid;
    peer->last_seen = 0;
    peer->status    = P2P_STAT_PUNCHING;
    peer->last_seq  = 0;

    // add to pending
    prot_array_push(&sys->pushing_peers, peer);

    return 0;
}

int p2p_peer_register(
    p2p_peers_system *sys,
    naddr_t           peer_addr,
    uint32_t          peer_uid,
    p2p_status        status
){
    if (!sys) return -1;

    p2p_peer peer;
    peer.peer_id   = peer_uid;
    peer.last_seen = 0;
    peer.status    = status;
    peer.last_seq  = 0;
    peer.fd        = netfdq(peer_addr);

    // add to pending
    if (status == P2P_STAT_PUNCHING)
        return prot_array_push(&sys->pushing_peers, &peer);
    else 
        return prot_table_set(&sys->peers, &peer_uid, &peer);
}

p2p_peer *p2p_psystem_Ppeer(
    p2p_peers_system *sys,
    uint32_t          peer_id
){
    prot_array_lock(&sys->pushing_peers);
    for (size_t i = 0; i < sys->pushing_peers.array.len; i++){
        p2p_peer* el = prot_array_at(&sys->pushing_peers, i);
        if (el->peer_id == peer_id){
            prot_array_unlock(&sys->pushing_peers);
            return el;
        }
    }
    prot_array_unlock(&sys->pushing_peers);
    return NULL;
}

bool p2p_psystem_Ppeer_check(
    p2p_peers_system *sys,
    uint32_t          peer_id,
    p2p_status        status
){
    p2p_peer *peer = p2p_psystem_Ppeer(sys, peer_id);
    if (!peer) return false;
    return peer->status == status;
}

p2p_peer *p2p_psystem_peer(
    p2p_peers_system *sys,
    uint32_t          peer_id
){
    return prot_table_get(&sys->peers, &peer_id);
}

bool p2p_psystem_isalive(
    p2p_peers_system *sys,
    uint32_t          peer_id
){
    void *peer = prot_table_get(&sys->peers, &peer_id);
    return peer != NULL;
}

int p2p_psystem_makealive(
    p2p_peers_system *sys,
    uint32_t          peer_id
){
    if (p2p_psystem_isalive(sys, peer_id)) 
        return -1;

    prot_array_lock(&sys->pushing_peers);

    for (size_t i = 0; i < sys->pushing_peers.array.len; i++){
        p2p_peer* el = prot_array_at(&sys->pushing_peers, i);
        if (el->peer_id == peer_id){
            p2p_peer copy;
            prot_array_remove(&sys->pushing_peers, i);
            
            copy.last_seen = get_timestump();
            copy.status    = P2P_STAT_ACTIVE;
            copy.peer_id   = el->peer_id;
            copy.fd        = el->fd;
            copy.last_seq  = 0;
            prot_table_set(&sys->peers, &peer_id, &copy);

            prot_array_unlock(&sys->pushing_peers);
            return 0;
        }
    }

    prot_array_unlock(&sys->pushing_peers);
    return -1;
}

int p2p_psystem_activity(
    p2p_peers_system *sys,
    uint32_t          peer_id,
    bool             *is_changed
){
    p2p_peer *peer = p2p_psystem_peer(sys, peer_id);
    if (peer == NULL) {
        printf("[activity] failed, peer %u is NULL\n", peer_id);
        return -1;
    }

    bool alive = peer->status == P2P_STAT_ACTIVE;
    uint32_t dt = get_timestump() - peer->last_seen;
    
    printf("... ls/dt: %u/%u\n", peer->last_seen, dt);
    if (dt >= P2P_PEER_DEAD_DT){
        if (is_changed) *is_changed = alive ? true: false;
        if (alive) {
            printf("... changing status to timeout\n");
            peer->status = P2P_STAT_TIMEOUT;
        }

        return 0;
    }

    // peer->last_seen = get_timestump();
    if (is_changed) *is_changed = false;
    return 0;
}

int p2p_psystem_pingpeer(
    p2p_peers_system *sys,
    uint32_t          peer_id
){
    return 0;
}

int p2p_psystem_pong(
    p2p_peers_system *sys,
    uint32_t          peer_id
){
    return 0;
}

void p2p_psystem_end(
    p2p_peers_system *sys
){
    prot_array_end(&sys->pushing_peers);
    prot_table_end(&sys->peers);
}

#endif
#define P2P_PEER
