/* Home screen: continue, recent, favorites, all games, platforms, settings. */
#define _GNU_SOURCE
#include "app.h"
#include "../core/util.h"

#include <stdio.h>
#include <string.h>

#define S(v) ((v) * gfx_h() / 768)

enum { H_CONTINUE, H_RECENT, H_FAVORITES, H_ALL, H_SYSTEM, H_SETTINGS };

typedef struct {
    int type;
    int system;
    long game;
} HomeEntry;

static HomeEntry g_entries[160];
static size_t g_n;

size_t home_count(void) { return g_n; }

static long first_recent_in_library(void)
{
    for (size_t i = 0; i < A.recent.count; i++) {
        long gi = tm_library_find(&A.lib, A.recent.items[i]);
        if (gi >= 0)
            return gi;
    }
    return -1;
}

void home_build(void)
{
    g_n = 0;
    long cont = first_recent_in_library();
    if (cont >= 0)
        g_entries[g_n++] = (HomeEntry){H_CONTINUE, -1, cont};
    if (A.recent.count)
        g_entries[g_n++] = (HomeEntry){H_RECENT, -1, -1};
    g_entries[g_n++] = (HomeEntry){H_FAVORITES, -1, -1};
    g_entries[g_n++] = (HomeEntry){H_ALL, -1, -1};
    int show_empty = (int)tm_ini_get_long(&A.settings, "general", "show_empty", 0);
    for (size_t s = 0; s < A.cat.nsystems && g_n < TM_ARRAY_LEN(g_entries) - 1; s++) {
        size_t n = tm_library_count_system(&A.lib, (int)s);
        /* experimental platforms appear only when the user has games for them */
        if (n == 0 && (!show_empty || A.cat.systems[s].experimental))
            continue;
        g_entries[g_n++] = (HomeEntry){H_SYSTEM, (int)s, -1};
    }
    g_entries[g_n++] = (HomeEntry){H_SETTINGS, -1, -1};
    if ((size_t)A.home_sel >= g_n)
        A.home_sel = 0;
}

static void entry_label(const HomeEntry *e, char *label, size_t ls, char *value, size_t vs)
{
    value[0] = '\0';
    switch (e->type) {
    case H_CONTINUE:
        snprintf(label, ls, "%s", tr("home.continue"));
        tm_strlcpy(value, A.lib.games[e->game].name, vs);
        break;
    case H_RECENT:
        snprintf(label, ls, "%s", tr("home.recent"));
        snprintf(value, vs, "%zu", A.recent.count);
        break;
    case H_FAVORITES:
        snprintf(label, ls, "%s", tr("home.favorites"));
        snprintf(value, vs, "%zu", A.fav.count);
        break;
    case H_ALL:
        snprintf(label, ls, "%s", tr("home.all"));
        snprintf(value, vs, "%zu", A.lib.count);
        break;
    case H_SYSTEM:
        snprintf(label, ls, "%s", A.cat.systems[e->system].name);
        snprintf(value, vs, "%zu", tm_library_count_system(&A.lib, e->system));
        break;
    default:
        snprintf(label, ls, "%s", tr("home.settings"));
    }
}

static void draw_panel(const HomeEntry *e, int x, int y, int w, int h)
{
    const TmTheme *t = gfx_theme();
    gfx_round_rect(x, y, w, h, S(14), t->panel);
    int px = x + S(24), py = y + S(22), pw = w - S(48);
    char buf[512];
    switch (e->type) {
    case H_CONTINUE: {
        const TmGame *g = &A.lib.games[e->game];
        const TmSystem *sys = &A.cat.systems[g->system];
        gfx_badge(px, py, S(34), sys->color, sys->short_name);
        py += S(52);
        py += gfx_text_wrap(FONT_L, px, py, pw, 3, t->text, g->name) + S(14);
        const TmEmulator *em = app_resolve_emu(g, NULL);
        snprintf(buf, sizeof buf, "%s: %s", tr("games.emulator"), em ? em->name : tr("games.no_emulator"));
        gfx_text(FONT_S, px, py, t->dim, ALIGN_LEFT, pw, buf);
        py += S(40);
        gfx_text_wrap(FONT_S, px, py, pw, 6, t->dim, tr("home.continue.hint"));
        break;
    }
    case H_SYSTEM: {
        const TmSystem *sys = &A.cat.systems[e->system];
        gfx_badge(px, py, S(34), sys->color, sys->short_name);
        py += S(52);
        py += gfx_text_wrap(FONT_L, px, py, pw, 2, t->text, sys->name) + S(10);
        size_t ng = tm_library_count_system(&A.lib, e->system);
        snprintf(buf, sizeof buf, tr(ng == 1 ? "home.games_1" : "home.games_n"), ng);
        gfx_text(FONT_M, px, py, t->text, ALIGN_LEFT, pw, buf);
        py += S(46);
        const char *pref = tm_ini_get(&A.settings, "emulators", sys->id, NULL);
        const TmEmulator *em = tm_catalog_resolve(&A.cat, sys, NULL, pref, A.paths.cores);
        snprintf(buf, sizeof buf, "%s: %s", tr("games.emulator"), em ? em->name : tr("games.no_emulator"));
        gfx_text(FONT_S, px, py, t->dim, ALIGN_LEFT, pw, buf);
        py += S(36);
        if (sys->nbios > 0) {
            char missing[256];
            int nm = app_bios_status(sys, missing, sizeof missing);
            if (nm == 0)
                snprintf(buf, sizeof buf, "%s", tr("home.bios_ok"));
            else
                snprintf(buf, sizeof buf, tr(sys->bios_required && nm == sys->nbios ? "home.bios_required" : "home.bios_optional"), missing);
            py += gfx_text_wrap(FONT_S, px, py, pw, 3, nm == 0 ? t->ok : (sys->bios_required ? t->warn : t->dim), buf) + S(8);
        }
        if (sys->note_key[0])
            gfx_text_wrap(FONT_S, px, py, pw, 6, t->warn, tr(sys->note_key));
        break;
    }
    default: {
        const char *k = e->type == H_RECENT ? "home.recent.desc" : e->type == H_FAVORITES ? "home.favorites.desc"
                      : e->type == H_ALL ? "home.all.desc" : "home.settings.desc";
        char label[96], value[64];
        entry_label(e, label, sizeof label, value, sizeof value);
        py += gfx_text_wrap(FONT_L, px, py, pw, 2, t->text, label) + S(16);
        gfx_text_wrap(FONT_S, px, py, pw, 8, t->dim, tr(k));
    }
    }
}

void home_draw(void)
{
    char title[128];
    snprintf(title, sizeof title, "TriMux · %s", tr("home.title"));
    app_header(title);
    int top = S(64) + S(14), bottom = gfx_h() - S(52) - S(10);
    int row = S(64);
    int listw = gfx_w() * 58 / 100;
    int visible = (bottom - top) / row;
    if (A.home_sel < A.home_top)
        A.home_top = A.home_sel;
    if (A.home_sel >= A.home_top + visible)
        A.home_top = A.home_sel - visible + 1;
    for (int i = 0; i < visible && (size_t)(A.home_top + i) < g_n; i++) {
        const HomeEntry *e = &g_entries[A.home_top + i];
        char label[96], value[64];
        entry_label(e, label, sizeof label, value, sizeof value);
        const char *badge = e->type == H_SYSTEM ? A.cat.systems[e->system].short_name : NULL;
        uint32_t bc = e->type == H_SYSTEM ? A.cat.systems[e->system].color : 0;
        app_draw_list_row(S(16), top + i * row, listw - S(24), row, A.home_top + i == A.home_sel, label,
                          e->type == H_CONTINUE ? "" : value, 1, bc, badge, 0);
    }
    if (g_n > (size_t)visible) { /* scroll indicator */
        const TmTheme *t = gfx_theme();
        int track = bottom - top;
        int bar = track * visible / (int)g_n;
        int pos = (track - bar) * A.home_sel / (int)(g_n - 1);
        gfx_rect(listw - S(6), top + pos, S(4), bar, t->dim);
    }
    if (g_n)
        draw_panel(&g_entries[A.home_sel], listw + S(4), top, gfx_w() - listw - S(20), bottom - top);
    app_footer(tr("home.hints"));
}

void home_input(TmButton b)
{
    if (!g_n)
        return;
    switch (b) {
    case BTN_UP: A.home_sel = A.home_sel > 0 ? A.home_sel - 1 : (int)g_n - 1; break;
    case BTN_DOWN: A.home_sel = (size_t)(A.home_sel + 1) < g_n ? A.home_sel + 1 : 0; break;
    case BTN_L1: A.home_sel = A.home_sel > 5 ? A.home_sel - 6 : 0; break;
    case BTN_R1: A.home_sel = (size_t)(A.home_sel + 6) < g_n ? A.home_sel + 6 : (int)g_n - 1; break;
    case BTN_A: {
        const HomeEntry *e = &g_entries[A.home_sel];
        A.query[0] = '\0';
        switch (e->type) {
        case H_CONTINUE: app_launch(e->game); break;
        case H_RECENT: games_open(VIEW_RECENT); break;
        case H_FAVORITES: games_open(VIEW_FAVORITES); break;
        case H_ALL: games_open(VIEW_ALL); break;
        case H_SYSTEM: games_open(e->system); break;
        default: A.menu_return = SCR_HOME; menu_open(PAGE_SETTINGS, 0, NULL);
        }
        break;
    }
    case BTN_Y:
        A.query[0] = '\0';
        games_open(VIEW_ALL);
        keyboard_open("", SCR_GAMES);
        break;
    case BTN_MENU:
    case BTN_HOME:
    case BTN_START:
        A.menu_return = SCR_HOME;
        menu_open(PAGE_QUICK, 0, NULL);
        break;
    default:
        break;
    }
}
