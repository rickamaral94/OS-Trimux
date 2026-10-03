#define _GNU_SOURCE
#include "store.h"
#include "ini.h"
#include "log.h"
#include "net.h"
#include "sha256.h"
#include "sysinfo.h"
#include "unzip.h"
#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <ftw.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define MAX_STORE_PACKAGE (400L << 20)

/* Card paths the store may write: never games, saves, BIOS or TriMux. */
static int rel_ok(const char *rel)
{
    static const char *const roots[] = {"Apps/", "Emus/", "Roms/PORTS/"};
    if (!rel || !*rel || rel[0] == '/' || strstr(rel, "..") || strchr(rel, '\\'))
        return 0;
    for (size_t i = 0; i < TM_ARRAY_LEN(roots); i++)
        if (strncmp(rel, roots[i], strlen(roots[i])) == 0 && rel[strlen(roots[i])])
            return 1;
    return strcmp(rel, "Apps") == 0;
}

static int sha_ok(const char *h)
{
    if (strlen(h) != 64)
        return 0;
    for (; *h; h++)
        if (!isxdigit((unsigned char)*h) || isupper((unsigned char)*h))
            return 0;
    return 1;
}

size_t tm_store_load(const char *ini_path, TmStoreItem *out, size_t max)
{
    TmIni ini;
    tm_ini_init(&ini);
    if (tm_ini_load(&ini, ini_path) != 0) {
        tm_ini_free(&ini);
        return 0;
    }
    const char *names[64];
    size_t ns = tm_ini_sections(&ini, names, TM_ARRAY_LEN(names)), n = 0;
    for (size_t i = 0; i < ns && n < max; i++) {
        const char *id = names[i];
        TmStoreItem *it = &out[n];
        memset(it, 0, sizeof *it);
        if (!*id || !tm_name_is_safe(id) || tm_strlcpy(it->id, id, sizeof it->id) != 0)
            continue;
        tm_strlcpy(it->name, tm_ini_get(&ini, id, "name", id), sizeof it->name);
        tm_strlcpy(it->desc_key, tm_ini_get(&ini, id, "desc", ""), sizeof it->desc_key);
        tm_strlcpy(it->version, tm_ini_get(&ini, id, "version", ""), sizeof it->version);
        tm_strlcpy(it->url, tm_ini_get(&ini, id, "url", ""), sizeof it->url);
        tm_strlcpy(it->sha256, tm_ini_get(&ini, id, "sha256", ""), sizeof it->sha256);
        it->size = tm_ini_get_long(&ini, id, "size", 0);
        tm_strlcpy(it->license, tm_ini_get(&ini, id, "license", ""), sizeof it->license);
        tm_strlcpy(it->source, tm_ini_get(&ini, id, "source", ""), sizeof it->source);
        tm_strlcpy(it->dest, tm_ini_get(&ini, id, "dest", ""), sizeof it->dest);
        tm_strlcpy(it->installed, tm_ini_get(&ini, id, "installed", ""), sizeof it->installed);
        tm_strlcpy(it->keep, tm_ini_get(&ini, id, "keep", ""), sizeof it->keep);
        tm_strlcpy(it->remove, tm_ini_get(&ini, id, "remove", ""), sizeof it->remove);
        tm_strlcpy(it->touch, tm_ini_get(&ini, id, "touch", ""), sizeof it->touch);
        it->system = (int)tm_ini_get_long(&ini, id, "system", 0);
        int ok = strncmp(it->url, "https://", 8) == 0 && !strchr(it->url, ' ') && sha_ok(it->sha256) &&
                 it->size > 0 && it->size <= MAX_STORE_PACKAGE && rel_ok(it->dest) && it->installed[0] &&
                 !strstr(it->installed, "..") && it->installed[0] != '/' && (!it->touch[0] || rel_ok(it->touch));
        char rm[256];
        tm_strlcpy(rm, it->remove, sizeof rm);
        for (char *s = NULL, *r = strtok_r(rm, "|", &s); r && ok; r = strtok_r(NULL, "|", &s))
            ok = rel_ok(tm_trim(r)) && strchr(tm_trim(r), '/') != NULL;
        if (ok)
            n++;
        else
            LOGW("store: entry %s rejected", id);
    }
    tm_ini_free(&ini);
    return n;
}

const TmStoreItem *tm_store_find(const TmStoreItem *items, size_t n, const char *id)
{
    for (size_t i = 0; id && i < n; i++)
        if (strcmp(items[i].id, id) == 0)
            return &items[i];
    return NULL;
}

static int card_path(const TmPaths *p, const char *rel, char *out, size_t size)
{
    return tm_path_join(out, size, p->sd, rel) == 0 && tm_path_is_safe_under(p->sd, out) ? 0 : -1;
}

int tm_store_installed(const TmPaths *p, const TmStoreItem *it)
{
    char dest[TM_PATH_MAX], mark[TM_PATH_MAX];
    return card_path(p, it->dest, dest, sizeof dest) == 0 &&
           tm_path_join(mark, sizeof mark, dest, it->installed) == 0 && tm_file_exists(mark);
}

/* ---- status and lock (/tmp/trimux/store.status, store.pid) ---- */

static void status_set(const TmPaths *p, const char *state, const char *id, const char *err, int pct)
{
    char path[TM_PATH_MAX], buf[256];
    tm_mkdir_p(p->tmp);
    int n = snprintf(buf, sizeof buf, "[store]\nstate = %s\nid = %s\nerror = %s\npercent = %d\n", state, id,
                     err ? err : "", pct);
    if (tm_path_join(path, sizeof path, p->tmp, "store.status") == 0 && n > 0)
        tm_atomic_write(path, buf, (size_t)n);
}

int tm_store_status_read(const TmPaths *p, TmStoreStatus *st)
{
    char path[TM_PATH_MAX];
    memset(st, 0, sizeof *st);
    TmIni ini;
    tm_ini_init(&ini);
    if (tm_path_join(path, sizeof path, p->tmp, "store.status") != 0 || !tm_file_exists(path) ||
        tm_ini_load(&ini, path) != 0) {
        tm_ini_free(&ini);
        return -1;
    }
    tm_strlcpy(st->state, tm_ini_get(&ini, "store", "state", ""), sizeof st->state);
    tm_strlcpy(st->id, tm_ini_get(&ini, "store", "id", ""), sizeof st->id);
    tm_strlcpy(st->error, tm_ini_get(&ini, "store", "error", ""), sizeof st->error);
    st->percent = (int)tm_ini_get_long(&ini, "store", "percent", 0);
    tm_ini_free(&ini);
    return 0;
}

int tm_store_running(const TmPaths *p)
{
    char path[TM_PATH_MAX], buf[32];
    if (tm_path_join(path, sizeof path, p->tmp, "store.pid") != 0 || tm_read_line(path, buf, sizeof buf) != 0)
        return 0;
    pid_t pid = (pid_t)atol(buf);
    return pid > 0 && kill(pid, 0) == 0;
}

static void pid_file(const TmPaths *p, int on)
{
    char path[TM_PATH_MAX], buf[32];
    if (tm_path_join(path, sizeof path, p->tmp, "store.pid") != 0)
        return;
    if (!on) {
        unlink(path);
        return;
    }
    tm_mkdir_p(p->tmp);
    snprintf(buf, sizeof buf, "%d\n", (int)getpid());
    tm_atomic_write(path, buf, strlen(buf));
}

/* ---- tree helpers, only below the store's own folders ---- */

static int rm_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st, (void)flag, (void)ftw;
    return remove(path);
}

static int rmtree(const char *path)
{
    if (!tm_file_exists(path) && !tm_dir_exists(path))
        return 0;
    return nftw(path, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
}

static int is_kept(const TmStoreItem *it, const char *name)
{
    char k[128];
    tm_strlcpy(k, it->keep, sizeof k);
    for (char *s = NULL, *t = strtok_r(k, "|", &s); t; t = strtok_r(NULL, "|", &s))
        if (strcmp(tm_trim(t), name) == 0)
            return 1;
    return 0;
}

/* Moves every top-level entry of stage into dest; on an update, entries in
 * "keep" that already exist (the user's settings) stay as they are. */
static int move_into(const TmStoreItem *it, const char *stage, const char *dest)
{
    if (tm_mkdir_p(dest) != 0)
        return -1;
    DIR *d = opendir(stage);
    if (!d)
        return -1;
    int rc = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && rc == 0) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char from[TM_PATH_MAX], to[TM_PATH_MAX];
        if (tm_path_join(from, sizeof from, stage, e->d_name) != 0 || tm_path_join(to, sizeof to, dest, e->d_name) != 0) {
            rc = -1;
            break;
        }
        int exists = tm_file_exists(to) || tm_dir_exists(to);
        if (exists && is_kept(it, e->d_name))
            continue;
        if (exists && rmtree(to) != 0)
            rc = -1;
        else if (rename(from, to) != 0)
            rc = -1;
    }
    closedir(d);
    return rc;
}

static int fail(const TmPaths *p, const TmStoreItem *it, const char *why, const char *zip, const char *stage)
{
    LOGW("store: %s failed: %s", it->id, why);
    if (zip)
        unlink(zip);
    if (stage)
        rmtree(stage);
    status_set(p, "error", it->id, why, 0);
    pid_file(p, 0);
    return -1;
}

static int download(const TmPaths *p, const TmStoreItem *it, const char *dst)
{
    char ca[TM_PATH_MAX], curl[TM_PATH_MAX], maxsize[24];
    tm_path_join(ca, sizeof ca, p->share, "cacert.pem");
    if (tm_fw_path(curl, sizeof curl, "/usr/bin/curl") != 0)
        return -1;
    snprintf(maxsize, sizeof maxsize, "%ld", it->size + (1L << 20));
    unlink(dst);
    char *argv[] = {curl, "-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-time", "1800",
                    "--connect-timeout", "15", "--max-filesize", maxsize, "--cacert", ca, "-o", (char *)dst,
                    (char *)it->url, NULL};
    pid_t pid = tm_spawn(argv, "/", NULL);
    if (pid <= 0)
        return -1;
    for (int t = 0; t < 1860 * 2 && kill(pid, 0) == 0; t++) {
        struct stat st;
        int pct = stat(dst, &st) == 0 ? (int)(st.st_size * 100 / it->size) : 0;
        status_set(p, "downloading", it->id, NULL, pct > 100 ? 100 : pct);
        usleep(500000);
    }
    if (kill(pid, 0) == 0) {
        kill(pid, SIGKILL);
        return -1;
    }
    return 0; /* the checksum decides */
}

int tm_store_install(const TmPaths *p, const TmStoreItem *it)
{
    if (tm_store_running(p))
        return 1;
    pid_file(p, 1);
    setpriority(PRIO_PROCESS, 0, 5);
    status_set(p, "downloading", it->id, NULL, 0);
    int bat = -1, chg = -1;
    tm_battery_read(&bat, &chg);
    if (bat >= 0 && bat < 20 && chg != 1)
        return fail(p, it, "battery", NULL, NULL);
    struct statvfs vs;
    unsigned long long need = (unsigned long long)it->size * 4 + (64ULL << 20);
    if (statvfs(p->sd, &vs) == 0 && (unsigned long long)vs.f_bavail * vs.f_frsize < need)
        return fail(p, it, "space", NULL, NULL);
    TmWifiStatus ws;
    if (!(tm_wifi_running() && tm_wifi_status(&ws) == 0 && strcmp(ws.state, "COMPLETED") == 0 && ws.ip[0]))
        return fail(p, it, "nowifi", NULL, NULL);

    /* download and extract on the card (packages can be larger than the RAM disk) */
    char zip[TM_PATH_MAX], stage[TM_PATH_MAX], dest[TM_PATH_MAX], got[65];
    if (tm_path_join(zip, sizeof zip, p->data, ".store-download.zip") != 0 ||
        tm_path_join(stage, sizeof stage, p->sd, ".trimux-store-new") != 0 || card_path(p, it->dest, dest, sizeof dest) != 0)
        return fail(p, it, "write", NULL, NULL);
    struct stat zst;
    if (download(p, it, zip) != 0 || stat(zip, &zst) != 0 || zst.st_size == 0)
        return fail(p, it, "network", zip, NULL);
    status_set(p, "installing", it->id, NULL, 0);
    if (tm_sha256_file(zip, got) != 0 || strcmp(got, it->sha256) != 0)
        return fail(p, it, "checksum", zip, NULL);
    LOGI("store: %s %s verified (sha256 %s)", it->id, it->version, got);
    rmtree(stage);
    int files = tm_unzip(zip, stage);
    unlink(zip);
    char mark[TM_PATH_MAX];
    if (files <= 0 || tm_path_join(mark, sizeof mark, stage, it->installed) != 0 || !tm_file_exists(mark))
        return fail(p, it, "package", NULL, stage);
    if (move_into(it, stage, dest) != 0)
        return fail(p, it, "write", NULL, stage);
    rmtree(stage);
    char touch[TM_PATH_MAX];
    if (it->touch[0] && card_path(p, it->touch, touch, sizeof touch) == 0 && !tm_file_exists(touch)) {
        char dir[TM_PATH_MAX];
        tm_strlcpy(dir, touch, sizeof dir);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            tm_mkdir_p(dir);
        }
        static const char marker[] = "# Opens PortMaster from the Ports list (created by TriMux).\n";
        tm_atomic_write(touch, marker, sizeof marker - 1);
    }
    sync();
    status_set(p, "done", it->id, NULL, 100);
    LOGI("store: installed %s %s (%d files) into %s", it->id, it->version, files, it->dest);
    pid_file(p, 0);
    return 0;
}

int tm_store_remove(const TmPaths *p, const TmStoreItem *it)
{
    if (tm_store_running(p))
        return 1;
    char rm[256];
    int rc = 0;
    tm_strlcpy(rm, it->remove, sizeof rm);
    for (char *s = NULL, *r = strtok_r(rm, "|", &s); r; r = strtok_r(NULL, "|", &s)) {
        char path[TM_PATH_MAX];
        r = tm_trim(r);
        if (!rel_ok(r) || card_path(p, r, path, sizeof path) != 0 || rmtree(path) != 0)
            rc = -1;
    }
    sync();
    status_set(p, rc == 0 ? "removed" : "error", it->id, rc == 0 ? NULL : "write", 0);
    LOGI("store: removed %s (%s)", it->id, rc == 0 ? "ok" : "partly");
    return rc;
}
