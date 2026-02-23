#include <soternet/interface.h>

int main(void){
    
    naddr_t servers[] = {
        naddr_domain("stun.l.google.com", 19302),
        naddr_domain("stunserver2025.stunprotocol.org", 3478),
        naddr_make4(nipv4("89.19.215.10", 9000))
    };

    soter client;
    soter_client(&client, servers[2], servers[0], servers[1], LOG_DEBUG);
    
    p2p_state state;
    while (true){
        soter_wait_state(&client, -1);
        
        state = *soter_get_state(&client, 0);
        soter_p2p_connect(&client, state.UID, state.ip, state.pubkey, &state.stfd, &state.sk);
        printf("Got peer: %s:%u:%u\n", state.ip.ip.v4.ip, state.ip.ip.v4.port, state.UID);
        
        // evfd_wait(state.stfd, POLLIN, -1);
        struct pollfd fds[2] = {
            {.fd = state.stfd, .events = POLLIN},
            {.fd = client.dispatcher.push_TO_evfd, .events = POLLIN}
        };

        uint64_t r;
        int ret = poll(fds, 2, -1);
        if (ret <= 0) goto end;
        if (fds[1].revents & POLLIN){
            SLOG_INFO("Got dead peer, waiting more...");
            read(fds[1].fd, &r, 8);
            continue;
        }

        if (fds[0].revents & POLLIN){
            SLOG_INFO("Got new peer!");
            read(fds[0].fd, &r, 8);
            break;
        }
    }

    p2p_peer *act_peer = NULL;
    for (int i = 0; i < 5; i++){
        act_peer = soter_peer(&client, state.UID);
        if (!act_peer) {
            SLOG_ERROR("[main] failed to get peer");
            sleep(1);
            continue;
        }
        break;
    }
    if (!act_peer) goto end;

    p2p_rudp_channel *chan = NULL;

    uint32_t seq = 0;
    soter_send(&client, state.UID, "Example chat hello message", 26, state.sk, seq);
    soter_acquire_chan(&client, &chan, state.UID, -1);
    while (true){
        soter_wait_pack(chan, -1);

        void *data; 
        size_t dsize;
        soter_recv(chan, &data, &dsize);

        printf("[%zu bytes] > %.*s\n", dsize, (int)dsize, (char*)data);
        free(data);

        char str[1024] = {0};
        if (scanf("%1023[^\n]", str) == 1) { 
            getchar();
            soter_send(&client, state.UID, str, strlen(str), state.sk, ++seq);
        }
    }

end:      
    soter_end(&client);
}