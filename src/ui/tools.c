/* Aplicativos › Ferramentas: statistics, random game, card cleanup, file
 * manager and the browser file server (menu pages built with menus.c). */
#define _GNU_SOURCE
#include "app.h"
#include "../core/log.h"
#include "../core/tools.h"
#include "../core/util.h"
#include "../core/webfiles.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ACT_NONE 0 /* menus.c: a row without an action */
#define FILES_PER_PAGE 56
#define FILES_MAX 20000

static void size_text(unsigned long long b, char *out, size_t size)
{
    static const char *u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)b;
    int i = 0;
    while (v >= 1024 && i < 4) {
        v /= 1024;
        i++;
    }
    if (i == 0)
        snprintf(out, size, "%llu B", b);
    else
        snprintf(out, size, v < 10 ? "%.1f %s" : "%.0f %s", v, u[i]);
}

/* ------------------------------------------------------------ entries in Aplicativos */

void tools_items(Menu *m)
{
    menu_header(m, tr("tools.header"));
    MenuItem *it = menu_add(m, ACT_T_PAGE, tr("tools.stats"), "›", tr("tools.stats.desc"));
    it->arg = PAGE_STATS;
    it = menu_add(m, ACT_T_RANDOM, tr("tools.random"), "", tr("tools.random.desc"));
    it->enabled = A.lib.count > 0;
    it = menu_add(m, ACT_T_PAGE, tr("tools.files"), "›", tr("tools.files.desc"));
    it->arg = PAGE_FILES;
    it = menu_add(m, ACT_T_WEB, tr("tools.web"), "›", tr("tools.web.desc"));
    it->enabled = tm_web_available(&A.paths);
    it = menu_add(m, ACT_T_PAGE, tr("tools.clean"), "›", tr("tools.clean.desc"));
    it->arg = PAGE_CLEAN;
}

/* ------------------------------------------------------------ statistics */

static void page_stats(Menu *m)
{
    tm_strlcpy(m->title, tr("tools.stats"), sizeof m->title);
    TmStats st;
    tm_stats_compute(&A.plays, &A.lib, &st);
    char v[64], d[512];
    if (!st.games) {
        menu_add(m, ACT_NONE, tr("stats.none"), "", tr("stats.none.desc"))->enabled = 1;
        return;
    }
    tm_format_duration(st.seconds, v, sizeof v);
    menu_add(m, ACT_NONE, tr("stats.total"), v, tr("stats.total.desc"))->enabled = 1;
    snprintf(v, sizeof v, "%ld", st.times);
    menu_add(m, ACT_NONE, tr("stats.sessions"), v, tr("stats.total.desc"))->enabled = 1;
    snprintf(v, sizeof v, "%d", st.games);
    menu_add(m, ACT_NONE, tr("stats.games"), v, tr("stats.total.desc"))->enabled = 1;
    if (st.last.game >= 0) {
        const TmGame *g = &A.lib.games[st.last.game];
        char when[32] = "";
        time_t t = (time_t)st.last.last;
        struct tm tmv;
        if (localtime_r(&t, &tmv))
            strftime(when, sizeof when, "%d/%m/%Y %H:%M", &tmv);
        snprintf(d, sizeof d, tr("stats.last.desc"), g->name, A.cat.systems[g->system].name, when);
        MenuItem *it = menu_add(m, ACT_T_GAME, tr("stats.last"), g->name, d);
        it->arg = st.last.game;
    }
    menu_header(m, tr("stats.top"));
    for (size_t i = 0; i < st.ntop; i++) {
        const TmGame *g = &A.lib.games[st.top[i].game];
        char label[96];
        snprintf(label, sizeof label, "%zu. %s", i + 1, g->name);
        tm_format_duration(st.top[i].seconds, v, sizeof v);
        char dur[32];
        tm_strlcpy(dur, v, sizeof dur);
        snprintf(d, sizeof d, tr("stats.game.desc"), g->name, A.cat.systems[g->system].name, st.top[i].times, dur);
        MenuItem *it = menu_add(m, ACT_T_GAME, label, v, d);
        it->arg = st.top[i].game;
    }
    menu_header(m, tr("stats.systems"));
    for (size_t i = 0; i < st.nsystems && m->n < MENU_MAX_ITEMS - 1; i++) {
        const TmSystem *sys = &A.cat.systems[st.systems[i].system];
        tm_format_duration(st.systems[i].seconds, v, sizeof v);
        int pct = st.seconds ? (int)(st.systems[i].seconds * 100 / st.seconds) : 0;
        snprintf(d, sizeof d, tr("stats.system.desc"), sys->name, st.systems[i].games, st.systems[i].times, pct);
        MenuItem *it = menu_add(m, ACT_NONE, sys->name, v, d);
        it->enabled = 1;
        it->badge_color = sys->color;
        tm_strlcpy(it->badge, sys->short_name, sizeof it->badge);
    }
}

/* ------------------------------------------------------------ random game */

static void random_game(void)
{
    static unsigned seed;
    if (!seed)
        seed = (unsigned)time(NULL) ^ (unsigned)getpid();
    long gi = tm_random_game(&A.lib, &A.plays, -1, 1, &seed);
    if (gi < 0) {
        app_toast(tr("keys.no_game"));
        return;
    }
    A.query[0] = '\0';
    A.nmenus = 0;
    games_open(A.lib.games[gi].system);
    for (size_t i = 0; i < A.nview; i++)
        if (A.view[i] == gi) {
            A.games_sel = (int)i;
            break;
        }
    TmPlays pl;
    tm_plays_get(&A.plays, A.lib.games[gi].relpath, &pl);
    app_toast(tr(pl.times ? "tools.random.any" : "tools.random.new"));
}

/* ------------------------------------------------------------ cleanup */

static TmCleanReport g_clean;
static unsigned g_clean_mask = (1u << TM_CLEAN_COMPUTER) | (1u << TM_CLEAN_TEMP);
static int g_clean_valid;

static void page_clean(Menu *m)
{
    tm_strlcpy(m->title, tr("tools.clean"), sizeof m->title);
    if (!g_clean_valid) {
        tm_clean_scan(&A.paths, &g_clean);
        g_clean_valid = 1;
    }
    menu_add(m, ACT_NONE, tr("clean.intro"), "", tr("clean.intro.desc"))->enabled = 1;
    unsigned long long total = 0;
    int files = 0;
    for (int c = 0; c < TM_CLEAN_N; c++) {
        const TmCleanCat *cc = &g_clean.cat[c];
        char k[48], v[64], sz[24], d[1024];
        snprintf(k, sizeof k, "clean.%s", tm_clean_id(c));
        size_text(cc->bytes, sz, sizeof sz);
        int on = (g_clean_mask >> c) & 1;
        if (cc->files)
            snprintf(v, sizeof v, "%s  %s", on ? "☑" : "☐", sz);
        else
            tm_strlcpy(v, tr("clean.nothing"), sizeof v);
        snprintf(k, sizeof k, "clean.%s.desc", tm_clean_id(c));
        int n = snprintf(d, sizeof d, "%s\n\n", tr(k));
        if (cc->files) {
            n += snprintf(d + n, sizeof d - n, tr("clean.found"), cc->files, sz);
            for (int s = 0; s < cc->nsample && n < (int)sizeof d - 200; s++)
                n += snprintf(d + n, sizeof d - n, "\n• %s", cc->sample[s]);
            if (cc->files > cc->nsample && n < (int)sizeof d - 8)
                snprintf(d + n, sizeof d - n, "\n• …");
        }
        snprintf(k, sizeof k, "clean.%s", tm_clean_id(c));
        MenuItem *it = menu_add(m, ACT_T_CLEAN_TOGGLE, tr(k), v, d);
        it->arg = c;
        it->enabled = cc->files > 0;
        if (on && cc->files) {
            total += cc->bytes;
            files += cc->files;
        }
    }
    char v[64], sz[24], d[256];
    size_text(total, sz, sizeof sz);
    snprintf(v, sizeof v, "%s", files ? sz : "");
    snprintf(d, sizeof d, tr("clean.run.desc"), files, sz);
    MenuItem *it = menu_add(m, ACT_T_CLEAN_RUN, tr("clean.run"), v, d);
    it->enabled = files > 0;
    it->arg = files;
    it = menu_add(m, ACT_T_CLEAN_RESCAN, tr("clean.rescan"), "", tr("clean.rescan.desc"));
}

/* ------------------------------------------------------------ file manager */

typedef struct {
    char *name;
    int dir;
    long long size;
    long mtime;
} FmEntry;

/* Text files open in the viewer: by extension (no file read per row), so a
 * page with hundreds of games stays quick. */
static int file_is_text(const char *name)
{
    static const char *const ext[] = {".log", ".txt", ".sh", ".json", ".ini", ".cfg", ".conf", ".md",
                                      ".csv", ".xml", ".opt", ".lang", ".m3u", ".cue", ".lst", ".py"};
    const char *dot = strrchr(name, '.');
    for (size_t i = 0; dot && i < sizeof ext / sizeof ext[0]; i++)
        if (strcasecmp(dot, ext[i]) == 0)
            return 1;
    return 0;
}

static void view_file(const char *rel)
{
    char abs[TM_PATH_MAX];
    if (tm_path_join(abs, sizeof abs, A.paths.sd, rel) != 0 || !tm_path_is_safe_under(A.paths.sd, abs))
        return;
    if (!textview_is_text(abs)) {
        app_dialog(DLG_INFO, tr("tools.files"), tr("files.not_text"), 0, NULL, 1);
        return;
    }
    if (textview_open(abs, rel) != 0)
        app_toast(tr("files.unreadable_file"));
}

static void ask_delete(MenuItem *it)
{
    if (it->arg) {
        app_dialog(DLG_INFO, tr("tools.files"), tr("files.protected"), 0, NULL, 1);
        return;
    }
    const char *name = strrchr(it->sarg, '/');
    char text[512];
    snprintf(text, sizeof text, tr("files.delete.confirm"), name ? name + 1 : it->sarg, it->value);
    app_dialog(DLG_FILE_DELETE, tr("files.delete"), text, 0, it->sarg, 0);
}

int tools_button_x(Menu *m, MenuItem *it)
{
    if (m->page != PAGE_FILES || it->id != ACT_T_FILE)
        return 0;
    ask_delete(it);
    return 1;
}

static int cmp_fm(const void *a, const void *b)
{
    const FmEntry *x = a, *y = b;
    if (x->dir != y->dir)
        return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}

static void page_files(Menu *m)
{
    const char *rel = m->sctx;
    snprintf(m->title, sizeof m->title, "%s%s%s", tr("tools.files"), rel[0] ? " › " : "", rel);
    char abs[TM_PATH_MAX];
    if ((rel[0] ? tm_path_join(abs, sizeof abs, A.paths.sd, rel) : tm_strlcpy(abs, A.paths.sd, sizeof abs)) != 0)
        return;
    DIR *d = opendir(abs);
    FmEntry *v = NULL;
    size_t n = 0, cap = 0;
    struct dirent *e;
    while (d && (e = readdir(d)) && n < FILES_MAX) {
        if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;
        char p[TM_PATH_MAX];
        struct stat st;
        if (tm_path_join(p, sizeof p, abs, e->d_name) != 0 || lstat(p, &st) != 0 ||
            !(S_ISDIR(st.st_mode) || S_ISREG(st.st_mode)))
            continue;
        if (n == cap) {
            FmEntry *nv = realloc(v, (cap = cap ? cap * 2 : 128) * sizeof *v);
            if (!nv)
                break;
            v = nv;
        }
        if (!(v[n].name = strdup(e->d_name)))
            break;
        v[n].dir = S_ISDIR(st.st_mode);
        v[n].size = (long long)st.st_size;
        v[n].mtime = (long)st.st_mtime;
        n++;
    }
    if (d)
        closedir(d);
    qsort(v, n, sizeof *v, cmp_fm);
    long off = m->ctx > 0 && (size_t)m->ctx < n ? m->ctx : 0;
    int ro = rel[0] && tm_path_protected(rel);
    if (!d)
        menu_add(m, ACT_NONE, tr("files.unreadable"), "", "")->enabled = 1;
    else if (!n)
        menu_add(m, ACT_NONE, tr("files.empty"), "", tr("files.desc"))->enabled = 1;
    if (off > 0) {
        MenuItem *it = menu_add(m, ACT_T_FILE_PAGE, tr("files.prev"), "", "");
        it->arg = off - FILES_PER_PAGE > 0 ? off - FILES_PER_PAGE : 0;
    }
    for (size_t i = (size_t)off; i < n && i < (size_t)off + FILES_PER_PAGE; i++) {
        char r[TM_PATH_MAX], val[48], desc[1024], when[32] = "";
        if ((rel[0] ? tm_path_join(r, sizeof r, rel, v[i].name) : tm_strlcpy(r, v[i].name, sizeof r)) != 0 ||
            strlen(r) >= sizeof m->items[0].sarg)
            continue;
        time_t t = (time_t)v[i].mtime;
        struct tm tmv;
        if (localtime_r(&t, &tmv))
            strftime(when, sizeof when, "%d/%m/%Y %H:%M", &tmv);
        int prot = ro || (!rel[0] && tm_path_protected(v[i].name));
        if (v[i].dir) {
            tm_strlcpy(val, "›", sizeof val);
            snprintf(desc, sizeof desc, tr("files.dir.desc"), v[i].name, when);
        } else {
            char sz[24];
            size_text((unsigned long long)v[i].size, sz, sizeof sz);
            tm_strlcpy(val, sz, sizeof val);
            snprintf(desc, sizeof desc, tr("files.file.desc"), v[i].name, sz, when);
        }
        if (prot) {
            size_t l = strlen(desc);
            snprintf(desc + l, sizeof desc - l, "\n\n%s", tr("files.protected"));
            if (!v[i].dir && file_is_text(v[i].name)) {
                l = strlen(desc);
                snprintf(desc + l, sizeof desc - l, "\n%s", tr("files.text.view"));
            }
        } else if (!v[i].dir) {
            size_t l = strlen(desc);
            snprintf(desc + l, sizeof desc - l, "\n\n%s", tr(file_is_text(v[i].name) ? "files.text.hint" : "files.delete.hint"));
        }
        char label[96];
        snprintf(label, sizeof label, "%s%s", v[i].dir ? "▸ " : "", v[i].name);
        MenuItem *it = menu_add(m, v[i].dir ? ACT_T_FILE_DIR : ACT_T_FILE, label, val, desc);
        tm_strlcpy(it->sarg, r, sizeof it->sarg);
        it->arg = prot;
    }
    if ((size_t)off + FILES_PER_PAGE < n) {
        char lbl[96];
        snprintf(lbl, sizeof lbl, tr("files.next"), (long)(n - (size_t)off - FILES_PER_PAGE));
        MenuItem *it = menu_add(m, ACT_T_FILE_PAGE, lbl, "", "");
        it->arg = off + FILES_PER_PAGE;
    }
    for (size_t i = 0; i < n; i++)
        free(v[i].name);
    free(v);
}

static void files_go(Menu *m, const char *rel, const char *select)
{
    tm_strlcpy(m->sctx, rel, sizeof m->sctx);
    m->ctx = 0;
    m->sel = m->top = 0;
    menu_rebuild();
    if (select)
        for (int i = 0; i < m->n; i++)
            if (strcmp(m->items[i].sarg, select) == 0) {
                m->sel = i;
                break;
            }
}

int tools_back(Menu *m)
{
    if (m->page != PAGE_FILES || !m->sctx[0])
        return 0;
    char from[256];
    tm_strlcpy(from, m->sctx, sizeof from);
    char *slash = strrchr(m->sctx, '/');
    char up[256] = "";
    if (slash) {
        *slash = '\0';
        tm_strlcpy(up, m->sctx, sizeof up);
    }
    files_go(m, up, from);
    return 1;
}

/* ------------------------------------------------------------ browser file server */

static void web_start(void)
{
    TmWifiStatus st;
    if (!tm_wifi_running() || tm_wifi_status(&st) != 0 || strcmp(st.state, "COMPLETED") != 0 || !st.ip[0]) {
        app_dialog(DLG_INFO, tr("tools.web"), tr("web.needs_wifi"), 0, NULL, 1);
        return;
    }
    if (tm_web_start(&A.paths, st.ip, TM_WEB_PORT) != 0) {
        app_dialog(DLG_INFO, tr("tools.web"), tr("web.failed"), 0, NULL, 1);
        return;
    }
    char msg[512];
    snprintf(msg, sizeof msg, tr("web.running"), st.ip, TM_WEB_PORT);
    app_dialog(DLG_WEB, tr("tools.web"), msg, 0, NULL, 1);
}

void tools_web_stop(void)
{
    if (tm_web_running(&A.paths))
        tm_web_stop(&A.paths);
}

/* ------------------------------------------------------------ dispatch */

int tools_page(Menu *m)
{
    switch (m->page) {
    case PAGE_STATS: page_stats(m); return 1;
    case PAGE_CLEAN: page_clean(m); return 1;
    case PAGE_FILES: page_files(m); return 1;
    default: return 0;
    }
}

void tools_activate(Menu *m, MenuItem *it, TmButton b)
{
    switch (it->id) {
    case ACT_T_PAGE:
        if (b == BTN_A) {
            if (it->arg == PAGE_CLEAN)
                g_clean_valid = 0; /* fresh numbers each time the page opens */
            menu_open((int)it->arg, 0, "");
        }
        return;
    case ACT_T_RANDOM:
        if (b == BTN_A)
            random_game();
        return;
    case ACT_T_GAME:
        if (b == BTN_A && it->arg >= 0 && (size_t)it->arg < A.lib.count)
            menu_open(PAGE_GAME_OPTIONS, it->arg, NULL);
        return;
    case ACT_T_WEB:
        if (b == BTN_A)
            web_start();
        return;
    case ACT_T_CLEAN_TOGGLE:
        g_clean_mask ^= 1u << it->arg;
        break;
    case ACT_T_CLEAN_RUN:
        if (b == BTN_A) {
            char text[512];
            snprintf(text, sizeof text, tr("clean.confirm"), it->arg, it->value);
            app_dialog(DLG_CLEAN, tr("tools.clean"), text, (long)g_clean_mask, NULL, 0);
        }
        return;
    case ACT_T_CLEAN_RESCAN:
        if (b != BTN_A)
            return;
        g_clean_valid = 0;
        break;
    case ACT_T_FILE_DIR:
        if (b == BTN_A || b == BTN_RIGHT)
            files_go(m, it->sarg, NULL);
        return;
    case ACT_T_FILE_PAGE:
        if (b == BTN_A) {
            m->ctx = it->arg;
            m->sel = m->top = 0;
            menu_rebuild();
        }
        return;
    case ACT_T_FILE: {
        if (b != BTN_A)
            return;
        const char *name = strrchr(it->sarg, '/');
        if (file_is_text(name ? name + 1 : it->sarg))
            view_file(it->sarg); /* X deletes (tools_button_x) */
        else
            ask_delete(it);
        return;
    }
    case ACT_T_VIEW:
        if (b == BTN_A) {
            char abs[TM_PATH_MAX];
            if (tm_path_join(abs, sizeof abs, A.paths.sd, it->sarg) != 0 || textview_open(abs, it->label) != 0)
                app_dialog(DLG_INFO, it->label, tr("log.empty"), 0, NULL, 1);
        }
        return;
    case ACT_T_LOGS:
        if (b == BTN_A) {
            char abs[TM_PATH_MAX];
            if (tm_path_join(abs, sizeof abs, A.paths.logdir, "apps") == 0)
                tm_mkdir_p(abs);
            menu_open(PAGE_FILES, 0, it->sarg);
        }
        return;
    default: return;
    }
    menu_rebuild();
}

int tools_dialog_result(int id, long arg, const char *sarg, int yes)
{
    switch (id) {
    case DLG_WEB:
        tools_web_stop();
        app_rescan(); /* games sent from the browser show up right away */
        return 1;
    case DLG_CLEAN: {
        if (!yes)
            return 1;
        TmCleanReport done;
        tm_clean_run(&A.paths, (unsigned)arg, &done);
        int files = 0;
        unsigned long long bytes = 0;
        for (int c = 0; c < TM_CLEAN_N; c++) {
            files += done.cat[c].files;
            bytes += done.cat[c].bytes;
        }
        char sz[24], msg[128];
        size_text(bytes, sz, sizeof sz);
        snprintf(msg, sizeof msg, tr("clean.done"), files, sz);
        app_toast(msg);
        g_clean_valid = 0;
        menu_rebuild();
        return 1;
    }
    case DLG_RUN_LOG: {
        char abs[TM_PATH_MAX];
        if (yes && sarg && sarg[0] && tm_path_join(abs, sizeof abs, A.paths.sd, sarg) == 0 &&
            tm_path_is_safe_under(A.paths.sd, abs) && textview_open(abs, sarg) != 0)
            app_toast(tr("files.unreadable_file"));
        return 1;
    }
    case DLG_FILE_DELETE: {
        if (!yes || !sarg || !sarg[0] || tm_path_protected(sarg))
            return 1;
        char abs[TM_PATH_MAX];
        if (tm_path_join(abs, sizeof abs, A.paths.sd, sarg) != 0 || !tm_path_is_safe_under(A.paths.sd, abs))
            return 1;
        int game = tm_library_find(&A.lib, sarg) >= 0;
        if (unlink(abs) == 0) {
            tm_fsync_parent(abs);
            LOGI("ui: deleted %s (file manager)", sarg);
            app_toast(tr("files.deleted"));
            if (game)
                app_rescan();
        } else {
            app_toast(tr("files.delete.failed"));
        }
        menu_rebuild();
        return 1;
    }
    default: return 0;
    }
}
