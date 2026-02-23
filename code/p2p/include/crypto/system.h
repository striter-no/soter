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
    SLOG_DEBUG("[encr] nonce: %02x%02x%02x%02x%02x%02x%02x%02x",
           nonce[0], nonce[1], nonce[2], nonce[3],
           nonce[4], nonce[5], nonce[6], nonce[7]);

    SLOG_DEBUG("[encr] sk.tx: %02x%02x%02x%02x%02x%02x%02x%02x",
           sk.tx[0], sk.tx[1], sk.tx[2], sk.tx[3],
           sk.tx[4], sk.tx[5], sk.tx[6], sk.tx[7]);

    SLOG_DEBUG("[encr] sk.rx: %02x%02x%02x%02x%02x%02x%02x%02x",
           sk.rx[0], sk.rx[1], sk.rx[2], sk.rx[3],
           sk.rx[4], sk.rx[5], sk.rx[6], sk.rx[7]);

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

    SLOG_DEBUG("[decr] nonce: %02x%02x%02x%02x%02x%02x%02x%02x",
           nonce[0], nonce[1], nonce[2], nonce[3],
           nonce[4], nonce[5], nonce[6], nonce[7]);

    SLOG_DEBUG("[decr] sk.tx: %02x%02x%02x%02x%02x%02x%02x%02x",
           sk.tx[0], sk.tx[1], sk.tx[2], sk.tx[3],
           sk.tx[4], sk.tx[5], sk.tx[6], sk.tx[7]);

    SLOG_DEBUG("[decr] sk.rx: %02x%02x%02x%02x%02x%02x%02x%02x",
           sk.rx[0], sk.rx[1], sk.rx[2], sk.rx[3],
           sk.rx[4], sk.rx[5], sk.rx[6], sk.rx[7]);

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

    SLOG_DEBUG("crypt: h_from before: %u", pack->h_from);
    udp_packet *out = udp_make_pack(
        seq, 
        apply_ntoh? ntohl(pack->h_from): pack->h_from, 
        apply_ntoh? ntohl(pack->h_to): pack->h_to, 
        pack->packtype, 
        encrypted, 
        encrsize
    ); // currently in net order
    free(encrypted);
    SLOG_DEBUG("crypt: h_from after: %u", out->h_from);

    return out;
}

udp_packet *soter_uncrypt_pack(
    const soter_session_keys *sk,
    udp_packet *pack,
    bool        apply_ntoh
){
    SLOG_DEBUG("uncrypt: origin before: %zu", pack->d_size);
    size_t orig_size = apply_ntoh ? ntohl(pack->d_size): pack->d_size;
    SLOG_DEBUG("uncrypt: origin after: %zu", orig_size);
    if (orig_size < SOTER_CRYPTO_OVERHEAD){
        SLOG_ERROR("[soter][uncrypt] pack: message is too short for beeing uncrypted: %zu (less than %zu)", orig_size + SOTER_CRYPTO_OVERHEAD, SOTER_CRYPTO_OVERHEAD);
        return NULL;
    }
    
    uint32_t seq       = apply_ntoh ? ntohl(pack->seq): pack->seq;
    SLOG_DEBUG("uncrypt: seq after: %zu", seq);

    size_t   decrsize  = orig_size - SOTER_CRYPTO_OVERHEAD;
    SLOG_DEBUG("uncr: decrsize: %zu", decrsize);

    SLOG_DEBUG("decrypting for: %u -> %u", pack->h_from, pack->h_to);
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

    SLOG_DEBUG("uncrypt: h_from before: %u", out->h_from);
    udp_packet *out2 = retranslate_udp(out, 0); // return in host order
    free(out);
    SLOG_DEBUG("uncrypt: h_from after: %u", out2->h_from);

    return out2;
}

#endif
#define SOTER_P2P_CRYPTO