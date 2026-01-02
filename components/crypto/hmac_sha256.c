#include "hmac_sha256.h"

#include <string.h>

#include "sha256.h"

void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len, uint8_t out[32])
{
    if (!out) return;
    if (!msg && msg_len != 0) return;

    uint8_t k0[64];
    memset(k0, 0, sizeof(k0));

    if (key && key_len > 0) {
        if (key_len > 64u) {
            sha256_ctx_t t;
            uint8_t kh[32];
            sha256_init(&t);
            sha256_update(&t, key, key_len);
            sha256_final(&t, kh);
            memcpy(k0, kh, sizeof(kh));
        } else {
            memcpy(k0, key, key_len);
        }
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (size_t i = 0; i < 64u; i++) {
        ipad[i] = (uint8_t)(k0[i] ^ 0x36u);
        opad[i] = (uint8_t)(k0[i] ^ 0x5Cu);
    }

    sha256_ctx_t inner;
    uint8_t inner_hash[32];
    sha256_init(&inner);
    sha256_update(&inner, ipad, sizeof(ipad));
    sha256_update(&inner, msg, msg_len);
    sha256_final(&inner, inner_hash);

    sha256_ctx_t outer;
    sha256_init(&outer);
    sha256_update(&outer, opad, sizeof(opad));
    sha256_update(&outer, inner_hash, sizeof(inner_hash));
    sha256_final(&outer, out);
}

