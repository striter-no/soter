#include <netcore/udp_sock.h>
#include <natpunch/stun.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef P2P_NAT_HELPER

typedef enum {
    NAT_DYNAMIC,
    NAT_STATIC,
    NAT_SYMMETRIC
} nat_type;

nat_type get_nat_type(
    p2p_udp *client,
    naddr_t first_stun,
    naddr_t second_stun,
    unsigned short port
){
    naddr_t addr[2] = {0};
    
    struct sockaddr_storage old_storage = client->fd.addr;
    socklen_t old_len = client->fd.addr_len;
    naddr_netfd(naddr_make4(nipv4("0.0.0.0", port)), &client->fd);

    if (0 > bind(
        client->fd.rfd,
        (const struct sockaddr*)&client->fd.addr,
        client->fd.addr_len
    )) {perror("bind"); return NAT_SYMMETRIC; }

    p2pnp_udp_stun(client, first_stun, &addr[0]);
    // sleep(1);
    p2pnp_udp_stun(client, second_stun, &addr[1]);

    client->fd.addr = old_storage;
    client->fd.addr_len = old_len;

    return (addr[0].ip.v4.port == addr[1].ip.v4.port) ? (
        (addr[0].ip.v4.port == port) ? NAT_STATIC: NAT_DYNAMIC
    ): NAT_SYMMETRIC;
}

const char *strnattype(nat_type type){
    static const char *answer = NULL;
    switch (type){
        case NAT_SYMMETRIC: answer = "SYMMETRIC"; break;
        case NAT_DYNAMIC:   answer = "DYNAMIC";   break;
        case NAT_STATIC:    answer = "STATIC";    break;
    }
    return answer;
}

#endif
#define P2P_NAT_HELPER