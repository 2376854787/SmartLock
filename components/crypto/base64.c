#include "base64.h"

static const char s_tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return 0;
    if (!in && in_len != 0) return 0;

    const size_t enc_len = ((in_len + 2u) / 3u) * 4u;
    if (out_sz < enc_len + 1u) return 0;

    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 3u) {
        const uint32_t b0 = in[i];
        const uint32_t b1 = (i + 1u < in_len) ? in[i + 1u] : 0u;
        const uint32_t b2 = (i + 2u < in_len) ? in[i + 2u] : 0u;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out[o++] = s_tbl[(triple >> 18) & 0x3Fu];
        out[o++] = s_tbl[(triple >> 12) & 0x3Fu];
        out[o++] = (i + 1u < in_len) ? s_tbl[(triple >> 6) & 0x3Fu] : '=';
        out[o++] = (i + 2u < in_len) ? s_tbl[(triple)&0x3Fu] : '=';
    }
    out[o] = '\0';
    return enc_len;
}

