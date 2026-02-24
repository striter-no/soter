#include <sodium.h>


#ifndef SOTER_CRYPTO_SIGNING
#define SOTER_SIGN_BYTES crypto_sign_BYTES

typedef struct {
    unsigned char id_pub[crypto_sign_PUBLICKEYBYTES];
    unsigned char id_sec[crypto_sign_SECRETKEYBYTES];
} soter_sign;

soter_sign soter_sign_gen(){
    soter_sign sgn;
    crypto_sign_keypair(sgn.id_pub, sgn.id_sec);

    return sgn;
}

int soter_sign_data(
    soter_sign *sgn,
    unsigned char *msg_to_sign,
    size_t         msg_len,
    unsigned char  out_signature[crypto_sign_BYTES]
){
    return crypto_sign_detached(
        out_signature, NULL, 
        msg_to_sign, msg_len, 
        sgn->id_sec
    );
}

int soter_sign_verify(
    soter_sign *sgn,
    unsigned char *msg_signed,
    size_t         msg_len,
    unsigned char  in_signature[crypto_sign_BYTES],
    unsigned char  pub_key     [crypto_sign_PUBLICKEYBYTES]
){
    return crypto_sign_verify_detached(in_signature, msg_signed, msg_len, pub_key) == 0;
}

int soter_sign_store(const soter_sign *kp, const char *path){
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    if (sizeof(*kp) != fwrite(kp, sizeof(*kp), 1, f)){
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int soter_sign_load(soter_sign *kp, const char *path){
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
#define SOTER_CRYPTO_SIGNING