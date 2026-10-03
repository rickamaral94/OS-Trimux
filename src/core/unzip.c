#define _GNU_SOURCE
#include "unzip.h"
#include "log.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* from stb_image (compiled in image.c): raw DEFLATE into a fixed buffer */
int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

#define MAX_ENTRY (512L << 20) /* one file inflated in memory at a time */

uint32_t tm_crc32(uint32_t crc, const void *data, size_t len)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    const unsigned char *p = data;
    crc = ~crc;
    while (len--)
        crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* Relative, no "..", no empty or hidden-root tricks. */
static int name_ok(const char *n)
{
    if (!*n || n[0] == '/' || strchr(n, '\\'))
        return 0;
    for (const char *s = n; *s;) {
        const char *e = strchr(s, '/');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if ((len == 2 && s[0] == '.' && s[1] == '.') || (len == 1 && s[0] == '.'))
            return 0;
        if (!e)
            break;
        s = e + 1;
    }
    return 1;
}

static int write_entry(FILE *z, const char *dest, const char *name, uint32_t off, uint16_t method, uint32_t csize,
                       uint32_t usize, uint32_t crc, unsigned mode)
{
    char path[TM_PATH_MAX];
    if (tm_path_join(path, sizeof path, dest, name) != 0)
        return -1;
    size_t nl = strlen(path);
    if (path[nl - 1] == '/') { /* directory entry */
        path[nl - 1] = '\0';
        return tm_mkdir_p(path);
    }
    if (usize > MAX_ENTRY || csize > MAX_ENTRY || (method != 0 && method != 8))
        return -1;
    unsigned char lh[30];
    if (fseeko(z, off, SEEK_SET) != 0 || fread(lh, 1, 30, z) != 30 || le32(lh) != 0x04034b50)
        return -1;
    if (fseeko(z, (off_t)off + 30 + le16(lh + 26) + le16(lh + 28), SEEK_SET) != 0)
        return -1;
    char *in = malloc(csize ? csize : 1), *out = NULL;
    if (!in || fread(in, 1, csize, z) != csize) {
        free(in);
        return -1;
    }
    if (method == 0) {
        out = in;
        in = NULL;
    } else {
        out = malloc(usize ? usize : 1);
        if (!out || (usize && stbi_zlib_decode_noheader_buffer(out, (int)usize, in, (int)csize) != (int)usize)) {
            free(in);
            free(out);
            return -1;
        }
        free(in);
    }
    int rc = -1;
    if (tm_crc32(0, out, usize) == crc) {
        char *slash = strrchr(path, '/');
        if (slash) {
            *slash = '\0';
            tm_mkdir_p(path);
            *slash = '/';
        }
        unlink(path);
        FILE *f = fopen(path, "wb");
        if (f) {
            rc = fwrite(out, 1, usize, f) == usize ? 0 : -1;
            if (fclose(f) != 0)
                rc = -1;
            chmod(path, (mode & 0111) ? 0755 : 0644);
        }
    } else {
        LOGW("unzip: CRC mismatch in %s", name);
    }
    free(out);
    return rc;
}

int tm_unzip(const char *zip, const char *dest)
{
    FILE *z = fopen(zip, "rb");
    if (!z)
        return -1;
    /* end of central directory: last 22 bytes + up to 64 KiB comment */
    unsigned char tail[22 + 65535];
    off_t size = (fseeko(z, 0, SEEK_END) == 0) ? ftello(z) : -1;
    long want = size > (off_t)sizeof tail ? (long)sizeof tail : (long)size;
    int files = -1;
    if (size < 22 || fseeko(z, size - want, SEEK_SET) != 0 || fread(tail, 1, (size_t)want, z) != (size_t)want)
        goto out;
    long e = -1;
    for (long i = want - 22; i >= 0; i--)
        if (le32(tail + i) == 0x06054b50) {
            e = i;
            break;
        }
    if (e < 0)
        goto out;
    uint16_t count = le16(tail + e + 10);
    uint32_t cd_size = le32(tail + e + 12), cd_off = le32(tail + e + 16);
    if (cd_off == 0xFFFFFFFFu || count == 0xFFFF || (off_t)cd_off + cd_size > size)
        goto out; /* zip64 or broken */
    unsigned char *cd = malloc(cd_size ? cd_size : 1);
    if (!cd || fseeko(z, cd_off, SEEK_SET) != 0 || fread(cd, 1, cd_size, z) != cd_size) {
        free(cd);
        goto out;
    }
    if (tm_mkdir_p(dest) != 0) {
        free(cd);
        goto out;
    }
    files = 0;
    size_t pos = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (pos + 46 > cd_size || le32(cd + pos) != 0x02014b50) {
            files = -1;
            break;
        }
        const unsigned char *h = cd + pos;
        uint16_t flags = le16(h + 8), method = le16(h + 10), nlen = le16(h + 28), xlen = le16(h + 30),
                 clen = le16(h + 32), made_by = le16(h + 4);
        uint32_t crc = le32(h + 16), csize = le32(h + 20), usize = le32(h + 24), ext = le32(h + 38),
                 off = le32(h + 42);
        if (pos + 46 + nlen > cd_size || nlen >= 512) {
            files = -1;
            break;
        }
        char name[512];
        memcpy(name, h + 46, nlen);
        name[nlen] = '\0';
        unsigned mode = (made_by >> 8) == 3 ? ext >> 16 : 0644; /* 3 = Unix */
        int symlink = (made_by >> 8) == 3 && (mode & 0170000) == 0120000;
        if ((flags & 1) || symlink || !name_ok(name) ||
            write_entry(z, dest, name, off, method, csize, usize, crc, mode) != 0) {
            LOGW("unzip: refused or failed entry %s", name);
            files = -1;
            break;
        }
        if (name[nlen - 1] != '/')
            files++;
        pos += 46 + (size_t)nlen + xlen + clen;
    }
    free(cd);
out:
    fclose(z);
    return files;
}
