#include <sodium.h>
#include "keys.h"

#ifndef SOTER_P2P_CRYPTOHANDSHAKE

int soter_handshake_server(
    const unsigned char *client_pubk,
    const soter_keypair *kp, 
    soter_session_keys  *sk
){
    return crypto_kx_server_session_keys(
        sk->rx, sk->tx, 
        kp->public_key, kp->private_key, 
        client_pubk
    );
}

int soter_handshake_client(
    const unsigned char *server_pubk,
    const soter_keypair *kp,
    soter_session_keys  *sk
){
    return crypto_kx_client_session_keys(
        sk->rx, sk->tx, 
        kp->public_key, kp->private_key, 
        server_pubk
    );
}

#endif
#define SOTER_P2P_CRYPTOHANDSHAKE