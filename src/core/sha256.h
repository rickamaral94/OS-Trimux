/* SHA-256 (FIPS 180-4), used to verify downloaded update packages. */
#ifndef TRIMUX_SHA256_H
#define TRIMUX_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    size_t n;
} TmSha256;

void tm_sha256_init(TmSha256 *s);
void tm_sha256_update(TmSha256 *s, const void *data, size_t len);
void tm_sha256_final(TmSha256 *s, uint8_t out[32]);
/* Lower-case hex digest of a file. Returns 0 on success. */
int tm_sha256_file(const char *path, char hex[65]);

#endif
