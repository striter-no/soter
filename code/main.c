#include <p2p/dispatcher.h>
#include <p2p/peers.h>
#include <natpunch/sserver.h>
#include <p2p/reliable_udp/system.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

void ask_conn(
    naddr_t *addr, uint32_t *uid
){
    char ip[INET_ADDRSTRLEN]; 
    unsigned port;
    printf("[main] ip:port:uid > "); fflush(stdin);
    scanf("%[^:]:%u:%u", ip, &port, uid);

    *addr = naddr_make4(nipv4(ip, port));
}

unsigned short rnd_port(){
    unsigned port = ((uint16_t)rand()) % UINT16_MAX;
    port = port <= 1024 ? (port + rand() % 1024): port;
    return port;
}

uint32_t rnd_uid(){
    return rand() % UINT32_MAX;
}

static void after_connection(
    p2p_rudp_system *rudp,
    p2p_udp         *client,
    p2p_peer        *punched_peer
);

int main(void){
    srand(time(NULL));
    
    p2p_udp client; 
    udp_create(&client, rnd_uid());
    
    naddr_t servers[] = {
        naddr_domain("stun.l.google.com", 19302),
        naddr_domain("stunserver2025.stunprotocol.org", 3478),
        naddr_make4(nipv4("89.19.215.10", 9000))
    };

    gossip_system gossipsyst;
    gossip_system_init(&gossipsyst, client.UID);

    naddr_t other_ip; uint32_t other_uid;
    p2p_peers_system psyst;
    p2p_psystem_init(&psyst, &client);
    if (0 > p2p_psystem_gnattype(&psyst, servers[0], servers[1], rnd_port()))
        return -1;
    
    printf("[main] my NAT type: %s\n", strnattype(psyst.nat_type));
    printf("[main] my conn: %s:%u:%u\n", client.addr.ip.v4.ip, client.addr.ip.v4.port, client.UID);

    p2p_listener listener;
    p2p_listener_init(&listener, &client);
    p2p_listener_start(&listener);

    int statfd;
    p2p_udp_stateserv(&listener, servers[2], &other_ip, &other_uid);
    statfd = p2p_peer_register(&psyst, other_ip, other_uid, P2P_STAT_PUNCHING, -1);
    printf("[main] state got: %s:%u:%u\n", other_ip.ip.v4.ip, other_ip.ip.v4.port, other_uid);

    p2p_rudp_system rudp;
    p2p_rudp_systeminit(&rudp, &client, &psyst);
    p2p_rudp_systemrun(&rudp);

    p2p_dispatcher dispatcher;
    p2p_dispatcher_init(&dispatcher, &listener, &psyst, &gossipsyst, &rudp.disp);
    p2p_dispatcher_start(&dispatcher);
    
    printf("[main] sending punch packet\n");
    p2p_psystem_punchnat(&psyst, other_uid, other_ip, &statfd, 1);
    printf("[main] waiting fd %u\n", statfd);
    
    evfd_wait(statfd, POLLIN, -1);

    p2p_peer *act_peer = p2p_psystem_peer(&psyst, other_uid);
    if (!act_peer) {
        printf("[main] failed to get peer\n");
        return -1;
    }
    printf("[main] got connected active peer: %u\n", act_peer->peer_id);

    // --- after connection

    after_connection(&rudp, &client, act_peer);

    // --- end of session

    gossip_system_end(&gossipsyst);
    p2p_rudp_systemend(&rudp);
    p2p_psystem_end(&psyst);
    p2p_dispatcher_end(&dispatcher);
    p2p_listener_end(&listener);

    udp_close(client);
}

static void after_connection(
    p2p_rudp_system *rudp,
    p2p_udp         *client,
    p2p_peer        *peer
){
    udp_packet *pack = udp_make_pack(
        0, client->UID, peer->peer_id, P2P_PACK_DATA, "Hello", 5
    );
    printf("[com] sending RUDP pack for %u\n", peer->peer_id);
    if (0 > p2p_rudp_newpack(rudp, pack, peer->peer_id)){
        printf("[com] failed to send pack\n");
        free(pack);
        return;
    }

    printf("[com] waiting channel for %u\n", peer->peer_id);
    p2p_rudpdisp_waitchan(&rudp->disp, peer->peer_id);
    printf("[com] got channel\n");

    p2p_rudp_channel *chan;
    if (0 > p2p_rudpdisp_getchan(&rudp->disp, &chan, peer->peer_id)){
        printf("Failed to get channel\n");
        return;
    }

    printf("[com] awaiting packet\n");
    udp_packet *out = NULL;
    if (0 > p2p_rudp_chan_awaitpack(chan, &out, -1)){
        printf("Failed to wait a packet\n");
        return;
    }
    printf("[com] awaited\n");

    if (!out) {
        printf("Failed to get packet\n");
        return;
    }
    printf("size: %u\n", out->d_size);
    printf("From peer: %u %.*s\n", out->d_size, out->d_size, out->data);
    free(out);
    sleep(10);
}