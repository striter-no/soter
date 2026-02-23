#include <netcore/udp_proto.h>
#include <natpunch/punch.h>
#include <p2p/nat_helper.h>

#include <base/prot_table.h>
#include <base/prot_array.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/eventfd.h>

#include <crypto/system.h>
#include <crypto/handshake.h>

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
    int        status_evfd;

    soter_session_keys sk;
    unsigned char pubkey[SOTER_PUBKEY_BYTES];
} p2p_peer;

typedef struct {
    nat_type    nat_type;
    p2p_udp    *p_client;
    prot_table  peers;

    prot_array    pushing_peers;
    soter_keypair kp;
} p2p_peers_system;

uint32_t get_timestump(){
    return time(NULL);
}

void p2p_peer_changestat(
    p2p_peer *peer, p2p_status new_status
){
    peer->status = new_status;
    write(peer->status_evfd, &(uint64_t){1}, 8);
}

int p2p_psystem_init(
    p2p_peers_system *sys,
    p2p_udp          *p_client,
    soter_keypair     kp
){
    sys->pushing_peers = prot_array_create(sizeof(p2p_peer));
    sys->peers         = prot_table_create(sizeof(uint32_t), sizeof(p2p_peer), DYN_OWN_BOTH);

    sys->p_client = p_client;
    sys->kp       = kp;
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


int p2p_peer_handshake(
    p2p_peers_system *sys,
    uint32_t peer_uid,
    unsigned char peer_pubk[SOTER_PUBKEY_BYTES],
    soter_session_keys *sk
){
    if (peer_uid < sys->p_client->UID){ // server
        if (0 > soter_handshake_server(
            peer_pubk,
            &sys->kp,
            sk
        )){
            SLOG_ERROR("[p2p][punchn][serv] failed to perform handshake");
            return -1;
        }
        SLOG_DEBUG("[p2p] handshaken as server, peer: %02x%02x%02x%02x%02x%02x%02x%02x", 
            peer_pubk[0], peer_pubk[1], peer_pubk[2], peer_pubk[3],
            peer_pubk[4], peer_pubk[5], peer_pubk[6], peer_pubk[7]
        );
    } else { // client
        if (0 > soter_handshake_client(
            peer_pubk,
            &sys->kp,
            sk
        )){
            SLOG_ERROR("[p2p][punchn][cli] failed to perform handshake");
            return -1;
        }
        SLOG_DEBUG("[p2p] handshaken as server, peer: %02x%02x%02x%02x%02x%02x%02x%02x", 
            peer_pubk[0], peer_pubk[1], peer_pubk[2], peer_pubk[3],
            peer_pubk[4], peer_pubk[5], peer_pubk[6], peer_pubk[7]
        );
    }
    return 0;
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

// * send punch message
int p2p_psystem_punchnat(
    p2p_peers_system *sys,
    uint32_t          peer_uid,
    naddr_t           peer_addr,
    unsigned char    *peer_pubk,
    int              *evfd,
    int               packs_n,
    soter_session_keys *sk
){
    p2p_peer peer;
    peer.fd = (nnet_fd){0};
    for (int i = 0; i < packs_n; i++){
        p2pnp_udp_punch(sys->p_client, peer_addr, sys->p_client->UID, &peer.fd);
        sleep(1);
    }

    peer.peer_id   = peer_uid;
    peer.last_seen = 0;
    peer.status    = P2P_STAT_PUNCHING;
    peer.last_seq  = 0;
    peer.status_evfd = evfd == NULL? eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK): *evfd;
    memcpy(peer.pubkey, peer_pubk, SOTER_PUBKEY_BYTES);
    if (!sk){
        SLOG_DEBUG("[punchnat] handshaking");
        if (0 > p2p_peer_handshake(sys, peer_uid, peer_pubk, &peer.sk)){
            SLOG_ERROR("[punchnat] handshake failed");
            return -1;
        }
    } else {
        peer.sk = *sk;
    }

    // add to pending
    prot_array_push(&sys->pushing_peers, &peer);

    return peer.status_evfd;
}

int p2p_peer_register(
    p2p_peers_system *sys,
    naddr_t           peer_addr,
    uint32_t          peer_uid,
    p2p_status        status,
    int               efd,

    const unsigned char *peer_pubk,
    soter_session_keys  *out_sk
){
    if (!sys) return -1;

    p2p_peer peer;
    peer.peer_id   = peer_uid;
    peer.last_seen = 0;
    peer.status    = status;
    peer.last_seq  = 0;
    peer.fd        = netfdq(peer_addr);
    peer.status_evfd = efd == -1? eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK): efd;
    memcpy(peer.pubkey, peer_pubk, SOTER_PUBKEY_BYTES);
    
    if (peer_uid < sys->p_client->UID){ // server
        if (0 > soter_handshake_server(
            peer_pubk,
            &sys->kp,
            &peer.sk
        )){
            SLOG_ERROR("[p2p][reg][serv] failed to perform handshake");
            return -1;
        }

        SLOG_DEBUG("[p2p][reg] handshaken as server, peer: %02x%02x%02x%02x%02x%02x%02x%02x", 
            peer_pubk[0], peer_pubk[1], peer_pubk[2], peer_pubk[3],
            peer_pubk[4], peer_pubk[5], peer_pubk[6], peer_pubk[7]
        );
    } else { // client
        if (0 > soter_handshake_client(
            peer_pubk,
            &sys->kp,
            &peer.sk
        )){
            SLOG_ERROR("[p2p][reg][cli] failed to perform handshake");
            return -1;
        }

        SLOG_DEBUG("[p2p][reg] handshaken as client, peer: %02x%02x%02x%02x%02x%02x%02x%02x", 
            peer_pubk[0], peer_pubk[1], peer_pubk[2], peer_pubk[3],
            peer_pubk[4], peer_pubk[5], peer_pubk[6], peer_pubk[7]
        );
    }

    *out_sk = peer.sk;
    // add to pending
    if (status == P2P_STAT_PUNCHING){
        prot_array_push(&sys->pushing_peers, &peer);
    } else { 
        prot_table_set(&sys->peers, &peer_uid, &peer);
    }
    return peer.status_evfd;
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
            
            copy.status_evfd = el->status_evfd;
            copy.last_seen   = get_timestump();
            copy.peer_id     = el->peer_id;
            copy.fd          = el->fd;
            copy.last_seq    = 0;
            copy.sk          = el->sk;
            memcpy(copy.pubkey, el->pubkey, SOTER_PUBKEY_BYTES);
            
            p2p_peer_changestat(&copy, P2P_STAT_ACTIVE);
            SLOG_DEBUG("[disp][p2p] changed stat for %u to ACTIVE", copy.status_evfd);
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
        SLOG_WARNING("[activity] failed, peer %u is NULL", peer_id);
        return -1;
    }

    bool alive = peer->status == P2P_STAT_ACTIVE;
    uint32_t dt = get_timestump() - peer->last_seen;
    
    if (dt >= P2P_PEER_DEAD_DT){
        if (is_changed) *is_changed = alive ? true: false;
        if (alive) {
            peer->status = P2P_STAT_TIMEOUT;
        }

        return 0;
    }

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
