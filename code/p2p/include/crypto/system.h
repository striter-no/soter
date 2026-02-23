#include <netinet/in.h>
#include <sodium.h>
#include <sodium/core.h>
#include <stdint.h>
#include <string.h>
#include "keys.h"
#include "signing.h"
#include <netcore/udp_proto.h>

#ifndef SOTER_MESSAGE_MAXLEN
#define SOTER_MESSAGE_MAXLEN (1420 - 25) /*MTU - sizeof(udp_packet)*/
#endif

#ifndef SOTER_P2P_CRYPTO

#ifdef SOTER_CRYPTO_OVERHEAD
#undef SOTER_CRYPTO_OVERHEAD
#endif

#define SOTER_CRYPTO_OVERHEAD crypto_aead_chacha20poly1305_ietf_ABYTES

int soter_crypto_init(){
    return sodium_init();
}

int soter_crypto_encrypt(
    unsigned char *out_ciphertext,
    const unsigned char *raw_data,
    size_t raw_size,
    uint32_t seq,
    soter_session_keys sk
){
    unsigned char nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES] = {0};
    memcpy(nonce, &seq, sizeof(seq));

    unsigned long long out_len;
    return crypto_aead_chacha20poly1305_ietf_encrypt(
        out_ciphertext, &out_len,
        raw_data, raw_size,
        NULL, 0,
        NULL, nonce, sk.tx
    );
}

int soter_crypto_decrypt(
    unsigned char *out_raw,
    const unsigned char *ciphertext,
    size_t cipher_size,
    uint32_t seq,
    soter_session_keys sk
){
    if (cipher_size < crypto_aead_chacha20poly1305_ietf_ABYTES) return -1;

    unsigned char nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES] = {0};
    memcpy(nonce, &seq, sizeof(seq));

    unsigned long long out_len;
    return crypto_aead_chacha20poly1305_ietf_decrypt(
        out_raw, &out_len,
        NULL,
        ciphertext, cipher_size,
        NULL, 0,
        nonce, sk.rx
    );
}

udp_packet *soter_crypto_pack(
    const soter_session_keys *sk,
    udp_packet *pack,
    bool        apply_ntoh
){
    uint32_t orig_size = apply_ntoh ? ntohl(pack->d_size): pack->d_size;
    if (orig_size + SOTER_CRYPTO_OVERHEAD > SOTER_MESSAGE_MAXLEN){
        SLOG_ERROR("[soter][crypto] pack: message is too long for beeing send: %zu (more than %zu)", orig_size + SOTER_CRYPTO_OVERHEAD, SOTER_MESSAGE_MAXLEN);
        return NULL;
    }

    uint32_t seq       = apply_ntoh ? ntohl(pack->seq): pack->seq;
    size_t   encrsize  = orig_size + SOTER_CRYPTO_OVERHEAD;
    uint8_t *encrypted = malloc(encrsize);
    if (0 > soter_crypto_encrypt(encrypted, pack->data, orig_size, seq, *sk)){
        SLOG_ERROR("[soter][uncrypt] pack: failed to encrypt message");
        free(encrypted);
        return NULL;
    }

    udp_packet *out = udp_make_pack(
        seq, pack->h_from, pack->h_to, pack->packtype, encrypted, encrsize
    ); // currently in net order
    free(encrypted);

    return out;
}

udp_packet *soter_uncrypt_pack(
    const soter_session_keys *sk,
    udp_packet *pack,
    bool        apply_ntoh
){
    size_t orig_size = apply_ntoh ? ntohl(pack->d_size): pack->d_size;
    if (orig_size < SOTER_CRYPTO_OVERHEAD){
        SLOG_ERROR("[soter][uncrypt] pack: message is too short for beeing uncrypted: %zu (less than %zu)", orig_size + SOTER_CRYPTO_OVERHEAD, SOTER_CRYPTO_OVERHEAD);
        return NULL;
    }
    
    uint32_t seq       = apply_ntoh ? ntohl(pack->seq): pack->seq;
    size_t   decrsize  = orig_size - SOTER_CRYPTO_OVERHEAD;
    uint8_t *decrypted = malloc(decrsize);
    if (0 > soter_crypto_decrypt(decrypted, pack->data, orig_size, seq, *sk)){
        SLOG_ERROR("[soter][uncrypt] pack: failed to decrypt message");
        free(decrypted);
        return NULL;
    }

    udp_packet *out = udp_make_pack(
        seq, pack->h_from, pack->h_to, pack->packtype, decrypted, decrsize
    ); // currently in net order
    free(decrypted);

    udp_packet *out2 = retranslate_udp(out, 0); // return in host order
    free(out);

    return out2;
}

#endif
#define SOTER_P2P_CRYPTO