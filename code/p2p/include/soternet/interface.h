#include <logging/logger.h>
#include <p2p/dispatcher.h>
#include <p2p/peers.h>
#include <natpunch/sserver.h>
#include <p2p/reliable_udp/system.h>
#include <stdint.h>
#include <unistd.h>

#ifndef SOTER_NET_INTERFACE

static unsigned short __rnd_port(){
    unsigned port = ((uint16_t)rand()) % UINT16_MAX;
    port = port <= 1024 ? (port + rand() % 1024): port;
    return port;
}

static uint32_t __rnd_uid(){
    return rand() % UINT32_MAX;
}

typedef struct {
    soter_keypair      kp;

    p2p_udp          net_client;

    gossip_system    gsyst;
    p2p_peers_system psyst;
    p2p_rudp_system  rudp;

    p2p_listener     listener;
    p2p_dispatcher   dispatcher;
} soter;

int soter_client(
    soter *client,
    naddr_t      state_serv,
    naddr_t      stun1,
    naddr_t      stun2,
    logger_type  min_level
){
    srand(time(NULL));
    SOTER_LOGER = logger_init(STDERR_FILENO);
    logger_set_level(&SOTER_LOGER, min_level);

    if (0 > udp_create(&client->net_client, __rnd_uid())){
        SLOG_ERROR("soter_client: failed to create network client");
        return -1;
    }

    client->kp = soter_keypair_make();

    if (0 > p2p_psystem_init(&client->psyst, &client->net_client, client->kp)){
        SLOG_ERROR("soter_client: failed to create peer system");
        return -1;
    }

    p2p_psystem_gnattype(&client->psyst, stun1, stun2, __rnd_port());

    if (0 > gossip_system_init(&client->gsyst, &client->psyst, client->net_client.UID)){
        SLOG_ERROR("soter_client: failed to create gossip system");
        return -1;
    }

    if (0 > p2p_listener_init(&client->listener, &client->net_client)){
        SLOG_ERROR("soter_client: failed to create peer system");
        return -1;
    }

    if (0 > p2p_listener_start(&client->listener)){
        SLOG_ERROR("soter_client: failed init listener");
        return -1;
    }

    if (0 > p2p_rudp_systeminit(&client->rudp, &client->net_client, &client->psyst)){
        SLOG_ERROR("soter_client: RUDP system initialisation failed");
        return -1;
    }

    if (0 > p2p_rudp_systemrun(&client->rudp)){
        SLOG_ERROR("soter_client: RUDP system failed to run");
        return -1;
    }

    if (0 > p2p_dispatcher_init(&client->dispatcher, &client->listener, 
                                &client->psyst,      &client->gsyst, 
                                &client->rudp.disp,   state_serv)){
        SLOG_ERROR("soter_client: failed to init dispatcher");
        return -1;
    }

    p2p_dispatcher_start(&client->dispatcher);

    return 0;
}

int soter_wait_state(
    soter *client,
    int          timeout
){
    int r = evfd_wait(client->dispatcher.sstate_evfd, POLLIN, timeout);
    if (r <= 0) return r;

    return 1;
}

p2p_state *soter_get_state(
    soter *client,
    size_t       inx
){
    return prot_array_at(&client->dispatcher.state_evfds, inx);
}

int soter_stun_connect(
    soter *client,
    naddr_t      stun
){
    if (0 > p2p_psystem_stunperform(&client->psyst, stun, __rnd_port())) {
        SLOG_ERROR("soter_client: failed to request STUN");
        return -1;
    }

    return 0;
}

p2p_peer *soter_peer(
    soter *client,
    uint32_t     UID
){
    return p2p_psystem_peer(&client->psyst, UID);
}

int soter_p2p_connect(
    soter *client,
    uint32_t     other_UID,
    naddr_t      other_addr,
    unsigned char other_pubkey[SOTER_PUBKEY_BYTES],
    int         *evfd
){
    if (0 > p2p_psystem_punchnat(&client->psyst, other_UID, other_addr, other_pubkey, evfd, 1)){
        SLOG_ERROR("soter_client: failed to request NAT punch");
        return -1;
    }

    return 0;
}

void soter_end(
    soter *client
){
    SLOG_DEBUG("ending listener");
    p2p_listener_end(&client->listener);

    SLOG_DEBUG("ending dispatcher");
    p2p_dispatcher_end(&client->dispatcher);

    SLOG_DEBUG("ending RUDP");
    p2p_rudp_systemend(&client->rudp);

    SLOG_DEBUG("ending peer system");
    p2p_psystem_end(&client->psyst);

    SLOG_DEBUG("ending gossip system");
    gossip_system_end(&client->gsyst);

    SLOG_DEBUG("ending net client");
    udp_close(client->net_client);
    logger_stop(&SOTER_LOGER);
}

// -- messages

int soter_acquire_chan(
    soter      *client,
    p2p_rudp_channel **chan,
    uint32_t          UID,
    int               timeout
){
    int r = p2p_rudpdisp_waitchan(&client->rudp.disp, UID, timeout);
    if (r <= 0) {
        SLOG_WARNING("soter_client: no channels available for %u", UID);
        return r;
    }

    if (0 > p2p_rudpdisp_getchan(&client->rudp.disp, chan, UID)){
        SLOG_ERROR("soter_client: Failed to get channel");
        return -1;
    }

    return 0;
}

int soter_wait_pack(
    p2p_rudp_channel *chan,
    int               timeout
){
    if (0 > p2p_rudp_chan_awaitpack(chan, -1)){
        SLOG_ERROR("soter_client: Failed to wait a packet");
        return -1;
    }

    return 0;
}

int soter_send(
    soter *client,
    uint32_t     other_UID,
    void        *data,
    size_t       size
){
    udp_packet *pack = udp_make_pack(
        0, 
        client->net_client.UID,  
        other_UID,
        P2P_PACK_DATA, 
        data, 
        size
    );

    if (0 > p2p_rudp_newpack(&client->rudp, pack, other_UID)){
        SLOG_ERROR("soter_client: Failed to send pack");
        free(pack);
        return -1;
    }

    SLOG_INFO("soter: sending %zu bytes as raw data, %zu in total", size, sizeof(udp_packet) + size);
    return 0;
}


int soter_recv(
    p2p_rudp_channel  *chan,
    void             **data, 
    size_t            *size
){
    udp_packet *out = NULL;
    p2p_rudp_chan_getpack(chan, &out);
    if (!out){
        SLOG_ERROR("soter_client: Failed to get packet");
        return -1;
    }

    *data = malloc(out->d_size);
    *size = out->d_size;
    memcpy(*data, out->data, out->d_size);

    free(out);
    return 0;
}

#endif
#define SOTER_NET_INTERFACE