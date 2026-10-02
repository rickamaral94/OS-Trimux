#define _GNU_SOURCE
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int tm_strlcpy(char *dst, const char *src, size_t size)
{
    if (!dst || size == 0)
        return -1;
    if (!src)
        src = "";
    size_t n = strlen(src);
    if (n >= size) {
        memcpy(dst, src, size - 1);
        dst[size - 1] = '\0';
        return -1;
    }
    memcpy(dst, src, n + 1);
    return 0;
}

int tm_snprintf(char *dst, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(dst, size, fmt, ap);
    va_end(ap);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

char *tm_trim(char *s)
{
    if (!s)
        return s;
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    return s;
}

int tm_strcasecmp_ascii(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb || !ca)
            return ca - cb;
    }
}

int tm_ends_with_ci(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && tm_strcasecmp_ascii(s + ls - lf, suffix) == 0;
}

int tm_starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int tm_path_join(char *out, size_t size, const char *a, const char *b)
{
    size_t la = strlen(a);
    while (*b == '/')
        b++;
    if (la > 0 && a[la - 1] == '/')
        return tm_snprintf(out, size, "%s%s", a, b);
    return tm_snprintf(out, size, "%s/%s", a, b);
}

int tm_name_is_safe(const char *name)
{
    if (!name || !*name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 0x20 || *p == '/' || *p == '\\' || *p == 0x7f)
            return 0;
    return strlen(name) < 256;
}

int tm_path_is_safe_under(const char *root, const char *path)
{
    if (!root || !path || !*path)
        return 0;
    size_t lr = strlen(root);
    if (strncmp(path, root, lr) != 0 || (path[lr] != '/' && path[lr] != '\0'))
        return 0;
    if (strlen(path) >= TM_PATH_MAX)
        return 0;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        if (*p < 0x20 || *p == 0x7f)
            return 0;
    /* reject any ".." component */
    const char *p = path;
    while ((p = strstr(p, "..")) != NULL) {
        int left = (p == path || p[-1] == '/');
        int right = (p[2] == '\0' || p[2] == '/');
        if (left && right)
            return 0;
        p += 2;
    }
    return 1;
}

int tm_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int tm_dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int tm_mkdir_p(const char *path)
{
    char tmp[TM_PATH_MAX];
    if (tm_strlcpy(tmp, path, sizeof tmp) != 0)
        return -1;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return tm_dir_exists(tmp) ? 0 : -1;
}

char *tm_read_file(const char *path, size_t max_size, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap + 1);
    while (buf) {
        if (len == cap) {
            if (cap >= max_size) {
                free(buf);
                buf = NULL;
                break;
            }
            cap = cap * 2 > max_size ? max_size : cap * 2;
            char *nb = realloc(buf, cap + 1);
            if (!nb) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = nb;
        }
        size_t n = fread(buf + len, 1, cap - len, f);
        if (n == 0)
            break;
        len += n;
    }
    if (buf && ferror(f)) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) {
        buf[len] = '\0';
        if (out_len)
            *out_len = len;
    }
    return buf;
}

int tm_read_line(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char *r = fgets(buf, (int)size, f);
    fclose(f);
    if (!r)
        return -1;
    char *t = tm_trim(buf);
    if (t != buf)
        memmove(buf, t, strlen(t) + 1);
    return 0;
}

int tm_read_long(const char *path, long *out)
{
    char buf[64], *end;
    if (tm_read_line(path, buf, sizeof buf) != 0)
        return -1;
    errno = 0;
    long v = strtol(buf, &end, 10);
    if (errno || end == buf)
        return -1;
    *out = v;
    return 0;
}

int tm_write_str(const char *path, const char *value)
{
    /* O_TRUNC is ignored by sysfs and keeps plain files (tests) consistent */
    int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0)
        return -1;
    size_t n = strlen(value);
    ssize_t w = write(fd, value, n);
    int rc = (w == (ssize_t)n) ? 0 : -1;
    if (close(fd) != 0)
        rc = -1;
    return rc;
}

int tm_atomic_write(const char *path, const void *data, size_t len)
{
    char tmp[TM_PATH_MAX];
    if (tm_snprintf(tmp, sizeof tmp, "%s.tmp", path) != 0)
        return -1;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return -1;
    const char *p = data;
    size_t left = len;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            unlink(tmp);
            return -1;
        }
        p += w;
        left -= (size_t)w;
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    /* fsync the directory so the rename itself is durable */
    char dir[TM_PATH_MAX];
    tm_strlcpy(dir, path, sizeof dir);
    char *slash = strrchr(dir, '/');
    if (slash) {
        if (slash == dir)
            slash[1] = '\0';
        else
            *slash = '\0';
        int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dfd >= 0) {
            fsync(dfd); /* best effort: vfat may not support it */
            close(dfd);
        }
    }
    return 0;
}

int tm_copy_file(const char *src, const char *dst)
{
    size_t len = 0;
    char *data = tm_read_file(src, 64u << 20, &len);
    if (!data)
        return -1;
    int rc = tm_atomic_write(dst, data, len);
    free(data);
    return rc;
}

/* Maps the second byte of a UTF-8 sequence starting with 0xC3 (U+00C0..U+00FF)
 * to its unaccented ASCII letter. */
static char fold_c3(unsigned char c)
{
    static const char map[64] =
        "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs"
        "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
    if (c >= 0x80 && c <= 0xBF)
        return map[c - 0x80];
    return 0;
}

void tm_fold_key(const char *in, char *out, size_t size)
{
    size_t o = 0;
    if (size == 0)
        return;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 1 < size; p++) {
        if (*p == 0xC3 && p[1]) {
            char c = fold_c3(p[1]);
            if (c) {
                out[o++] = (char)tolower((unsigned char)c);
                p++;
                continue;
            }
        }
        out[o++] = (char)tolower(*p);
    }
    out[o] = '\0';
}

uint64_t tm_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
