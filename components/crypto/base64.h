#ifndef SMARTLOCK_BASE64_H
#define SMARTLOCK_BASE64_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns encoded length (excluding null), or 0 on error (buffer too small). */
size_t base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif /* SMARTLOCK_BASE64_H */
