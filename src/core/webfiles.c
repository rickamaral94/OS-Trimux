#define _GNU_SOURCE
#include "webfiles.h"
#include "log.h"
#include "net.h"
#include "tools.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define BUSYBOX "/bin/busybox"
#define MAX_ENTRIES 20000
#define FREE_MARGIN (16ULL << 20) /* never fill the card to the last byte */

/* ------------------------------------------------------------ server */

static int pid_path(const TmPaths *p, char *out, size_t size) { return tm_path_join(out, size, p->tmp, "web.pid"); }

static pid_t read_pid(const TmPaths *p)
{
    char pf[TM_PATH_MAX], path[64];
    long pid = 0;
    if (pid_path(p, pf, sizeof pf) != 0 || tm_read_long(pf, &pid) != 0 || pid <= 1)
        return -1;
    snprintf(path, sizeof path, "/proc/%ld/cmdline", pid);
    size_t len = 0;
    char *cmd = tm_read_file(path, 4096, &len);
    int ours = 0;
    for (size_t i = 0; cmd && i + 5 <= len; i++)
        if (memcmp(cmd + i, "httpd", 5) == 0)
            ours = 1;
    free(cmd);
    return ours ? (pid_t)pid : -1;
}

int tm_web_available(const TmPaths *p)
{
    char bb[TM_PATH_MAX], page[TM_PATH_MAX];
    return tm_fw_path(bb, sizeof bb, BUSYBOX) == 0 && access(bb, X_OK) == 0 &&
           tm_path_join(page, sizeof page, p->share, "web/index.html") == 0 && tm_file_exists(page);
}

int tm_web_running(const TmPaths *p) { return read_pid(p) > 0; }

void tm_web_stop(const TmPaths *p)
{
    pid_t pid = read_pid(p);
    if (pid > 0) {
        kill(-pid, SIGTERM); /* httpd and any running CGI share the group */
        kill(pid, SIGTERM);
        for (int i = 0; i < 20 && read_pid(p) > 0; i++)
            usleep(25000);
        if (read_pid(p) > 0) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
        }
        LOGI("web: file server stopped (pid %d)", (int)pid);
    }
    char pf[TM_PATH_MAX];
    if (pid_path(p, pf, sizeof pf) == 0)
        unlink(pf);
}

int tm_web_start(const TmPaths *p, const char *ip, int port)
{
    if (!ip || !ip[0])
        return -1;
    for (const char *c = ip; *c; c++)
        if (!((*c >= '0' && *c <= '9') || *c == '.'))
            return -1;
    tm_web_stop(p);
    char www[TM_PATH_MAX], cgi[TM_PATH_MAX], page[TM_PATH_MAX], dst[TM_PATH_MAX], script[TM_PATH_MAX],
        ctl[TM_PATH_MAX], bb[TM_PATH_MAX];
    if (tm_path_join(www, sizeof www, p->tmp, "www") != 0 || tm_path_join(cgi, sizeof cgi, www, "cgi-bin") != 0 ||
        tm_path_join(page, sizeof page, p->share, "web/index.html") != 0 ||
        tm_path_join(dst, sizeof dst, www, "index.html") != 0 ||
        tm_path_join(script, sizeof script, cgi, "files") != 0 ||
        tm_path_join(ctl, sizeof ctl, p->sys, "bin/trimuxctl") != 0 || tm_fw_path(bb, sizeof bb, BUSYBOX) != 0)
        return -1;
    if (strchr(ctl, '\'') || tm_mkdir_p(cgi) != 0 || tm_copy_file(page, dst) != 0)
        return -1;
    char body[TM_PATH_MAX + 64];
    snprintf(body, sizeof body, "#!/bin/sh\nexec '%s' webcgi\n", ctl);
    if (tm_atomic_write(script, body, strlen(body)) != 0 || chmod(script, 0755) != 0)
        return -1;
    char listen[64];
    snprintf(listen, sizeof listen, "%s:%d", ip, port);
    char *argv[] = {bb, "httpd", "-f", "-p", listen, "-h", www, NULL};
    pid_t pid = tm_spawn(argv, "/", NULL);
    if (pid <= 0)
        return -1;
    char s[32], pf[TM_PATH_MAX];
    snprintf(s, sizeof s, "%d\n", (int)pid);
    if (pid_path(p, pf, sizeof pf) != 0 || tm_atomic_write(pf, s, strlen(s)) != 0) {
        kill(-pid, SIGTERM);
        return -1;
    }
    LOGI("web: file server on http://%s (pid %d)", listen, (int)pid);
    return 0;
}

/* ------------------------------------------------------------ CGI */

static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') {
            *o++ = ' ';
        } else if (*s == '%' && s[1] && s[2]) {
            char h[3] = {s[1], s[2], 0};
            char *end;
            long v = strtol(h, &end, 16);
            if (*end == '\0') {
                *o++ = (char)v;
                s += 2;
            } else {
                *o++ = *s;
            }
        } else {
            *o++ = *s;
        }
    }
    *o = '\0';
}

/* value of key in a query string (decoded), "" if absent */
static void query_get(const char *q, const char *key, char *out, size_t size)
{
    out[0] = '\0';
    size_t kl = strlen(key);
    for (const char *s = q; s && *s;) {
        const char *amp = strchr(s, '&');
        size_t len = amp ? (size_t)(amp - s) : strlen(s);
        if (len > kl && strncmp(s, key, kl) == 0 && s[kl] == '=') {
            size_t vl = len - kl - 1;
            if (vl >= size)
                vl = size - 1;
            memcpy(out, s + kl + 1, vl);
            out[vl] = '\0';
            url_decode(out);
            return;
        }
        s = amp ? amp + 1 : NULL;
    }
}

/* Normalizes a relative path (no leading/trailing '/') and refuses "..",
 * ".", empty components and control characters. "" is the card root. */
static int clean_rel(char *rel)
{
    char tmp[TM_PATH_MAX];
    size_t o = 0;
    const char *s = rel;
    while (*s == '/')
        s++;
    while (*s) {
        const char *e = strchr(s, '/');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (n == 0) { /* "//" */
            s++;
            continue;
        }
        char comp[256];
        if (n >= sizeof comp)
            return -1;
        memcpy(comp, s, n);
        comp[n] = '\0';
        if (!tm_name_is_safe(comp) || o + n + 2 >= sizeof tmp)
            return -1;
        if (o)
            tmp[o++] = '/';
        memcpy(tmp + o, comp, n);
        o += n;
        s += n;
        while (*s == '/')
            s++;
    }
    tmp[o] = '\0';
    memcpy(rel, tmp, o + 1);
    return 0;
}

static int abs_of(const char *root, const char *rel, char *out, size_t size)
{
    return rel[0] ? tm_path_join(out, size, root, rel) : tm_strlcpy(out, root, size);
}

static void reply(FILE *out, int code, const char *json)
{
    const char *txt = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" :
                      code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed" : code == 409 ? "Conflict" :
                      code == 507 ? "Insufficient Storage" : "Internal Server Error";
    fprintf(out,
            "HTTP/1.0 %d %s\r\nContent-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\n"
            "Content-Length: %zu\r\n\r\n%s",
            code, txt, strlen(json), json);
}

static int fail(FILE *out, int code, const char *err)
{
    char j[96];
    snprintf(j, sizeof j, "{\"error\":\"%s\"}", err);
    reply(out, code, j);
    return -1;
}

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
        if (*c == '"' || *c == '\\')
            fprintf(f, "\\%c", *c);
        else if (*c < 0x20)
            fprintf(f, "\\u%04x", *c);
        else
            fputc(*c, f);
    }
    fputc('"', f);
}

typedef struct {
    char *name;
    int dir;
    long long size;
    long mtime;
} Entry;

static int cmp_entry(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    if (x->dir != y->dir)
        return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}

static int op_list(const char *root, const char *rel, FILE *out)
{
    char abs[TM_PATH_MAX];
    if (abs_of(root, rel, abs, sizeof abs) != 0)
        return fail(out, 400, "path");
    DIR *d = opendir(abs);
    if (!d)
        return fail(out, 404, "notfound");
    Entry *v = NULL;
    size_t n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < MAX_ENTRIES) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char p[TM_PATH_MAX];
        struct stat st;
        if (tm_path_join(p, sizeof p, abs, e->d_name) != 0 || lstat(p, &st) != 0 ||
            !(S_ISDIR(st.st_mode) || S_ISREG(st.st_mode)))
            continue;
        if (n == cap) {
            Entry *nv = realloc(v, (cap = cap ? cap * 2 : 128) * sizeof *v);
            if (!nv)
                break;
            v = nv;
        }
        v[n].name = strdup(e->d_name);
        v[n].dir = S_ISDIR(st.st_mode);
        v[n].size = (long long)st.st_size;
        v[n].mtime = (long)st.st_mtime;
        if (v[n].name)
            n++;
    }
    closedir(d);
    qsort(v, n, sizeof *v, cmp_entry);
    struct statvfs vfs;
    unsigned long long fre = 0, total = 0;
    if (statvfs(root, &vfs) == 0) {
        fre = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
        total = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
    }
    char *buf = NULL;
    size_t blen = 0;
    FILE *j = open_memstream(&buf, &blen);
    if (!j) {
        for (size_t i = 0; i < n; i++)
            free(v[i].name);
        free(v);
        return fail(out, 500, "memory");
    }
    fputs("{\"path\":", j);
    json_str(j, rel);
    fprintf(j, ",\"readonly\":%s,\"free\":%llu,\"total\":%llu,\"entries\":[",
            rel[0] && tm_path_protected(rel) ? "true" : "false", fre, total);
    for (size_t i = 0; i < n; i++) {
        fputs(i ? ",{\"n\":" : "{\"n\":", j);
        json_str(j, v[i].name);
        fprintf(j, ",\"d\":%d,\"s\":%lld,\"t\":%ld", v[i].dir, v[i].size, v[i].mtime);
        int ro = !rel[0] && tm_path_protected(v[i].name);
        fputs(ro ? ",\"ro\":1}" : "}", j);
        free(v[i].name);
    }
    fputs("]}", j);
    fclose(j);
    free(v);
    reply(out, 200, buf);
    free(buf);
    return 0;
}

static int op_get(const char *root, const char *rel, FILE *out)
{
    char abs[TM_PATH_MAX];
    struct stat st;
    if (!rel[0] || abs_of(root, rel, abs, sizeof abs) != 0)
        return fail(out, 400, "path");
    int fd = open(abs, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (fd >= 0)
            close(fd);
        return fail(out, 404, "notfound");
    }
    const char *name = strrchr(rel, '/');
    name = name ? name + 1 : rel;
    fprintf(out, "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lld\r\n"
                 "Content-Disposition: attachment; filename*=UTF-8''",
            (long long)st.st_size);
    for (const unsigned char *c = (const unsigned char *)name; *c; c++) {
        if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || strchr("-._~", *c))
            fputc(*c, out);
        else
            fprintf(out, "%%%02X", *c);
    }
    fputs("\r\n\r\n", out);
    char buf[65536];
    ssize_t r;
    while ((r = read(fd, buf, sizeof buf)) > 0)
        if (fwrite(buf, 1, (size_t)r, out) != (size_t)r)
            break;
    close(fd);
    return 0;
}

/* where a write would land: the parent folder must exist and not be protected */
static int writable_dir(const char *root, const char *rel, char *abs, size_t size)
{
    if (rel[0] && tm_path_protected(rel))
        return 403;
    if (abs_of(root, rel, abs, size) != 0)
        return 400;
    return tm_dir_exists(abs) ? 0 : 404;
}

static int op_put(const char *root, const char *rel, const char *name, int overwrite, FILE *in, FILE *out)
{
    char dir[TM_PATH_MAX], dst[TM_PATH_MAX], part[TM_PATH_MAX], pname[300];
    int code = writable_dir(root, rel, dir, sizeof dir);
    if (code)
        return fail(out, code, code == 403 ? "protected" : code == 404 ? "notfound" : "path");
    if (!tm_name_is_safe(name) || (!rel[0] && tm_path_protected(name)))
        return fail(out, 400, "name");
    const char *cl = getenv("CONTENT_LENGTH");
    char *end = NULL;
    long long len = cl ? strtoll(cl, &end, 10) : -1;
    if (!cl || *end || len < 0)
        return fail(out, 400, "length");
    if (tm_path_join(dst, sizeof dst, dir, name) != 0 || snprintf(pname, sizeof pname, ".%s.trimux-part", name) >= (int)sizeof pname ||
        tm_path_join(part, sizeof part, dir, pname) != 0)
        return fail(out, 400, "name");
    struct stat st;
    if (stat(dst, &st) == 0 && (S_ISDIR(st.st_mode) || !overwrite))
        return fail(out, 409, S_ISDIR(st.st_mode) ? "isdir" : "exists");
    struct statvfs vfs;
    if (statvfs(dir, &vfs) == 0 && (unsigned long long)vfs.f_bavail * vfs.f_frsize < (unsigned long long)len + FREE_MARGIN)
        return fail(out, 507, "nospace");
    int fd = open(part, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return fail(out, 500, "write");
    char buf[65536];
    long long left = len;
    int ok = 1;
    while (left > 0) {
        size_t want = left > (long long)sizeof buf ? sizeof buf : (size_t)left;
        size_t r = fread(buf, 1, want, in);
        if (r == 0) {
            ok = 0; /* the browser stopped sending */
            break;
        }
        for (size_t off = 0; off < r;) {
            ssize_t w = write(fd, buf + off, r - off);
            if (w <= 0) {
                ok = 0;
                break;
            }
            off += (size_t)w;
        }
        if (!ok)
            break;
        left -= (long long)r;
    }
    if (fsync(fd) != 0)
        ok = 0;
    close(fd);
    if (!ok || rename(part, dst) != 0) {
        unlink(part);
        LOGW("web: upload of %s/%s failed", rel, name);
        return fail(out, 500, left > 0 ? "incomplete" : "write");
    }
    tm_fsync_parent(dst);
    LOGI("web: uploaded %s%s%s (%lld bytes)", rel, rel[0] ? "/" : "", name, len);
    reply(out, 200, "{\"ok\":true}");
    return 0;
}

static int op_mkdir(const char *root, const char *rel, const char *name, FILE *out)
{
    char dir[TM_PATH_MAX], dst[TM_PATH_MAX];
    int code = writable_dir(root, rel, dir, sizeof dir);
    if (code)
        return fail(out, code, code == 403 ? "protected" : code == 404 ? "notfound" : "path");
    if (!tm_name_is_safe(name) || (!rel[0] && tm_path_protected(name)) || tm_path_join(dst, sizeof dst, dir, name) != 0)
        return fail(out, 400, "name");
    if (mkdir(dst, 0755) != 0)
        return fail(out, errno == EEXIST ? 409 : 500, errno == EEXIST ? "exists" : "write");
    tm_fsync_parent(dst);
    LOGI("web: created folder %s%s%s", rel, rel[0] ? "/" : "", name);
    reply(out, 200, "{\"ok\":true}");
    return 0;
}

static int op_del(const char *root, const char *rel, FILE *out)
{
    char abs[TM_PATH_MAX];
    struct stat st;
    if (!rel[0] || tm_path_protected(rel))
        return fail(out, 403, "protected");
    if (abs_of(root, rel, abs, sizeof abs) != 0)
        return fail(out, 400, "path");
    if (lstat(abs, &st) != 0)
        return fail(out, 404, "notfound");
    if (S_ISDIR(st.st_mode) ? rmdir(abs) != 0 : unlink(abs) != 0)
        return fail(out, 409, S_ISDIR(st.st_mode) ? "notempty" : "write");
    tm_fsync_parent(abs);
    LOGI("web: deleted %s", rel);
    reply(out, 200, "{\"ok\":true}");
    return 0;
}

/* Changes must come from the page itself: the browser sends the page's
 * address as Referer, and another site open in the same browser cannot make
 * it match this server's Host. */
static int same_origin(void)
{
    const char *ref = getenv("HTTP_REFERER"), *host = getenv("HTTP_HOST");
    if (!ref || !host || !host[0] || strncmp(ref, "http://", 7) != 0)
        return 0;
    ref += 7;
    size_t hl = strlen(host);
    return strncmp(ref, host, hl) == 0 && (ref[hl] == '/' || ref[hl] == '\0');
}

int tm_web_cgi(const char *root, FILE *in, FILE *out)
{
    const char *method = getenv("REQUEST_METHOD");
    const char *q = getenv("QUERY_STRING");
    char op[16], rel[TM_PATH_MAX], name[300], ow[8];
    q = q ? q : "";
    query_get(q, "op", op, sizeof op);
    query_get(q, "path", rel, sizeof rel);
    query_get(q, "name", name, sizeof name);
    query_get(q, "overwrite", ow, sizeof ow);
    if (clean_rel(rel) != 0)
        return fail(out, 400, "path");
    int post = method && strcmp(method, "POST") == 0;
    int rc;
    if (strcmp(op, "list") == 0)
        rc = op_list(root, rel, out);
    else if (strcmp(op, "get") == 0)
        rc = op_get(root, rel, out);
    else if (!post && (strcmp(op, "put") == 0 || strcmp(op, "mkdir") == 0 || strcmp(op, "del") == 0))
        rc = fail(out, 405, "method"); /* changes only by POST: a link or <img> can never delete */
    else if (!same_origin())
        rc = fail(out, 403, "origin");
    else if (strcmp(op, "put") == 0)
        rc = op_put(root, rel, name, strcmp(ow, "1") == 0, in, out);
    else if (strcmp(op, "mkdir") == 0)
        rc = op_mkdir(root, rel, name, out);
    else if (strcmp(op, "del") == 0)
        rc = op_del(root, rel, out);
    else
        rc = fail(out, 400, "op");
    fflush(out);
    return rc;
}
