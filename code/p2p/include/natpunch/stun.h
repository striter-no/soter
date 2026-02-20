#include <netcore/udp_proto.h>

#ifndef P2P_NATPUNCH_STUN

#define STUN_BINDING_REQUEST  0x0001
#define STUN_BINDING_RESPONSE 0x0101
#define STUN_MAGIC_COOKIE     0x2112A442
#define ATTR_XOR_MAPPED_ADDR  0x0020

struct stun_header {
    uint16_t type;
    uint16_t length;
    uint32_t magic;
    uint8_t  id[12];
};

struct stun_attr {
    uint16_t type;
    uint16_t length;
};

int p2pnp_udp_stun(
    p2p_udp *client,
    naddr_t  stun_addr,
    naddr_t *client_ip
){
    struct stun_header req;
    req.type   = htons(STUN_BINDING_REQUEST);
    req.length = htons(0);
    req.magic  = htonl(STUN_MAGIC_COOKIE);
    
    for(int i=0; i<12; i++) req.id[i] = rand();

    nnet_fd stun_fd = netfdq(stun_addr);
    sendto(client->fd.rfd, &req, sizeof(req), 0, (struct sockaddr*)&stun_fd.addr, stun_fd.addr_len);

    uint8_t buf[1024];
    struct sockaddr_storage from;
    socklen_t from_len = sizeof(from);
    
    netfd_wait(client->fd, POLLIN, 10000);
    int r = recvfrom(client->fd.rfd, buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);
    if (r < (int)sizeof(struct stun_header)) {
        perror("recvfrom");
        return -1;
    }

    struct stun_header *res = (struct stun_header *)buf;
    
    if (ntohs(res->type) != STUN_BINDING_RESPONSE || ntohl(res->magic) != STUN_MAGIC_COOKIE) {
        perror("invalid STUN response\n");
        return -1;
    }

    int msg_len = ntohs(res->length);
    int pos = sizeof(struct stun_header);

    while (pos < pos + msg_len && pos + 4 <= r) {
        struct stun_attr *attr = (struct stun_attr *)&buf[pos];
        uint16_t type = ntohs(attr->type);
        uint16_t len  = ntohs(attr->length);

        if (type == ATTR_XOR_MAPPED_ADDR) {
            uint16_t x_port = *(uint16_t *)&buf[pos + 6];
            uint32_t x_ip   = *(uint32_t *)&buf[pos + 8];

            uint16_t port = ntohs(x_port) ^ (STUN_MAGIC_COOKIE >> 16);
            uint32_t ip   = ntohl(x_ip) ^ STUN_MAGIC_COOKIE;

            struct in_addr in;
            in.s_addr = htonl(ip);
            char *ip_str = inet_ntoa(in);

            *client_ip = naddr_make4(nipv4(ip_str, port));
            return 0;
        }

        pos += 4 + ((len + 3) & ~3);
    }
    return -1;
}

#endif
#define P2P_NATPUNCH_STUN