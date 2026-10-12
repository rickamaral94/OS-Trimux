#define _GNU_SOURCE
#include "uninstall.h"
#include "apps.h"
#include "log.h"
#include "net.h"
#include "update.h"
#include "util.h"

#include <dirent.h>
#include <ftw.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

int tm_uninstall_rompath_dir(const char *emu_rel, const char *rompath, char *out, size_t size)
{
    char raw[TM_PATH_MAX], path[TM_PATH_MAX * 2];
    size_t n = 0;
    for (const char *s = rompath; *s && n + 1 < sizeof raw; s++)
        if (!(s[0] == '\\' && s[1] == '/')) /* JSON may write "\/" */
            raw[n++] = *s;
    raw[n] = '\0';
    if (!raw[0])
        return -1;
    static const char card[] = "/mnt/SDCARD/";
    if (strncmp(raw, card, sizeof card - 1) == 0)
        snprintf(path, sizeof path, "%s", raw + sizeof card - 1);
    else if (raw[0] == '/' || !emu_rel)
        return -1; /* not on the card */
    else
        snprintf(path, sizeof path, "%s/%s", emu_rel, raw);
    /* normalise "." and ".." */
    char *seg[64];
    int depth = 0;
    char *save = NULL;
    for (char *tok = strtok_r(path, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (strcmp(tok, ".") == 0)
            continue;
        if (strcmp(tok, "..") == 0) {
            if (depth == 0)
                return -1;
            depth--;
            continue;
        }
        if (depth == 64)
            return -1;
        seg[depth++] = tok;
    }
    if (depth < 2 || strcasecmp(seg[0], "Roms") != 0)
        return -1;
    return tm_strlcpy(out, seg[1], size);
}

static int is_license_name(const char *name)
{
    static const char *const pre[] = {"copying", "license", "readme"};
    for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++)
        if (strncasecmp(name, pre[i], strlen(pre[i])) == 0)
            return 1;
    return 0;
}

/* 1 when the folder holds a game-like file (two levels down at most). */
static int has_games(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int found = 0;
    struct dirent *e;
    while (!found && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || is_license_name(e->d_name))
            continue;
        char p[TM_PATH_MAX];
        struct stat st;
        if (tm_path_join(p, sizeof p, dir, e->d_name) != 0 || lstat(p, &st) != 0)
            continue;
        if (S_ISREG(st.st_mode))
            found = 1;
        else if (S_ISDIR(st.st_mode) && depth > 0)
            found = has_games(p, depth - 1);
    }
    closedir(d);
    return found;
}

static void add_read(TmUninstallCheck *c, const char *dir)
{
    for (int i = 0; i < c->nread; i++)
        if (strcasecmp(c->read[i], dir) == 0)
            return;
    if (c->nread < TM_UNINSTALL_MAX_DIRS)
        tm_strlcpy(c->read[c->nread++], dir, sizeof c->read[0]);
}

/* Stock emulators of one Emus folder (card_rel: its place on the card, or
 * NULL for the internal memory). */
static void scan_emus(TmUninstallCheck *c, const char *abs, const char *card_rel)
{
    DIR *d = opendir(abs);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char cfg[TM_PATH_MAX], emu_dir[TM_PATH_MAX], rompath[TM_PATH_MAX], rel[TM_PATH_MAX], dir[64];
        if (tm_path_join(emu_dir, sizeof emu_dir, abs, e->d_name) != 0 ||
            tm_path_join(cfg, sizeof cfg, emu_dir, "config.json") != 0 || !tm_file_exists(cfg) ||
            tm_json_file_get(cfg, "rompath", rompath, sizeof rompath) != 0)
            continue;
        c->stock_emus++;
        int ok = card_rel ? tm_snprintf(rel, sizeof rel, "%s/%s", card_rel, e->d_name) == 0 : 0;
        if (tm_uninstall_rompath_dir(ok ? rel : NULL, rompath, dir, sizeof dir) == 0)
            add_read(c, dir);
    }
    closedir(d);
}

int tm_uninstall_check(const TmPaths *p, TmUninstallCheck *c)
{
    memset(c, 0, sizeof *c);
    char abs[TM_PATH_MAX];
    if (tm_path_join(abs, sizeof abs, p->sd, "Emus") == 0)
        scan_emus(c, abs, "Emus");
    if (tm_fw_path(abs, sizeof abs, "/mnt/UDISK/Emus") == 0)
        scan_emus(c, abs, NULL);
    char roms[TM_PATH_MAX];
    if (tm_path_join(roms, sizeof roms, p->sd, "Roms") != 0)
        return -1;
    DIR *d = opendir(roms);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char sub[TM_PATH_MAX];
        struct stat st;
        if (e->d_name[0] == '.' || tm_path_join(sub, sizeof sub, roms, e->d_name) != 0 || lstat(sub, &st) != 0 ||
            !S_ISDIR(st.st_mode) || !has_games(sub, 2))
            continue;
        c->game_dirs++;
        int read = 0;
        for (int i = 0; i < c->nread && !read; i++)
            read = strcasecmp(c->read[i], e->d_name) == 0;
        if (!read && c->nunread < TM_UNINSTALL_MAX_DIRS)
            tm_strlcpy(c->unread[c->nunread++], e->d_name, sizeof c->unread[0]);
    }
    closedir(d);
    return 0;
}

static int rm_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st, (void)flag, (void)ftw;
    if (remove(path) != 0) {
        LOGW("uninstall: could not remove %s", path);
        return 0; /* keep going: remove as much as possible */
    }
    return 0;
}

int tm_uninstall_run(const TmPaths *p)
{
    /* the start hook first: once trimui/ is gone the stock interface opens,
     * even if the rest is interrupted */
    static const char *const order[] = {"trimui", "TriMux.old", "trimui.old", "TriMux"};
    if (tm_update_running(p)) {
        LOGW("uninstall: refused, an update is running");
        return -1;
    }
    int rc = 0;
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
        char abs[TM_PATH_MAX];
        struct stat st;
        if (tm_path_join(abs, sizeof abs, p->sd, order[i]) != 0)
            return -1;
        if (lstat(abs, &st) != 0)
            continue; /* not there */
        if (!S_ISDIR(st.st_mode)) {
            rc = -1; /* a file or link with that name is not ours to remove */
            continue;
        }
        nftw(abs, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
        if (lstat(abs, &st) == 0) {
            LOGE("uninstall: %s is still on the card", order[i]);
            rc = -1;
        } else {
            LOGI("uninstall: removed %s", order[i]);
        }
    }
    sync();
    return rc;
}
