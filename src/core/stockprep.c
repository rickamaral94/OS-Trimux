#define _GNU_SOURCE
#include "stockprep.h"
#include "apps.h"
#include "consoleart.h"
#include "image.h"
#include "log.h"
#include "net.h"
#include "uninstall.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

/* ------------------------------------------------------------ icons */

int tm_stock_icon(const char *path, const char *sys_id, unsigned rgb)
{
    enum { W = 300 };
    unsigned char *px = calloc(1, W * W * 4);
    if (!px)
        return -1;
    tm_console_art(px, W, sys_id, rgb);
    int rc = tm_image_write_png(path, W, W, px);
    free(px);
    return rc;
}

/* ------------------------------------------------------------ plan */

typedef struct {
    char dir[80];  /* Emus/<name> */
    char read[64]; /* Roms folder it reads */
    int ours;
    int current; /* ours, written by this version of the files (icons, launch.sh) */
} Emu;

/* Emulators of one Emus folder; card_rel NULL for the internal memory. */
static int scan_emus(const char *abs, const char *card_rel, Emu *out, int max, int n)
{
    DIR *d = opendir(abs);
    if (!d)
        return n;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (e->d_name[0] == '.')
            continue;
        char emu[TM_PATH_MAX], cfg[TM_PATH_MAX], mark[TM_PATH_MAX], rompath[TM_PATH_MAX], rel[TM_PATH_MAX];
        if (tm_path_join(emu, sizeof emu, abs, e->d_name) != 0 || tm_path_join(cfg, sizeof cfg, emu, "config.json") != 0 ||
            !tm_file_exists(cfg) || tm_json_file_get(cfg, "rompath", rompath, sizeof rompath) != 0)
            continue;
        Emu *m = &out[n];
        memset(m, 0, sizeof *m);
        int ok = card_rel && tm_snprintf(rel, sizeof rel, "%s/%s", card_rel, e->d_name) == 0;
        if (tm_uninstall_rompath_dir(ok ? rel : NULL, rompath, m->read, sizeof m->read) != 0)
            m->read[0] = '\0';
        snprintf(m->dir, sizeof m->dir, "%s", ok ? rel : e->d_name);
        m->ours = card_rel && tm_path_join(mark, sizeof mark, emu, TM_STOCK_MARKER) == 0 && tm_file_exists(mark);
        if (m->ours) {
            char *text = tm_read_file(mark, 256, NULL);
            m->current = text && strstr(text, TM_STOCK_FILES) != NULL;
            free(text);
        }
        n++;
    }
    closedir(d);
    return n;
}

/* Platform of a Roms folder: one of its folder names, or "Anything (TAG)". */
static const TmSystem *system_of(const TmCatalog *cat, const char *dir)
{
    char tag[64] = "";
    const char *open = strrchr(dir, '(');
    size_t len = strlen(dir);
    if (open && len > 0 && dir[len - 1] == ')' && (size_t)(dir + len - 1 - open - 1) < sizeof tag) {
        memcpy(tag, open + 1, (size_t)(dir + len - 1 - open - 1));
        tag[dir + len - 1 - open - 1] = '\0';
    }
    for (size_t i = 0; i < cat->nsystems; i++)
        for (int f = 0; f < cat->systems[i].nfolders; f++)
            if (strcasecmp(cat->systems[i].folders[f], dir) == 0 || (tag[0] && strcasecmp(cat->systems[i].folders[f], tag) == 0))
                return &cat->systems[i];
    return NULL;
}

/* Bytes still to copy for src -> dst (0 when dst already has the same size). */
static unsigned long long copy_need(const char *src, const char *dst)
{
    struct stat a, b;
    if (stat(src, &a) != 0)
        return 0;
    if (stat(dst, &b) == 0 && a.st_size == b.st_size)
        return 0;
    return (unsigned long long)a.st_size;
}

static const char *g_tree_dst;
static const char *g_tree_src;
static unsigned long long g_tree_need;
static int tree_need_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)ftw;
    if (flag != FTW_F)
        return 0;
    char dst[TM_PATH_MAX];
    if (tm_snprintf(dst, sizeof dst, "%s%s", g_tree_dst, path + strlen(g_tree_src)) == 0)
        g_tree_need += copy_need(path, dst);
    (void)st;
    return 0;
}

static unsigned long long tree_need(const char *src, const char *dst)
{
    if (!tm_dir_exists(src))
        return 0;
    g_tree_src = src;
    g_tree_dst = dst;
    g_tree_need = 0;
    nftw(src, tree_need_entry, 16, FTW_PHYS);
    return g_tree_need;
}

static int runtime_paths(const TmPaths *p, char *rt, size_t size)
{
    return tm_path_join(rt, size, p->sd, TM_STOCK_RUNTIME);
}

int tm_stock_plan(const TmPaths *p, const TmCatalog *cat, const TmIni *settings, TmStockPlan *out)
{
    memset(out, 0, sizeof *out);
    static Emu emus[96];
    int ne = 0;
    char abs[TM_PATH_MAX], rt[TM_PATH_MAX];
    if (tm_path_join(abs, sizeof abs, p->sd, "Emus") == 0)
        ne = scan_emus(abs, "Emus", emus, 96, ne);
    if (tm_fw_path(abs, sizeof abs, "/mnt/UDISK/Emus") == 0)
        ne = scan_emus(abs, NULL, emus, 96, ne);
    for (int i = 0; i < ne; i++) {
        if (emus[i].ours)
            out->trimux_emus++;
        else
            out->original_emus++;
    }
    runtime_paths(p, rt, sizeof rt);
    char ra_src[TM_PATH_MAX], ra_dst[TM_PATH_MAX];
    tm_path_join(ra_src, sizeof ra_src, p->retroarch, "retroarch");
    tm_path_join(ra_dst, sizeof ra_dst, rt, "retroarch");
    char roms[TM_PATH_MAX];
    if (tm_path_join(roms, sizeof roms, p->sd, "Roms") != 0)
        return -1;
    DIR *d = opendir(roms);
    if (!d)
        return 0;
    int needs_runtime = 0;
    char seen_cores[TM_STOCK_MAX][64];
    int ncores = 0;
    char seen_sysdirs[8][64];
    int nsysdirs = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && out->n < TM_STOCK_MAX) {
        char sub[TM_PATH_MAX];
        struct stat st;
        if (e->d_name[0] == '.' || tm_path_join(sub, sizeof sub, roms, e->d_name) != 0 || lstat(sub, &st) != 0 ||
            !S_ISDIR(st.st_mode) || !tm_roms_dir_has_games(sub, 2) || strlen(e->d_name) >= 64)
            continue;
        TmStockItem *it = &out->items[out->n++];
        memset(it, 0, sizeof *it);
        tm_strlcpy(it->dir, e->d_name, sizeof it->dir);
        int safe = 1; /* the name goes into launch.sh and config.json */
        for (const char *c = e->d_name; *c && safe; c++)
            safe = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                   strchr(" _-.()+&,'", *c) != NULL || (unsigned char)*c >= 0x80;
        const Emu *orig = NULL, *mine = NULL;
        for (int i = 0; i < ne; i++)
            if (emus[i].read[0] && strcasecmp(emus[i].read, e->d_name) == 0) {
                if (emus[i].ours && !mine)
                    mine = &emus[i];
                else if (!emus[i].ours && !orig)
                    orig = &emus[i];
            }
        const TmSystem *sys = system_of(cat, e->d_name);
        if (sys) {
            tm_strlcpy(it->sys_id, sys->id, sizeof it->sys_id);
            tm_strlcpy(it->sys_name, sys->name, sizeof it->sys_name);
        }
        if (orig) {
            it->state = TM_STOCK_ORIGINAL;
            tm_strlcpy(it->emu_dir, orig->dir, sizeof it->emu_dir);
            continue;
        }
        const TmEmulator *em = sys ? tm_catalog_resolve(cat, sys, NULL, tm_ini_get(settings, "emulators", sys->id, NULL),
                                                        p->cores)
                                   : NULL;
        if (!em || strcmp(em->type, "retroarch") != 0 || !safe) {
            it->state = TM_STOCK_UNSUPPORTED;
            continue;
        }
        tm_strlcpy(it->emu_id, em->id, sizeof it->emu_id);
        needs_runtime = 1;
        /* bytes for its core (once per core) and the files it needs */
        int seen = 0;
        for (int c = 0; c < ncores && !seen; c++)
            seen = strcmp(seen_cores[c], em->core) == 0;
        unsigned long long need = 0;
        char src[TM_PATH_MAX], dst[TM_PATH_MAX];
        if (!seen && ncores < TM_STOCK_MAX) {
            tm_strlcpy(seen_cores[ncores++], em->core, sizeof seen_cores[0]);
            if (tm_path_join(src, sizeof src, p->cores, em->core) == 0 && tm_snprintf(dst, sizeof dst, "%s/cores/%s", rt, em->core) == 0)
                need += copy_need(src, dst);
        }
        if (em->system_dir[0]) {
            int s2 = 0;
            for (int c = 0; c < nsysdirs && !s2; c++)
                s2 = strcmp(seen_sysdirs[c], em->system_dir) == 0;
            if (!s2 && nsysdirs < 8) {
                tm_strlcpy(seen_sysdirs[nsysdirs++], em->system_dir, sizeof seen_sysdirs[0]);
                if (tm_path_join(src, sizeof src, p->sd, em->system_dir) == 0 && tm_snprintf(dst, sizeof dst, "%s/system", rt) == 0)
                    need += tree_need(src, dst);
            }
        }
        out->need_bytes += need;
        if (mine) {
            char launch[TM_PATH_MAX];
            tm_strlcpy(it->emu_dir, mine->dir, sizeof it->emu_dir);
            int complete = need == 0 && tm_snprintf(launch, sizeof launch, "%s/%s/launch.sh", p->sd, mine->dir) == 0 &&
                           tm_file_exists(launch) && copy_need(ra_src, ra_dst) == 0 && mine->current;
            it->state = complete ? TM_STOCK_TRIMUX : TM_STOCK_MISSING;
        } else {
            it->state = TM_STOCK_MISSING;
            snprintf(it->emu_dir, sizeof it->emu_dir, "Emus/%s", e->d_name);
            char ed[TM_PATH_MAX];
            if (tm_path_join(ed, sizeof ed, p->sd, it->emu_dir) == 0 && tm_dir_exists(ed))
                snprintf(it->emu_dir, sizeof it->emu_dir, "Emus/%s-TriMux", e->d_name); /* someone else's folder */
        }
    }
    closedir(d);
    if (needs_runtime) {
        char src[TM_PATH_MAX], dst[TM_PATH_MAX];
        out->need_bytes += copy_need(ra_src, ra_dst);
        if (tm_path_join(src, sizeof src, p->retroarch, "autoconfig") == 0 && tm_snprintf(dst, sizeof dst, "%s/autoconfig", rt) == 0)
            out->need_bytes += tree_need(src, dst);
    }
    return 0;
}

void tm_stock_counts(const TmStockPlan *plan, int counts[4])
{
    counts[0] = counts[1] = counts[2] = counts[3] = 0;
    for (int i = 0; i < plan->n; i++)
        counts[plan->items[i].state]++;
}

/* ------------------------------------------------------------ prepare */

/* Streamed copy (cores reach 80 MB), written beside dst and renamed when
 * complete, so a power cut never leaves half a file under the real name. */
static int copy_stream(const char *src, const char *dst)
{
    if (copy_need(src, dst) == 0)
        return 0;
    char tmp[TM_PATH_MAX];
    if (tm_snprintf(tmp, sizeof tmp, "%s.part", dst) != 0)
        return -1;
    int in = open(src, O_RDONLY);
    if (in < 0)
        return -1;
    int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (out < 0) {
        close(in);
        return -1;
    }
    static char buf[1 << 16];
    int rc = 0;
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n == 0)
            break;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            rc = -1;
            break;
        }
        for (ssize_t off = 0; off < n;) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                rc = -1;
                break;
            }
            off += w;
        }
        if (rc)
            break;
    }
    if (rc == 0 && fsync(out) != 0)
        rc = -1;
    close(in);
    if (close(out) != 0)
        rc = -1;
    if (rc == 0 && rename(tmp, dst) != 0)
        rc = -1;
    if (rc != 0)
        unlink(tmp);
    else
        tm_fsync_parent(dst);
    return rc;
}

static int g_tree_rc;
static int tree_copy_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st, (void)ftw;
    char dst[TM_PATH_MAX];
    if (tm_snprintf(dst, sizeof dst, "%s%s", g_tree_dst, path + strlen(g_tree_src)) != 0) {
        g_tree_rc = -1;
        return 0;
    }
    if (flag == FTW_D)
        tm_mkdir_p(dst);
    else if (flag == FTW_F && copy_stream(path, dst) != 0)
        g_tree_rc = -1;
    return 0;
}

static int copy_tree(const char *src, const char *dst)
{
    if (!tm_dir_exists(src))
        return 0;
    g_tree_src = src;
    g_tree_dst = dst;
    g_tree_rc = 0;
    tm_mkdir_p(dst);
    nftw(src, tree_copy_entry, 16, FTW_PHYS);
    return g_tree_rc;
}

/* RetroArch configuration for the stock interface: TriMux's base settings
 * (controls, hotkeys) with the folders of the runtime. */
static int write_ra_cfg(const TmPaths *p, const char *rt)
{
    char base[TM_PATH_MAX], cfg[TM_PATH_MAX];
    if (tm_path_join(base, sizeof base, p->retroarch, "retroarch.base.cfg") != 0 ||
        tm_path_join(cfg, sizeof cfg, rt, "retroarch.cfg") != 0)
        return -1;
    size_t len = 0;
    char *text = tm_read_file(base, 1u << 20, &len);
    if (!text)
        return -1;
    char extra[1024];
    int n = snprintf(extra, sizeof extra,
                     "\n# Emus/_TriMux (Sistema oficial): folders of this copy\n"
                     "libretro_directory = \"%s/cores\"\n"
                     "joypad_autoconfig_dir = \"%s/autoconfig\"\n"
                     "cache_directory = \"/tmp/trimux-stock\"\n",
                     rt, rt);
    char *all = malloc(len + (size_t)n + 1);
    if (!all || n < 0 || (size_t)n >= sizeof extra) {
        free(text);
        free(all);
        return -1;
    }
    memcpy(all, text, len);
    memcpy(all + len, extra, (size_t)n);
    int rc = tm_atomic_write(cfg, all, len + (size_t)n);
    free(text);
    free(all);
    return rc;
}

/* JSON string value (no quotes or backslashes are expected in these). */
static void json_safe(char *s)
{
    for (; *s; s++)
        if (*s == '"' || *s == '\\' || (unsigned char)*s < 32)
            *s = '_';
}

static int write_emu(const TmPaths *p, const TmStockItem *it, const TmSystem *sys, const TmEmulator *em)
{
    char dir[TM_PATH_MAX], path[TM_PATH_MAX];
    if (tm_path_join(dir, sizeof dir, p->sd, it->emu_dir) != 0 || tm_mkdir_p(dir) != 0)
        return -1;
    /* config.json in the stock format (keys read by the stock MainUI) */
    char label[160], exts[256] = "", body[1536];
    /* a second folder of the same platform (NES beside FC) says which one it is */
    if (sys->nfolders > 0 && strcasecmp(sys->folders[0], it->dir) != 0)
        snprintf(label, sizeof label, "%s - %s", sys->name, it->dir);
    else
        tm_strlcpy(label, sys->name, sizeof label);
    json_safe(label);
    size_t l = 0;
    for (int i = 0; i < sys->nexts; i++)
        l += (size_t)snprintf(exts + l, sizeof exts - l, "%s%s", i ? "|" : "", sys->exts[i]);
    char folder[64];
    tm_strlcpy(folder, it->dir, sizeof folder);
    json_safe(folder);
    int n = snprintf(body, sizeof body,
                     "{\n"
                     "  \"label\": \"%s\",\n"
                     "  \"icon\": \"icon.png\",\n"
                     "  \"icontop\": \"icon.png\",\n"
                     "  \"launch\": \"launch.sh\",\n"
                     "  \"rompath\": \"../../Roms/%s\",\n"
                     "  \"imgpath\": \"../../Imgs/%s\",\n"
                     "  \"useswap\": 0,\n"
                     "  \"extlist\": \"%s\",\n"
                     "  \"description\": \"%s - TriMux RetroArch (%s)\"\n"
                     "}\n",
                     label, folder, folder, exts, label, em->name);
    if (n < 0 || (size_t)n >= sizeof body || tm_path_join(path, sizeof path, dir, "config.json") != 0 ||
        tm_atomic_write(path, body, (size_t)n) != 0)
        return -1;
    /* launch.sh: the stock menu runs it with the game as the last argument */
    const char *sysdir = em->system_dir[0] ? "$RT/system" : "$SD/Bios";
    char sh[4096];
    n = snprintf(sh, sizeof sh,
                 "#!/bin/sh\n"
                 "# Written by TriMux (Configurações › Sistema › Sistema oficial): plays\n"
                 "# %s in the stock TrimUI interface with the RetroArch copy in\n"
                 "# Emus/_TriMux. Saves go to Saves/%s, the same folder TriMux uses.\n"
                 "D=$(cd \"$(dirname \"$0\")\" && pwd)\n"
                 "SD=$(cd \"$D/../..\" && pwd)\n"
                 "RT=$SD/Emus/_TriMux\n"
                 "ROM=\n"
                 "for a in \"$@\"; do ROM=$a; done\n"
                 "[ -e \"$ROM\" ] || [ ! -e \"$SD/Roms/%s/$ROM\" ] || ROM=$SD/Roms/%s/$ROM\n"
                 "mkdir -p \"$SD/Saves/%s\" \"$SD/States/%s\" \"$SD/Screenshots\" \"$SD/TriMuxData/retroarch\" /tmp/trimux-stock\n"
                 "APPEND=/tmp/trimux-stock/append.cfg\n"
                 "cat > \"$APPEND\" <<EOF\n"
                 "savefile_directory = \"$SD/Saves/%s\"\n"
                 "savestate_directory = \"$SD/States/%s\"\n"
                 "system_directory = \"%s\"\n"
                 "screenshot_directory = \"$SD/Screenshots\"\n"
                 "EOF\n"
                 "export HOME=\"$SD/TriMuxData/retroarch\"\n"
                 "export LD_LIBRARY_PATH=/usr/trimui/lib:/usr/lib:/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\n"
                 "cd \"$RT\" || exit 1\n"
                 "start=$(cut -d. -f1 /proc/uptime 2>/dev/null || echo 0)\n"
                 "./retroarch -c \"$RT/retroarch.cfg\" --appendconfig \"$APPEND\" -L \"$RT/cores/%s\" \"$ROM\" "
                 "> /tmp/trimux-stock/last.log 2>&1\n"
                 "rc=$?\n"
                 "now=$(cut -d. -f1 /proc/uptime 2>/dev/null || echo 0)\n"
                 "if [ $rc -ne 0 ] && [ $((now - start)) -lt 5 ]; then\n"
                 "    # GLES context refused: try RetroArch's SDL2 renderer once, as TriMux does\n"
                 "    echo 'video_driver = \"sdl2\"' >> \"$APPEND\"\n"
                 "    ./retroarch -c \"$RT/retroarch.cfg\" --appendconfig \"$APPEND\" -L \"$RT/cores/%s\" \"$ROM\" "
                 ">> /tmp/trimux-stock/last.log 2>&1\n"
                 "    rc=$?\n"
                 "fi\n"
                 "sync\n"
                 "exit $rc\n",
                 sys->name, sys->id, it->dir, it->dir, sys->id, sys->id, sys->id, sys->id, sysdir, em->core, em->core);
    if (n < 0 || (size_t)n >= sizeof sh || tm_path_join(path, sizeof path, dir, "launch.sh") != 0 ||
        tm_atomic_write(path, sh, (size_t)n) != 0)
        return -1;
    chmod(path, 0755);
    if (tm_path_join(path, sizeof path, dir, "icon.png") != 0 ||
        tm_stock_icon(path, sys->id, sys->color) != 0)
        LOGW("stock: could not draw the icon of %s", it->emu_dir);
    char mark[64];
    n = snprintf(mark, sizeof mark, "%s %s %s\n", sys->id, em->id, TM_STOCK_FILES);
    if (tm_path_join(path, sizeof path, dir, TM_STOCK_MARKER) != 0 || tm_atomic_write(path, mark, (size_t)n) != 0)
        return -1;
    return 0;
}

int tm_stock_prepare(const TmPaths *p, const TmCatalog *cat, const TmIni *settings, char *err, size_t errsz)
{
    static TmStockPlan plan;
    tm_strlcpy(err, "", errsz);
    if (tm_stock_plan(p, cat, settings, &plan) != 0) {
        tm_strlcpy(err, "stock.err.scan", errsz);
        return -1;
    }
    int todo = 0;
    for (int i = 0; i < plan.n; i++)
        todo += plan.items[i].state == TM_STOCK_MISSING || plan.items[i].state == TM_STOCK_TRIMUX;
    if (!todo)
        return 0;
    struct statvfs vs;
    if (statvfs(p->sd, &vs) == 0 && (unsigned long long)vs.f_bavail * vs.f_frsize < plan.need_bytes + (16ULL << 20)) {
        tm_strlcpy(err, "stock.err.space", errsz);
        LOGW("stock: %llu bytes needed, not enough space", plan.need_bytes);
        return -1;
    }
    char rt[TM_PATH_MAX], src[TM_PATH_MAX], dst[TM_PATH_MAX];
    runtime_paths(p, rt, sizeof rt);
    int rc = 0;
    rc |= tm_mkdir_p(rt);
    tm_snprintf(dst, sizeof dst, "%s/cores", rt);
    rc |= tm_mkdir_p(dst);
    rc |= tm_path_join(src, sizeof src, p->retroarch, "retroarch");
    rc |= tm_path_join(dst, sizeof dst, rt, "retroarch");
    if (rc == 0 && copy_stream(src, dst) != 0)
        rc = -1;
    chmod(dst, 0755);
    if (tm_path_join(src, sizeof src, p->retroarch, "autoconfig") == 0 && tm_snprintf(dst, sizeof dst, "%s/autoconfig", rt) == 0)
        rc |= copy_tree(src, dst);
    if (tm_path_join(src, sizeof src, p->sys, "licenses") == 0 && tm_snprintf(dst, sizeof dst, "%s/licenses", rt) == 0)
        copy_tree(src, dst); /* licences of RetroArch and the cores travel with them */
    rc |= write_ra_cfg(p, rt);
    if (rc != 0) {
        tm_strlcpy(err, "stock.err.copy", errsz);
        LOGE("stock: could not copy RetroArch to %s", TM_STOCK_RUNTIME);
        return -1;
    }
    int done = 0;
    for (int i = 0; i < plan.n; i++) {
        TmStockItem *it = &plan.items[i];
        if (it->state != TM_STOCK_MISSING && it->state != TM_STOCK_TRIMUX)
            continue;
        const TmSystem *sys = tm_catalog_system(cat, it->sys_id);
        const TmEmulator *em = tm_catalog_emulator(cat, it->emu_id);
        if (!sys || !em)
            continue;
        if (tm_path_join(src, sizeof src, p->cores, em->core) != 0 || tm_snprintf(dst, sizeof dst, "%s/cores/%s", rt, em->core) != 0 ||
            copy_stream(src, dst) != 0) {
            LOGE("stock: could not copy the core %s", em->core);
            rc = -1;
            continue;
        }
        if (em->system_dir[0] && tm_path_join(src, sizeof src, p->sd, em->system_dir) == 0 &&
            tm_snprintf(dst, sizeof dst, "%s/system", rt) == 0 && copy_tree(src, dst) != 0) {
            rc = -1;
            continue;
        }
        if (write_emu(p, it, sys, em) != 0) {
            LOGE("stock: could not write %s", it->emu_dir);
            rc = -1;
            continue;
        }
        LOGI("stock: %s reads Roms/%s with %s", it->emu_dir, it->dir, em->id);
        done++;
    }
    sync();
    if (rc != 0)
        tm_strlcpy(err, "stock.err.copy", errsz);
    LOGI("stock: %d emulator(s) ready for the stock interface", done);
    return rc;
}
