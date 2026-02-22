#include <netcore/udp_proto.h>
#include <base/dyn_queue.h>
#include <natpunch/sserver.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#define DEAD_DT 7

int main(void){
    SOTER_LOGER = logger_init(STDOUT_FILENO);
    const char bind_ip[] = "127.0.0.1";
    unsigned   bind_port = 9000;
    naddr_t    addr      = naddr_make4(nipv4(bind_ip, bind_port));

    p2p_udp    server;
    udp_create(&server, 0);
    udp_bind(&server, addr);

    dyn_queue  peers = dyn_queue_create(sizeof(p2p_state_peer));
    dyn_table  alive = dyn_table_create(sizeof(uint32_t), sizeof(uint64_t), DYN_OWN_BOTH);

    while (true){
        int r = netfd_wait(server.fd, POLLIN, 100);
        
        for (size_t i = 0; i < alive.array.len;){
            dyn_pair *at = dyn_array_at(&alive.array, i);
            uint32_t  uid = *(uint32_t*)at->first;
            uint64_t  timestamp = *(uint64_t*)at->second;

            if (get_timestump() - timestamp >= DEAD_DT){
                SLOG_INFO("[clear] removing timeouted %u (%zu remains)", uid, alive.array.len - 1);
                dyn_table_remove(&alive, &uid);
            } else {
                i++;
            }
        }
        if (alive.array.len == 0){
            for (size_t i = 0; i < peers.arr.len; i++) dyn_array_remove(&peers.arr, 0);
        }

        if (r <= 0) continue;

        nnet_fd from = {0};
        udp_packet *incoming = udp_pack_recv(&server, &from);
        if (!incoming) {
            SLOG_WARNING("[listener] aborted packet");
            continue;
        }

        uint32_t UID = 0;
        
        // if (0 >= netfd_wait(server.fd, POLLIN, -1)) continue;
        if (sizeof(uint32_t) != incoming->d_size){
            SLOG_WARNING("[run] ignoring corrupted packet");
            continue;
        }


        memcpy(&UID, incoming->data, sizeof(uint32_t));
        naddr_t from_ip = naddr_nfd2str(from);
        p2p_state_peer state = p2p_state_info2peer(from_ip, UID);

        if (peers.arr.len == 0){
            udp_packet *pack = udp_make_pack(0, 0, UID, P2P_PACK_STATE, "0", 1);
            udp_pack_send(&server, pack, from);

            uint64_t t = get_timestump();
            dyn_table_set(&alive, &UID, &t);
        } else {
            uint64_t t = get_timestump();
            dyn_table_set(&alive, &UID, &t);
            
            p2p_state_peer *prev = dyn_queue_peek(&peers);
            if (prev->ip == state.ip && prev->port == state.port){
                SLOG_INFO("[run] ignoring request due host:host situation (%s:%u)", from_ip.ip.v4.ip, from_ip.ip.v4.port);
                udp_packet *pack = udp_make_pack(0, 0, UID, P2P_PACK_STATE, "0", 1);
                udp_pack_send(&server, pack, from);

                free(incoming);
                continue;
            }

            if (dyn_array_count(&alive.array, &prev->uid) == 0){
                SLOG_INFO("[run] removing candidate from queue, due its un-aliveness");
                dyn_queue_pop(&peers, NULL);
                free(incoming);
                continue;
            }

            naddr_t peer_ip; uint32_t peer_uid;
            p2p_state_peer2info(*prev, &peer_ip, &peer_uid);

            SLOG_INFO("[run] peer sended: %s:%u:%u", peer_ip.ip.v4.ip, peer_ip.ip.v4.port, peer_uid);
            udp_packet *pack = udp_make_pack(0, 0, UID, P2P_PACK_STATE, prev, sizeof(*prev));
            udp_pack_send(&server, pack, from);
            dyn_queue_pop(&peers, NULL);
        }

        uint64_t t = get_timestump();
        dyn_table_set(&alive, &UID, &t);
        dyn_queue_push(&peers, &state);
        SLOG_INFO("[run] new peer accuired: %s:%u:%u", from_ip.ip.v4.ip, from_ip.ip.v4.port, UID);

        free(incoming);
    }

    udp_close(server);
    dyn_queue_end(&peers);
    dyn_table_end(&alive);

    logger_stop(&SOTER_LOGER);
}