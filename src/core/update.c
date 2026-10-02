/* Online updater from GitHub releases. */
#define _GNU_SOURCE
#include "update.h"
#include "ini.h"
#include "log.h"
#include "net.h"
#include "sha256.h"
#include "sysinfo.h"
#include "util.h"

#include <ftw.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define JSMN_STATIC
#include "third_party/jsmn.h"

#define DEFAULT_REPO "rickamaral94/OS-Trimux"
#define MAX_JSON (2L * 1024 * 1024)
#define MAX_PACKAGE (200L * 1024 * 1024)

/* ------------------------------------------------------------ versions */

int tm_version_cmp(const char *a, const char *b)
{
    if (*a == 'v' || *a == 'V')
        a++;
    if (*b == 'v' || *b == 'V')
        b++;
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y)
            return x < y ? -1 : 1;
        if (*a == '.')
            a++;
        else if (*a)
            break; /* suffixes such as "-rc1" are ignored */
        if (*b == '.')
            b++;
        else if (*b)
            break;
        if (!*a && !*b)
            break;
    }
    return 0;
}

/* ------------------------------------------------------------ JSON */

typedef struct {
    const char *js;
    jsmntok_t *t;
    int n;
} Json;

static int skip(const Json *j, int i)
{
    int end = i + 1;
    if (j->t[i].type == JSMN_OBJECT || j->t[i].type == JSMN_ARRAY) {
        int count = j->t[i].size * (j->t[i].type == JSMN_OBJECT ? 2 : 1);
        for (int k = 0; k < count; k++)
            end = skip(j, end);
    }
    return end;
}

static int tok_is(const Json *j, int i, const char *s)
{
    int len = j->t[i].end - j->t[i].start;
    return j->t[i].type == JSMN_STRING && (int)strlen(s) == len && strncmp(j->js + j->t[i].start, s, (size_t)len) == 0;
}

static void put_utf8(char *out, size_t size, size_t *o, unsigned cp)
{
    char b[4];
    int n = 0;
    if (cp < 0x80)
        b[n++] = (char)cp;
    else if (cp < 0x800) {
        b[n++] = (char)(0xC0 | (cp >> 6));
        b[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        b[n++] = (char)(0xE0 | (cp >> 12));
        b[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[n++] = (char)(0x80 | (cp & 0x3F));
    }
    for (int k = 0; k < n && *o + 1 < size; k++)
        out[(*o)++] = b[k];
}

/* Copies a JSON string token, decoding escapes. */
static void tok_str(const Json *j, int i, char *out, size_t size)
{
    size_t o = 0;
    const char *p = j->js + j->t[i].start, *e = j->js + j->t[i].end;
    for (; p < e && o + 1 < size; p++) {
        if (*p != '\\' || p + 1 >= e) {
            out[o++] = *p;
            continue;
        }
        p++;
        switch (*p) {
        case 'n': out[o++] = '\n'; break;
        case 't': out[o++] = ' '; break;
        case 'r': break;
        case 'b':
        case 'f': break;
        case 'u': {
            unsigned cp = 0;
            int ok = p + 4 < e;
            for (int k = 1; ok && k <= 4; k++) {
                char c = p[k];
                cp = cp * 16 + (unsigned)(c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                                                         : (ok = 0));
            }
            if (ok) {
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDFFF)
                    cp = '?'; /* surrogate pairs (emoji): not needed in notes */
                put_utf8(out, size, &o, cp);
            }
            break;
        }
        default: out[o++] = *p; break; /* \" \\ \/ */
        }
    }
    out[o] = '\0';
}

static int tok_true(const Json *j, int i)
{
    return j->t[i].type == JSMN_PRIMITIVE && j->js[j->t[i].start] == 't';
}

/* Release notes for the screen: the first section only, light markdown cleanup. */
static void clean_notes(char *s)
{
    char *w = s;
    int headings = 0;
    for (char *r = s; *r;) {
        if ((r == s || r[-1] == '\n') && r[0] == '#' && r[1] == ' ' && ++headings == 2)
            break;
        if (r[0] == '*' && r[1] == '*') {
            r += 2;
            continue;
        }
        if (*r == '`' || *r == '#' || *r == '\r') {
            r++;
            continue;
        }
        if (*r == '[') { /* [text](url) -> text */
            char *close = strchr(r, ']');
            if (close && close[1] == '(' && strchr(close, ')')) {
                memmove(w, r + 1, (size_t)(close - r - 1));
                w += close - r - 1;
                r = strchr(close, ')') + 1;
                continue;
            }
        }
        *w++ = *r++;
    }
    *w = '\0';
    tm_trim(s);
}

int tm_update_pick(const char *json, size_t len, const char *current, int allow_prerelease, TmRelease *out)
{
    jsmn_parser ps;
    jsmn_init(&ps);
    int n = jsmn_parse(&ps, json, len, NULL, 0);
    if (n <= 0)
        return -1;
    Json j = {json, calloc((size_t)n, sizeof(jsmntok_t)), n};
    if (!j.t)
        return -1;
    jsmn_init(&ps);
    if (jsmn_parse(&ps, json, len, j.t, (unsigned)n) != n || j.t[0].type != JSMN_ARRAY) {
        free(j.t);
        return -1;
    }
    int found = 0;
    TmRelease best;
    memset(&best, 0, sizeof best);
    int i = 1;
    for (int r = 0; r < j.t[0].size; r++) {
        int obj = i, next = skip(&j, i);
        TmRelease c;
        memset(&c, 0, sizeof c);
        int draft = 0, body = -1, assets = -1;
        if (j.t[obj].type == JSMN_OBJECT) {
            for (int k = obj + 1; k < next;) {
                int val = k + 1;
                if (tok_is(&j, k, "tag_name"))
                    tok_str(&j, val, c.tag, sizeof c.tag);
                else if (tok_is(&j, k, "draft"))
                    draft = tok_true(&j, val);
                else if (tok_is(&j, k, "prerelease"))
                    c.prerelease = tok_true(&j, val);
                else if (tok_is(&j, k, "body"))
                    body = val;
                else if (tok_is(&j, k, "assets") && j.t[val].type == JSMN_ARRAY)
                    assets = val;
                k = skip(&j, val);
            }
        }
        i = next;
        if (draft || !c.tag[0] || (c.prerelease && !allow_prerelease))
            continue;
        tm_strlcpy(c.version, c.tag[0] == 'v' || c.tag[0] == 'V' ? c.tag + 1 : c.tag, sizeof c.version);
        int ok = c.version[0] >= '0' && c.version[0] <= '9';
        for (const char *v = c.version; *v && ok; v++) /* goes into file names and status files */
            ok = (*v >= '0' && *v <= '9') || (*v >= 'a' && *v <= 'z') || (*v >= 'A' && *v <= 'Z') || *v == '.' || *v == '-';
        if (!ok)
            continue;
        if (tm_version_cmp(c.version, current) <= 0 || (found && tm_version_cmp(c.version, best.version) <= 0))
            continue;
        char want_pkg[96], want_sha[96];
        snprintf(want_pkg, sizeof want_pkg, "TriMux-%s-update.tar.gz", c.version);
        snprintf(want_sha, sizeof want_sha, "TriMux-%s-brickpro.sha256", c.version);
        for (int a = assets >= 0 ? assets + 1 : 0, cnt = 0; assets >= 0 && cnt < j.t[assets].size; cnt++) {
            int aend = skip(&j, a);
            char name[96] = "", url[512] = "";
            long size = 0;
            for (int k = a + 1; k < aend;) {
                int val = k + 1;
                if (tok_is(&j, k, "name"))
                    tok_str(&j, val, name, sizeof name);
                else if (tok_is(&j, k, "browser_download_url"))
                    tok_str(&j, val, url, sizeof url);
                else if (tok_is(&j, k, "size"))
                    size = strtol(json + j.t[val].start, NULL, 10);
                k = skip(&j, val);
            }
            if (strcmp(name, want_pkg) == 0 && strncmp(url, "https://", 8) == 0) {
                tm_strlcpy(c.pkg_name, name, sizeof c.pkg_name);
                tm_strlcpy(c.pkg_url, url, sizeof c.pkg_url);
                c.pkg_size = size;
            } else if (strcmp(name, want_sha) == 0 && strncmp(url, "https://", 8) == 0) {
                tm_strlcpy(c.sha_url, url, sizeof c.sha_url);
            }
            a = aend;
        }
        if (!c.pkg_url[0] || !c.sha_url[0])
            continue; /* releases from before the online updater */
        if (body >= 0) {
            tok_str(&j, body, c.notes, sizeof c.notes);
            clean_notes(c.notes);
        }
        best = c;
        found = 1;
    }
    free(j.t);
    if (found)
        *out = best;
    return found;
}

int tm_update_sha_lookup(const char *text, const char *name, char hex[65])
{
    char *copy = strdup(text ? text : "");
    char *save = NULL;
    int rc = -1;
    for (char *l = copy ? strtok_r(copy, "\n", &save) : NULL; l; l = strtok_r(NULL, "\n", &save)) {
        char *sp = strchr(l, ' ');
        if (!sp || sp - l != 64)
            continue;
        char *fname = tm_trim(sp);
        if (*fname == '*')
            fname++; /* binary-mode marker */
        if (strcmp(fname, name) != 0)
            continue;
        for (int i = 0; i < 64; i++) {
            char c = l[i];
            hex[i] = (char)(c >= 'A' && c <= 'F' ? c - 'A' + 'a' : c);
            if (!((hex[i] >= '0' && hex[i] <= '9') || (hex[i] >= 'a' && hex[i] <= 'f')))
                goto out;
        }
        hex[64] = '\0';
        rc = 0;
        break;
    }
out:
    free(copy);
    return rc;
}

/* ------------------------------------------------------------ status */

static int tmp_path(const TmPaths *p, const char *name, char *out, size_t size)
{
    return tm_path_join(out, size, p->tmp, name);
}

static void status_set(const TmPaths *p, const char *state, const char *version, const char *error, int percent)
{
    char path[TM_PATH_MAX], buf[256];
    int n = snprintf(buf, sizeof buf, "state=%s\nversion=%s\nerror=%s\npercent=%d\n", state, version ? version : "",
                     error ? error : "", percent);
    if (n > 0 && tmp_path(p, "update.status", path, sizeof path) == 0)
        tm_atomic_write(path, buf, (size_t)n);
}

int tm_update_status_read(const TmPaths *p, TmUpdateStatus *st)
{
    memset(st, 0, sizeof *st);
    char path[TM_PATH_MAX];
    TmIni ini;
    tm_ini_init(&ini);
    if (tmp_path(p, "update.status", path, sizeof path) != 0 || !tm_file_exists(path))
        return -1;
    char *txt = tm_read_file(path, 4096, NULL);
    int rc = txt ? tm_ini_parse(&ini, txt, strlen(txt)) : -1;
    free(txt);
    if (rc == 0) {
        tm_strlcpy(st->state, tm_ini_get(&ini, "", "state", ""), sizeof st->state);
        tm_strlcpy(st->version, tm_ini_get(&ini, "", "version", ""), sizeof st->version);
        tm_strlcpy(st->error, tm_ini_get(&ini, "", "error", ""), sizeof st->error);
        st->percent = (int)tm_ini_get_long(&ini, "", "percent", 0);
    }
    tm_ini_free(&ini);
    return rc;
}

static void info_write(const TmPaths *p, const TmRelease *r)
{
    TmIni ini;
    tm_ini_init(&ini);
    tm_ini_set(&ini, "release", "tag", r->tag);
    tm_ini_set(&ini, "release", "version", r->version);
    tm_ini_set_long(&ini, "release", "prerelease", r->prerelease);
    tm_ini_set(&ini, "release", "pkg_name", r->pkg_name);
    tm_ini_set(&ini, "release", "pkg_url", r->pkg_url);
    tm_ini_set_long(&ini, "release", "pkg_size", r->pkg_size);
    tm_ini_set(&ini, "release", "sha_url", r->sha_url);
    char path[TM_PATH_MAX];
    if (tmp_path(p, "update.ini", path, sizeof path) == 0)
        tm_ini_save(&ini, path);
    tm_ini_free(&ini);
    if (tmp_path(p, "update-notes.txt", path, sizeof path) == 0)
        tm_atomic_write(path, r->notes, strlen(r->notes));
}

int tm_update_info_read(const TmPaths *p, TmRelease *r)
{
    memset(r, 0, sizeof *r);
    char path[TM_PATH_MAX];
    TmIni ini;
    tm_ini_init(&ini);
    if (tmp_path(p, "update.ini", path, sizeof path) != 0 || tm_ini_load(&ini, path) != 0) {
        tm_ini_free(&ini);
        return -1;
    }
    tm_strlcpy(r->tag, tm_ini_get(&ini, "release", "tag", ""), sizeof r->tag);
    tm_strlcpy(r->version, tm_ini_get(&ini, "release", "version", ""), sizeof r->version);
    r->prerelease = (int)tm_ini_get_long(&ini, "release", "prerelease", 0);
    tm_strlcpy(r->pkg_name, tm_ini_get(&ini, "release", "pkg_name", ""), sizeof r->pkg_name);
    tm_strlcpy(r->pkg_url, tm_ini_get(&ini, "release", "pkg_url", ""), sizeof r->pkg_url);
    r->pkg_size = tm_ini_get_long(&ini, "release", "pkg_size", 0);
    tm_strlcpy(r->sha_url, tm_ini_get(&ini, "release", "sha_url", ""), sizeof r->sha_url);
    tm_ini_free(&ini);
    if (tmp_path(p, "update-notes.txt", path, sizeof path) == 0) {
        char *n = tm_read_file(path, sizeof r->notes, NULL);
        if (n)
            tm_strlcpy(r->notes, n, sizeof r->notes);
        free(n);
    }
    return r->version[0] && r->pkg_url[0] ? 0 : -1;
}

int tm_update_running(const TmPaths *p)
{
    char path[TM_PATH_MAX];
    long pid = 0;
    if (tmp_path(p, "update.pid", path, sizeof path) != 0 || tm_read_long(path, &pid) != 0 || pid <= 1)
        return 0;
    return kill((pid_t)pid, 0) == 0;
}

int tm_update_current(const TmPaths *p, char *out, size_t size)
{
    char path[TM_PATH_MAX];
    if (tm_path_join(path, sizeof path, p->sys, "VERSION") != 0 || tm_read_line(path, out, size) != 0) {
        tm_strlcpy(out, "0", size);
        return -1;
    }
    return 0;
}

static int sd_child(const TmPaths *p, const char *name, char *out, size_t size)
{
    return tm_path_join(out, size, p->sd, name);
}

int tm_update_has_backup(const TmPaths *p)
{
    char a[TM_PATH_MAX], b[TM_PATH_MAX];
    return sd_child(p, "TriMux.old", a, sizeof a) == 0 && sd_child(p, "trimui.old", b, sizeof b) == 0 &&
           tm_dir_exists(a) && tm_dir_exists(b);
}

/* ------------------------------------------------------------ helpers */

static int rm_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st, (void)flag, (void)ftw;
    return remove(path);
}

/* Recursive delete, only for the updater's own folders at the card root. */
static int rmtree_own(const TmPaths *p, const char *name)
{
    static const char *const allowed[] = {"TriMux.old", "trimui.old", ".trimux-new", "TriMux.swap",
                                         "trimui.swap", "TriMux.bad", "trimui.bad"};
    int ok = 0;
    for (size_t i = 0; i < TM_ARRAY_LEN(allowed); i++)
        ok |= strcmp(name, allowed[i]) == 0;
    char path[TM_PATH_MAX];
    if (!ok || sd_child(p, name, path, sizeof path) != 0)
        return -1;
    if (!tm_file_exists(path) && !tm_dir_exists(path))
        return 0;
    return nftw(path, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
}

static int wifi_ready(void)
{
    TmWifiStatus st;
    return tm_wifi_running() && tm_wifi_status(&st) == 0 && strcmp(st.state, "COMPLETED") == 0 && st.ip[0];
}

static void ca_file(const TmPaths *p, char *out, size_t size) { tm_path_join(out, size, p->share, "cacert.pem"); }

static void pid_begin(const TmPaths *p)
{
    char path[TM_PATH_MAX], buf[32];
    tm_mkdir_p(p->tmp);
    snprintf(buf, sizeof buf, "%d\n", (int)getpid());
    if (tmp_path(p, "update.pid", path, sizeof path) == 0)
        tm_atomic_write(path, buf, strlen(buf));
}

static void pid_end(const TmPaths *p)
{
    char path[TM_PATH_MAX];
    if (tmp_path(p, "update.pid", path, sizeof path) == 0)
        unlink(path);
}

static void repo_name(const TmPaths *p, char *out, size_t size)
{
    char path[TM_PATH_MAX];
    TmIni ini;
    tm_ini_init(&ini);
    tm_strlcpy(out, DEFAULT_REPO, size);
    if (tm_path_join(path, sizeof path, p->share, "update.ini") == 0 && tm_ini_load(&ini, path) == 0) {
        const char *r = tm_ini_get(&ini, "update", "repo", DEFAULT_REPO);
        int ok = strlen(r) < 100 && strchr(r, '/') != NULL;
        for (const char *c = r; *c && ok; c++)
            ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
                 strchr("-_./", *c);
        if (ok)
            tm_strlcpy(out, r, size);
    }
    tm_ini_free(&ini);
}

/* ------------------------------------------------------------ check */

int tm_update_check(const TmPaths *p, int allow_prerelease, int wait_wifi_s, TmRelease *out)
{
    char current[32], repo[128], url[256], json[TM_PATH_MAX], ca[TM_PATH_MAX];
    memset(out, 0, sizeof *out);
    tm_update_current(p, current, sizeof current);
    pid_begin(p);
    status_set(p, "checking", NULL, NULL, 0);
    for (int waited = 0; !wifi_ready(); waited++) {
        if (waited >= wait_wifi_s) {
            status_set(p, "error", NULL, "nowifi", 0);
            pid_end(p);
            return -1;
        }
        sleep(1);
    }
    repo_name(p, repo, sizeof repo);
    snprintf(url, sizeof url, "https://api.github.com/repos/%s/releases?per_page=15", repo);
    tmp_path(p, "releases.json", json, sizeof json);
    ca_file(p, ca, sizeof ca);
    int rc = tm_https_get(url, json, ca, MAX_JSON, 30, "application/vnd.github+json");
    if (rc != 0) {
        LOGW("update: could not reach %s (curl %d)", url, rc);
        status_set(p, "error", NULL, "network", 0);
        pid_end(p);
        return -1;
    }
    size_t len = 0;
    char *txt = tm_read_file(json, MAX_JSON, &len);
    unlink(json);
    int found = txt ? tm_update_pick(txt, len, current, allow_prerelease, out) : -1;
    free(txt);
    if (found < 0) {
        status_set(p, "error", NULL, "badreply", 0);
    } else if (found == 0) {
        status_set(p, "uptodate", current, NULL, 0);
        LOGI("update: %s is the newest version", current);
    } else {
        info_write(p, out);
        status_set(p, "available", out->version, NULL, 0);
        LOGI("update: %s available (installed %s)", out->version, current);
    }
    pid_end(p);
    return found < 0 ? -1 : found;
}

/* ------------------------------------------------------------ install */

static int fail(const TmPaths *p, const char *version, const char *why)
{
    LOGW("update: %s failed: %s", version, why);
    status_set(p, "error", version, why, 0);
    rmtree_own(p, ".trimux-new");
    pid_end(p);
    return -1;
}

/* Download with progress: curl runs detached while the file size is watched. */
static int download_package(const TmPaths *p, const TmRelease *r, const char *dst)
{
    char ca[TM_PATH_MAX], curl[TM_PATH_MAX], maxsize[24];
    ca_file(p, ca, sizeof ca);
    if (tm_fw_path(curl, sizeof curl, "/usr/bin/curl") != 0)
        return -1;
    snprintf(maxsize, sizeof maxsize, "%ld", MAX_PACKAGE);
    unlink(dst);
    char *argv[] = {curl, "-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-time", "900",
                    "--connect-timeout", "15", "--max-filesize", maxsize, "--cacert", ca, "-o", (char *)dst,
                    (char *)r->pkg_url, NULL};
    pid_t pid = tm_spawn(argv, "/", NULL);
    if (pid <= 0)
        return -1;
    for (int t = 0; t < 960 * 2 && kill(pid, 0) == 0; t++) {
        struct stat st;
        int pct = 0;
        if (r->pkg_size > 0 && stat(dst, &st) == 0)
            pct = (int)(st.st_size * 100 / r->pkg_size);
        status_set(p, "downloading", r->version, NULL, pct > 100 ? 100 : pct);
        usleep(500000);
    }
    if (kill(pid, 0) == 0) {
        kill(pid, SIGKILL);
        return -1;
    }
    return 0; /* success is decided by the checksum */
}

static int check_tree(const TmPaths *p, const char *stage, const char *version)
{
    static const char *const need[] = {"TriMux/bin/trimuxctl", "TriMux/bin/trimux-ui", "TriMux/scripts/supervisor.sh",
                                       "TriMux/share/systems.ini", "trimui/app/MainUI", "trimui/app/preload.sh"};
    char path[TM_PATH_MAX], v[32];
    (void)p;
    for (size_t i = 0; i < TM_ARRAY_LEN(need); i++)
        if (tm_path_join(path, sizeof path, stage, need[i]) != 0 || !tm_file_exists(path))
            return -1;
    if (tm_path_join(path, sizeof path, stage, "TriMux/VERSION") != 0 || tm_read_line(path, v, sizeof v) != 0)
        return -1;
    return strcmp(v, version) == 0 ? 0 : -1;
}

int tm_update_install(const TmPaths *p, const TmRelease *r)
{
    char current[32];
    tm_update_current(p, current, sizeof current);
    if (tm_update_running(p))
        return 1;
    pid_begin(p);
    setpriority(PRIO_PROCESS, 0, 5);
    if (tm_version_cmp(r->version, current) <= 0)
        return fail(p, r->version, "notnewer");
    /* never start what cannot finish */
    int bat = -1, chg = -1;
    tm_battery_read(&bat, &chg);
    if (bat >= 0 && bat < 30 && chg != 1)
        return fail(p, r->version, "battery");
    struct statvfs vs;
    unsigned long long need = (unsigned long long)(r->pkg_size > 0 ? r->pkg_size : 40L << 20) * 6 + (64ULL << 20);
    if (statvfs(p->sd, &vs) == 0 && (unsigned long long)vs.f_bavail * vs.f_frsize < need)
        return fail(p, r->version, "space");
    if (!wifi_ready())
        return fail(p, r->version, "nowifi");

    char pkg[TM_PATH_MAX], sha[TM_PATH_MAX], ca[TM_PATH_MAX], stage[TM_PATH_MAX];
    tmp_path(p, "update.tar.gz", pkg, sizeof pkg);
    tmp_path(p, "update.sha256", sha, sizeof sha);
    ca_file(p, ca, sizeof ca);
    sd_child(p, ".trimux-new", stage, sizeof stage);

    status_set(p, "downloading", r->version, NULL, 0);
    if (tm_https_get(r->sha_url, sha, ca, 64 * 1024, 60, NULL) != 0)
        return fail(p, r->version, "network");
    char *shatxt = tm_read_file(sha, 64 * 1024, NULL), want[65], got[65];
    int ok = shatxt && tm_update_sha_lookup(shatxt, r->pkg_name, want) == 0;
    free(shatxt);
    unlink(sha);
    if (!ok)
        return fail(p, r->version, "checksum");
    struct stat pst;
    if (download_package(p, r, pkg) != 0 || stat(pkg, &pst) != 0 || pst.st_size == 0) {
        unlink(pkg);
        return fail(p, r->version, "network");
    }
    status_set(p, "installing", r->version, NULL, 0);
    if (tm_sha256_file(pkg, got) != 0 || strcmp(got, want) != 0) {
        unlink(pkg);
        return fail(p, r->version, "checksum");
    }
    LOGI("update: %s verified (sha256 %s)", r->pkg_name, got);

    /* extract next to the current version */
    rmtree_own(p, ".trimux-new");
    if (tm_mkdir_p(stage) != 0)
        return fail(p, r->version, "write");
    char bb[TM_PATH_MAX];
    tm_fw_path(bb, sizeof bb, "/bin/busybox");
    char *argv[] = {bb, "tar", "-xzf", pkg, "-C", stage, NULL};
    int rc = tm_run(argv, NULL, 0, 600000);
    unlink(pkg);
    if (rc != 0 || check_tree(p, stage, r->version) != 0)
        return fail(p, r->version, "package");

    /* swap: current -> .old, new -> current (each step undone on failure) */
    char cur_tm[TM_PATH_MAX], cur_ui[TM_PATH_MAX], old_tm[TM_PATH_MAX], old_ui[TM_PATH_MAX], new_tm[TM_PATH_MAX],
        new_ui[TM_PATH_MAX], readme[TM_PATH_MAX], new_readme[TM_PATH_MAX];
    sd_child(p, "TriMux", cur_tm, sizeof cur_tm);
    sd_child(p, "trimui", cur_ui, sizeof cur_ui);
    sd_child(p, "TriMux.old", old_tm, sizeof old_tm);
    sd_child(p, "trimui.old", old_ui, sizeof old_ui);
    tm_path_join(new_tm, sizeof new_tm, stage, "TriMux");
    tm_path_join(new_ui, sizeof new_ui, stage, "trimui");
    sd_child(p, "LEIA-ME.txt", readme, sizeof readme);
    tm_path_join(new_readme, sizeof new_readme, stage, "LEIA-ME.txt");
    rmtree_own(p, "TriMux.old");
    rmtree_own(p, "trimui.old");
    rmtree_own(p, "TriMux.bad"); /* left by an automatic rollback */
    rmtree_own(p, "trimui.bad");
    if (rename(cur_tm, old_tm) != 0)
        return fail(p, r->version, "write");
    if (rename(new_tm, cur_tm) != 0) {
        rename(old_tm, cur_tm);
        return fail(p, r->version, "write");
    }
    if (rename(cur_ui, old_ui) != 0 || rename(new_ui, cur_ui) != 0) {
        if (!tm_dir_exists(cur_ui))
            rename(old_ui, cur_ui);
        rename(cur_tm, new_tm);
        rename(old_tm, cur_tm);
        return fail(p, r->version, "write");
    }
    if (tm_file_exists(new_readme))
        tm_copy_file(new_readme, readme);
    rmtree_own(p, ".trimux-new");

    /* trimui/app/MainUI goes back to TriMux.old if this version never reaches the menu */
    char pend[TM_PATH_MAX], tries[TM_PATH_MAX];
    tm_mkdir_p(p->state);
    tm_path_join(pend, sizeof pend, p->state, "update_pending");
    tm_path_join(tries, sizeof tries, p->state, "update_tries");
    tm_atomic_write(pend, current, strlen(current));
    tm_atomic_write(tries, "0\n", 2);
    sync();
    status_set(p, "ready", r->version, NULL, 100);
    LOGI("update: installed %s (previous %s kept as TriMux.old)", r->version, current);
    pid_end(p);
    return 0;
}

static void clear_pending(const TmPaths *p)
{
    char path[TM_PATH_MAX];
    if (tm_path_join(path, sizeof path, p->state, "update_pending") == 0)
        unlink(path);
    if (tm_path_join(path, sizeof path, p->state, "update_tries") == 0)
        unlink(path);
}

/* ------------------------------------------------------------ rollback */

static int swap_dirs(const TmPaths *p, const char *cur, const char *old, const char *tmp)
{
    char a[TM_PATH_MAX], b[TM_PATH_MAX], t[TM_PATH_MAX];
    sd_child(p, cur, a, sizeof a);
    sd_child(p, old, b, sizeof b);
    sd_child(p, tmp, t, sizeof t);
    rmtree_own(p, tmp);
    if (rename(a, t) != 0)
        return -1;
    if (rename(b, a) != 0) {
        rename(t, a);
        return -1;
    }
    return rename(t, b);
}

int tm_update_rollback(const TmPaths *p)
{
    char current[32];
    tm_update_current(p, current, sizeof current);
    if (!tm_update_has_backup(p))
        return -1;
    if (swap_dirs(p, "TriMux", "TriMux.old", "TriMux.swap") != 0)
        return -1;
    if (swap_dirs(p, "trimui", "trimui.old", "trimui.swap") != 0) {
        swap_dirs(p, "TriMux", "TriMux.old", "TriMux.swap"); /* undo */
        return -1;
    }
    clear_pending(p);
    sync();
    LOGI("update: went back from %s to the previous version", current);
    return 0;
}

void tm_update_confirm(const TmPaths *p)
{
    /* update_tries is raised by trimui/app/MainUI: while it is still 0 the new
     * version has not been started yet (the menu running now is the old one,
     * or the new menu was started by the old supervisor), so it proves nothing */
    char path[TM_PATH_MAX];
    long tries = 0;
    if (tm_path_join(path, sizeof path, p->state, "update_pending") != 0 || !tm_file_exists(path))
        return;
    if (tm_path_join(path, sizeof path, p->state, "update_tries") == 0 && tm_read_long(path, &tries) == 0 && tries < 1)
        return;
    clear_pending(p);
    LOGI("update: new version confirmed");
}
