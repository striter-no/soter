#include <netinet/in.h>
#include <sodium/crypto_hash_sha256.h>
#include <sodium/crypto_kx.h>
#include <sodium/utils.h>
#include <stdio.h>
#include <string.h>

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

uint32_t soter_pubkey_to_uid(const unsigned char *pubkey){
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, pubkey, crypto_kx_PUBLICKEYBYTES);
    
    uint32_t uid;
    memcpy(&uid, hash, sizeof(uid));
    return uid;
}

bool soter_pubkey_and_uid_check(const unsigned char *pubkey, uint32_t uid){
    return uid == soter_pubkey_to_uid(pubkey);
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

char* soter_encode_pubkey_uid(const uint8_t pubkey[32], uint32_t uid) {
    if (pubkey == NULL) return NULL;

    size_t binary_len = 32 + 1 + 4;
    unsigned char binary_blob[37];

    memcpy(binary_blob, pubkey, 32);
    binary_blob[32] = ':';
    
    uint32_t uid_be = htonl(uid);
    memcpy(binary_blob + 33, &uid_be, 4);

    size_t b64_len = sodium_base64_encoded_len(binary_len, sodium_base64_VARIANT_ORIGINAL);
    char* b64_str = (char*)malloc(b64_len);
    if (b64_str == NULL) return NULL;

    if (sodium_bin2base64(b64_str, b64_len, binary_blob, binary_len, 
                          sodium_base64_VARIANT_ORIGINAL) == NULL) {
        free(b64_str);
        return NULL;
    }

    return b64_str;
}

int soter_decode_pubkey_uid(const char* b64_str, uint8_t out_pubkey[32], uint32_t* out_uid) {
    if (b64_str == NULL || out_pubkey == NULL || out_uid == NULL) return -1;

    size_t b64_len = strlen(b64_str);
    size_t max_binary_len = (b64_len * 3) / 4 + 10;
    
    unsigned char* binary_blob = (unsigned char*)malloc(max_binary_len);
    if (binary_blob == NULL) return -1;

    size_t binary_len = 0;
    if (sodium_base642bin(binary_blob, max_binary_len, b64_str, b64_len, 
                          NULL, &binary_len, NULL, sodium_base64_VARIANT_ORIGINAL) != 0) {
        free(binary_blob);
        return -1;
    }

    if (binary_len != 37) {
        free(binary_blob);
        return -1;
    }

    if (binary_blob[32] != ':') {
        free(binary_blob);
        return -1;
    }

    memcpy(out_pubkey, binary_blob, 32);

    uint32_t uid_be;
    memcpy(&uid_be, binary_blob + 33, 4);
    *out_uid = ntohl(uid_be);

    free(binary_blob);
    return 0;
}


#endif
#define SOTER_CRYPTO_KE