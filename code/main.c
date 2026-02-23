#include <soternet/interface.h>


int main(void){
    
    naddr_t servers[] = {
        naddr_domain("stun.l.google.com", 19302),
        naddr_domain("stunserver2025.stunprotocol.org", 3478),
        naddr_make4(nipv4("89.19.215.10", 9000))
    };

    soter client;
    soter_client(&client, servers[2], servers[0], servers[1], LOG_WARNING);

    printf("NAT type: %s\n", strnattype(client.psyst.nat_type));
    printf("Current UID: %u\n", client.net_client.UID);
    soter_wait_state(&client, -1);
    

    p2p_state state = *soter_get_state(&client, 0);
    soter_p2p_connect(&client, state.UID, state.ip, state.pubkey, &state.stfd, &state.sk);
    printf("Got peer: %s:%u:%u\n", state.ip.ip.v4.ip, state.ip.ip.v4.port, state.UID);
    
    evfd_wait(state.stfd, POLLIN, -1);

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
    soter_send(&client, state.UID, "Hello", 5, state.sk, 0);
    soter_acquire_chan(&client, &chan, state.UID, -1);
    soter_wait_pack(chan, -1);

    void *data; size_t dsize;
    soter_recv(chan, &data, &dsize);

    printf("dsize: %zu\n", dsize);
    printf("got: %.*s\n", (int)dsize, (char*)data);
    free(data);

end:      
    soter_end(&client);
}