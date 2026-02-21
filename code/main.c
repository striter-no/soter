#include <p2p/dispatcher.h>
#include <p2p/peers.h>
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

int main(void){
    srand(time(NULL));
    
    p2p_udp client; 
    udp_create(&client, rnd_uid());
    
    naddr_t stuns[] = {
        naddr_domain("stun.l.google.com", 19302),
        naddr_domain("stunserver2025.stunprotocol.org", 3478)
    };

    gossip_system gossipsyst;
    gossip_system_init(&gossipsyst, client.UID);

    p2p_peers_system psyst;
    p2p_psystem_init(&psyst, &client);
    p2p_psystem_stunperform(&psyst, stuns[0], stuns[1], rnd_port(), true);
    printf("[main] my conn: %s:%u:%u\n", client.addr.ip.v4.ip, client.addr.ip.v4.port, client.UID);

    naddr_t other_ip; uint32_t other_uid;
    ask_conn(&other_ip, &other_uid);

    p2p_listener listener;
    p2p_listener_init(&listener, &client);
    p2p_listener_start(&listener);

    p2p_dispatcher dispatcher;
    p2p_dispatcher_init(&dispatcher, &listener, &psyst, &gossipsyst);
    p2p_dispatcher_start(&dispatcher);
    
    printf("[main] sending punch packet\n");
    p2p_peer peer;
    p2p_psystem_punchnat(&psyst, other_uid, other_ip, &peer, 1);
    sleep(20);

    gossip_system_end(&gossipsyst);
    p2p_psystem_end(&psyst);
    p2p_dispatcher_end(&dispatcher);
    p2p_listener_end(&listener);

    udp_close(client);
}
