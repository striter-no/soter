#include <sodium/crypto_kx.h>
#include <stdio.h>

#ifndef SOTER_CRYPTO_KE
#define SOTER_PUBKEY_BYTES crypto_kx_PUBLICKEYBYTES

typedef struct {
    uint8_t public_key[crypto_kx_PUBLICKEYBYTES];
    uint8_t private_key[crypto_kx_SECRETKEYBYTES];
} soter_keypair;

typedef struct {
    unsigned char rx[crypto_kx_SESSIONKEYBYTES];
    unsigned char tx[crypto_kx_SESSIONKEYBYTES];
} soter_session_keys;

soter_keypair soter_keypair_make(){
    soter_keypair kp;
    crypto_kx_keypair(kp.public_key, kp.private_key);
    
    return kp;
}

int soter_keypair_store(const soter_keypair *kp, const char *path){
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    if (sizeof(*kp) != fwrite(kp, sizeof(*kp), 1, f)){
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int soter_keypair_load(soter_keypair *kp, const char *path){
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    if (fread(kp, sizeof(*kp), 1, f) != sizeof(*kp)){
        fclose(f);
        return -1;
    }
    
    fclose(f);
    return 0;
}

#endif
#define SOTER_CRYPTO_KE