#define _GNU_SOURCE
#include "runlog.h"
#include "ini.h"
#include "log.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void tm_tail_init(TmTail *t)
{
    t->pos = 0;
    t->total = 0;
}

void tm_tail_add(TmTail *t, const char *data, size_t len)
{
    t->total += len;
    if (len >= TM_TAIL_SIZE) { /* only the end fits */
        memcpy(t->buf, data + len - TM_TAIL_SIZE, TM_TAIL_SIZE);
        t->pos = 0;
        return;
    }
    size_t first = TM_TAIL_SIZE - t->pos;
    if (first > len)
        first = len;
    memcpy(t->buf + t->pos, data, first);
    memcpy(t->buf, data + first, len - first);
    t->pos = (t->pos + len) % TM_TAIL_SIZE;
}

size_t tm_tail_get(const TmTail *t, char *out, size_t size)
{
    size_t kept = t->total < TM_TAIL_SIZE ? t->total : TM_TAIL_SIZE;
    if (kept > size)
        kept = size;
    size_t start = (t->pos + TM_TAIL_SIZE - kept) % TM_TAIL_SIZE;
    size_t first = TM_TAIL_SIZE - start;
    if (first > kept)
        first = kept;
    memcpy(out, t->buf + start, first);
    memcpy(out + first, t->buf, kept - first);
    return kept;
}

int tm_tail_drain(TmTail *t, int fd)
{
    char chunk[4096];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n > 0) {
            tm_tail_add(t, chunk, (size_t)n);
            continue;
        }
        if (n == 0)
            return 1;
        if (errno == EINTR)
            continue;
        return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : 1;
    }
}

int tm_runlog_path(const TmPaths *p, const char *name, char *out, size_t size)
{
    char clean[64];
    size_t n = 0;
    for (const char *s = name; *s && n + 1 < sizeof clean; s++) {
        char c = *s;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                 c == '_' || (c == '.' && n > 0);
        clean[n++] = ok ? c : '_';
    }
    clean[n] = '\0';
    if (!n)
        tm_strlcpy(clean, "app", sizeof clean);
    char dir[TM_PATH_MAX];
    if (tm_path_join(dir, sizeof dir, p->logdir, "apps") != 0)
        return -1;
    return tm_snprintf(out, size, "%s/%s.log", dir, clean);
}

int tm_runlog_save(const char *path, const char *label, int code, unsigned long secs, const TmTail *t)
{
    char dir[TM_PATH_MAX];
    tm_strlcpy(dir, path, sizeof dir);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        tm_mkdir_p(dir);
    }
    static char body[TM_TAIL_SIZE + 512];
    char when[32] = "";
    time_t now = time(NULL);
    struct tm tmv;
    if (localtime_r(&now, &tmv))
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tmv);
    int h = snprintf(body, sizeof body, "# %s, %s\n# exit code %d after %lu s\n%s", label, when, code, secs,
                     t->total > TM_TAIL_SIZE ? "# (only the last 32 KiB of the output are kept)\n" : "");
    if (h < 0 || (size_t)h >= sizeof body)
        return -1;
    size_t len = (size_t)h + tm_tail_get(t, body + h, sizeof body - (size_t)h);
    return tm_atomic_write(path, body, len);
}

static int lastrun_file(const TmPaths *p, char *out, size_t size)
{
    return tm_path_join(out, size, p->tmp, "lastrun.ini");
}

int tm_lastrun_write(const TmPaths *p, const TmLastRun *r)
{
    char path[TM_PATH_MAX], num[32];
    if (lastrun_file(p, path, sizeof path) != 0)
        return -1;
    TmIni ini;
    tm_ini_init(&ini);
    int rc = tm_ini_set(&ini, "run", "label", r->label);
    rc |= tm_ini_set(&ini, "run", "log", r->log);
    snprintf(num, sizeof num, "%d", r->code);
    rc |= tm_ini_set(&ini, "run", "code", num);
    snprintf(num, sizeof num, "%lu", r->secs);
    rc |= tm_ini_set(&ini, "run", "seconds", num);
    if (rc == 0)
        rc = tm_ini_save(&ini, path);
    tm_ini_free(&ini);
    return rc;
}

int tm_lastrun_read(const TmPaths *p, TmLastRun *r)
{
    char path[TM_PATH_MAX];
    memset(r, 0, sizeof *r);
    if (lastrun_file(p, path, sizeof path) != 0 || !tm_file_exists(path))
        return -1;
    TmIni ini;
    tm_ini_init(&ini);
    if (tm_ini_load(&ini, path) != 0) {
        tm_ini_free(&ini);
        return -1;
    }
    tm_strlcpy(r->label, tm_ini_get(&ini, "run", "label", ""), sizeof r->label);
    tm_strlcpy(r->log, tm_ini_get(&ini, "run", "log", ""), sizeof r->log);
    r->code = (int)tm_ini_get_long(&ini, "run", "code", 0);
    long s = tm_ini_get_long(&ini, "run", "seconds", 0);
    r->secs = s > 0 ? (unsigned long)s : 0;
    tm_ini_free(&ini);
    return 0;
}

void tm_lastrun_clear(const TmPaths *p)
{
    char path[TM_PATH_MAX];
    if (lastrun_file(p, path, sizeof path) == 0)
        unlink(path);
}

int tm_lastrun_failed(const TmLastRun *r)
{
    return (r->code != 0 && r->secs < 30) || r->secs < 3;
}
