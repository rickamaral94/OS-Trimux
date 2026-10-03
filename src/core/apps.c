#define _GNU_SOURCE
#include "apps.h"
#include "ini.h"
#include "log.h"
#include "net.h"
#include "util.h"

#define JSMN_STATIC
#include "third_party/jsmn.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Firmware apps that must not run under TriMux: the card formatter, USB
 * mass storage (it takes the card away from the running system) and the FN
 * key editor (it rewrites the stock key setup that TriMux replaces). */
static const char *const k_blocked[] = {"zformatter_fat32", "usb_storage", "fn_editor"};

static int json_skip(const jsmntok_t *t, int n, int i)
{
    int end = i + 1;
    if (t[i].type == JSMN_OBJECT || t[i].type == JSMN_ARRAY) {
        int count = t[i].size * (t[i].type == JSMN_OBJECT ? 2 : 1);
        for (int k = 0; k < count && end < n; k++)
            end = json_skip(t, n, end);
    }
    return end;
}

/* Value of a top-level string/number key. */
static int json_get(const char *js, const jsmntok_t *t, int n, const char *key, char *out, size_t size)
{
    if (n < 1 || t[0].type != JSMN_OBJECT)
        return -1;
    for (int i = 1; i + 1 < n; i = json_skip(t, n, i + 1)) {
        int klen = t[i].end - t[i].start, v = i + 1;
        if (t[i].type != JSMN_STRING || (int)strlen(key) != klen || strncmp(js + t[i].start, key, (size_t)klen) != 0)
            continue;
        if (t[v].type != JSMN_STRING && t[v].type != JSMN_PRIMITIVE)
            return -1;
        int vlen = t[v].end - t[v].start;
        if ((size_t)vlen >= size)
            vlen = (int)size - 1;
        memcpy(out, js + t[v].start, (size_t)vlen);
        out[vlen] = '\0';
        return 0;
    }
    return -1;
}

static int name_ok(const char *s)
{
    return s[0] && s[0] != '.' && !strchr(s, '/') && !strchr(s, '\\');
}

int tm_app_load(const char *dir, const char *lang, TmApp *out)
{
    memset(out, 0, sizeof *out);
    char cfg[TM_PATH_MAX];
    if (tm_path_join(cfg, sizeof cfg, dir, "config.json") != 0)
        return -1;
    size_t len = 0;
    char *js = tm_read_file(cfg, 64 * 1024, &len);
    if (!js)
        return -1;
    jsmn_parser ps;
    jsmntok_t t[128];
    jsmn_init(&ps);
    int n = jsmn_parse(&ps, js, len, t, 128);
    int rc = -1;
    char key[48];
    if (n > 0 && json_get(js, t, n, "launch", out->launch, sizeof out->launch) == 0 && name_ok(out->launch)) {
        char script[TM_PATH_MAX];
        if (tm_path_join(script, sizeof script, dir, out->launch) == 0 && tm_file_exists(script)) {
            snprintf(key, sizeof key, "label.%s.lang", lang && lang[0] ? lang : "en");
            if (json_get(js, t, n, key, out->label, sizeof out->label) != 0 || !out->label[0])
                json_get(js, t, n, "label", out->label, sizeof out->label);
            if (!out->label[0]) {
                const char *b = strrchr(dir, '/');
                tm_strlcpy(out->label, b ? b + 1 : dir, sizeof out->label);
            }
            snprintf(key, sizeof key, "description.%s.lang", lang && lang[0] ? lang : "en");
            if (json_get(js, t, n, key, out->desc, sizeof out->desc) != 0 || !out->desc[0])
                json_get(js, t, n, "description", out->desc, sizeof out->desc);
            char ic[64] = "";
            if ((json_get(js, t, n, "icontop", ic, sizeof ic) != 0 || !ic[0]) &&
                (json_get(js, t, n, "icon", ic, sizeof ic) != 0 || !ic[0]))
                ic[0] = '\0';
            if (ic[0] && name_ok(ic) && tm_path_join(out->icon, sizeof out->icon, dir, ic) == 0 &&
                !tm_file_exists(out->icon))
                out->icon[0] = '\0';
            tm_strlcpy(out->dir, dir, sizeof out->dir);
            rc = 0;
        }
    }
    free(js);
    return rc;
}

static int blocked(const char *dir)
{
    const char *b = strrchr(dir, '/');
    b = b ? b + 1 : dir;
    for (size_t i = 0; i < TM_ARRAY_LEN(k_blocked); i++)
        if (strcmp(b, k_blocked[i]) == 0)
            return 1;
    return 0;
}

/* roots[0..2]: card, internal memory, firmware */
static int app_roots(const TmPaths *p, char roots[3][TM_PATH_MAX])
{
    if (tm_path_join(roots[0], TM_PATH_MAX, p->sd, "Apps") != 0 ||
        tm_fw_path(roots[1], TM_PATH_MAX, "/mnt/UDISK/Apps") != 0 ||
        tm_fw_path(roots[2], TM_PATH_MAX, "/usr/trimui/apps") != 0)
        return -1;
    return 0;
}

static int cmp_app(const void *a, const void *b)
{
    const TmApp *x = a, *y = b;
    int c = strcasecmp(x->label, y->label);
    return c ? c : x->builtin - y->builtin;
}

size_t tm_apps_scan(const TmPaths *p, const char *lang, TmApp *out, size_t max)
{
    char roots[3][TM_PATH_MAX];
    size_t n = 0;
    if (app_roots(p, roots) != 0)
        return 0;
    for (int r = 0; r < 3; r++) {
        DIR *d = opendir(roots[r]);
        if (!d)
            continue;
        struct dirent *e;
        while ((e = readdir(d)) && n < max) {
            if (!name_ok(e->d_name))
                continue;
            char dir[TM_PATH_MAX];
            if (tm_path_join(dir, sizeof dir, roots[r], e->d_name) != 0 || !tm_dir_exists(dir) || blocked(dir))
                continue;
            if (tm_app_load(dir, lang, &out[n]) == 0) {
                out[n].builtin = r == 2;
                n++;
            }
        }
        closedir(d);
    }
    qsort(out, n, sizeof *out, cmp_app);
    return n;
}

int tm_app_allowed(const TmPaths *p, const char *dir, TmApp *out)
{
    char roots[3][TM_PATH_MAX], real[PATH_MAX], rroot[PATH_MAX];
    if (app_roots(p, roots) != 0 || !realpath(dir, real) || blocked(real))
        return 0;
    for (int r = 0; r < 3; r++) {
        if (!realpath(roots[r], rroot))
            continue;
        size_t l = strlen(rroot);
        /* a direct child of the root, nothing deeper */
        if (strncmp(real, rroot, l) == 0 && real[l] == '/' && !strchr(real + l + 1, '/'))
            return tm_app_load(real, NULL, out) == 0 && ((out->builtin = r == 2), 1);
    }
    return 0;
}

static int request_path(const TmPaths *p, char *out, size_t size)
{
    return tm_path_join(out, size, p->tmp, "app.ini");
}

int tm_app_request_write(const TmPaths *p, const char *dir)
{
    char path[TM_PATH_MAX];
    TmIni ini;
    tm_ini_init(&ini);
    tm_ini_set(&ini, "app", "dir", dir);
    int rc = request_path(p, path, sizeof path) == 0 ? tm_ini_save(&ini, path) : -1;
    tm_ini_free(&ini);
    return rc;
}

int tm_app_request_read(const TmPaths *p, char *dir, size_t size)
{
    char path[TM_PATH_MAX];
    TmIni ini;
    tm_ini_init(&ini);
    if (request_path(p, path, sizeof path) != 0 || tm_ini_load(&ini, path) != 0) {
        tm_ini_free(&ini);
        return -1;
    }
    unlink(path); /* one request, one start */
    tm_strlcpy(dir, tm_ini_get(&ini, "app", "dir", ""), size);
    tm_ini_free(&ini);
    return dir[0] ? 0 : -1;
}
