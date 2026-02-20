#include "ip_core.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>

#ifndef P2P_UDP_SOCK

typedef struct {
    nnet_fd  fd;
    uint32_t UID;
    naddr_t  addr;
} p2p_udp;

int udp_create(p2p_udp *cli, uint32_t UID){
    cli->fd.rfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (cli->fd.rfd < 0) return -1;

    int opt = 1;
    setsockopt(cli->fd.rfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(cli->fd.rfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    fcntl(cli->fd.rfd, F_SETFL, O_NONBLOCK);

    cli->UID = UID;
    return 0;
}

void udp_close(p2p_udp cli){
    close(cli.fd.rfd);
}

int udp_bind(
    p2p_udp *cli,
    naddr_t addr
){
    naddr_netfd(addr, &cli->fd);

    return bind(
        cli->fd.rfd,
        (const struct sockaddr*)&cli->fd.addr,
        cli->fd.addr_len
    );
}

ssize_t udp_recv(
    p2p_udp *cli,
    void    *buffer,
    size_t   recv_size,

    nnet_fd *from
){
    if (from) {
        from->addr_len = sizeof(struct sockaddr_storage);
        memset(&from->addr, 0, sizeof(from->addr));
    }

    return recvfrom(
        cli->fd.rfd,
        buffer, recv_size, 0,
        (from ? (struct sockaddr*)&from->addr: NULL),
        (from ? &from->addr_len: NULL)
    );
}

ssize_t udp_send(
    p2p_udp    *cli,
    const void *buffer,
    size_t      send_size,

    nnet_fd     to
){
    return sendto(
        cli->fd.rfd,
        buffer, send_size, 0,
        (const struct sockaddr*)&to.addr,
        to.addr_len
    );
}

#endif
#define P2P_UDP_SOCK