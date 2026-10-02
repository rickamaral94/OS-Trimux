/* Game list: per platform, all, favorites or recent; search and filters. */
#define _GNU_SOURCE
#include "app.h"
#include "../core/log.h"
#include "../core/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define S(v) ((v) * gfx_h() / 768)

static int view_push(int gi)
{
    if (A.nview == A.view_cap) {
        size_t ncap = A.view_cap ? A.view_cap * 2 : 256;
        int *n = realloc(A.view, ncap * sizeof(int));
        if (!n)
            return -1;
        A.view = n;
        A.view_cap = ncap;
    }
    A.view[A.nview++] = gi;
    return 0;
}

void games_rebuild(void)
{
    A.nview = 0;
    char q[64];
    tm_fold_key(A.query, q, sizeof q);
    if (A.view_system == VIEW_RECENT) {
        for (size_t i = 0; i < A.recent.count; i++) {
            long gi = tm_library_find(&A.lib, A.recent.items[i]);
            if (gi >= 0 && tm_game_matches(&A.lib.games[gi], q))
                view_push((int)gi);
        }
    } else {
        for (size_t i = 0; i < A.lib.count; i++) {
            const TmGame *g = &A.lib.games[i];
            if (A.view_system >= 0 && g->system != A.view_system)
                continue;
            int fav = tm_list_index(&A.fav, g->relpath) >= 0;
            if ((A.view_system == VIEW_FAVORITES || A.fav_only) && !fav)
                continue;
            if (!tm_game_matches(g, q))
                continue;
            view_push((int)i);
        }
    }
    if (A.games_sel >= (int)A.nview)
        A.games_sel = A.nview ? (int)A.nview - 1 : 0;
}

void games_open(int system)
{
    A.view_system = system;
    A.games_sel = A.games_top = 0;
    A.fav_only = 0;
    games_rebuild();
    A.screen = SCR_GAMES;
}

long games_selected_index(void)
{
    if (A.screen != SCR_GAMES || A.nview == 0 || A.games_sel < 0 || (size_t)A.games_sel >= A.nview)
        return -1;
    return A.view[A.games_sel];
}

static const char *view_title(void)
{
    static char t[160];
    if (A.view_system >= 0)
        snprintf(t, sizeof t, "%s", A.cat.systems[A.view_system].name);
    else
        snprintf(t, sizeof t, "%s", tr(A.view_system == VIEW_FAVORITES ? "home.favorites"
                                       : A.view_system == VIEW_RECENT  ? "home.recent"
                                                                       : "home.all"));
    if (A.query[0]) {
        size_t n = strlen(t);
        snprintf(t + n, sizeof t - n, " · “%s”", A.query);
    }
    if (A.fav_only && A.view_system != VIEW_FAVORITES) {
        size_t n = strlen(t);
        snprintf(t + n, sizeof t - n, " · ★");
    }
    return t;
}

static void draw_panel(const TmGame *g, int x, int y, int w, int h)
{
    const TmTheme *t = gfx_theme();
    const TmSystem *sys = &A.cat.systems[g->system];
    gfx_round_rect(x, y, w, h, S(14), t->panel);
    int px = x + S(22), py = y + S(20), pw = w - S(44);
    gfx_badge(px, py, S(34), sys->color, sys->short_name);
    if (tm_list_index(&A.fav, g->relpath) >= 0)
        gfx_star(x + w - S(40), py + S(17), S(15), t->warn);
    py += S(50);
    py += gfx_text_wrap(FONT_L, px, py, pw, 3, t->text, g->name) + S(12);
    gfx_text(FONT_S, px, py, t->dim, ALIGN_LEFT, pw, sys->name);
    py += S(40);
    int ov = 0;
    const TmEmulator *em = app_resolve_emu(g, &ov);
    char buf[256];
    snprintf(buf, sizeof buf, "%s: %s%s%s", tr("games.emulator"), em ? em->name : tr("games.no_emulator"),
             ov ? " " : "", ov ? tr("games.override_suffix") : "");
    py += gfx_text_wrap(FONT_S, px, py, pw, 2, em ? t->text : t->warn, buf) + S(8);
    if (em && em->note_key[0])
        py += gfx_text_wrap(FONT_S, px, py, pw, 4, t->dim, tr(em->note_key)) + S(8);
    const char *file = strrchr(g->relpath, '/');
    gfx_text_wrap(FONT_S, px, y + h - S(80), pw, 2, t->dim, file ? file + 1 : g->relpath);
}

void games_draw(void)
{
    const TmTheme *t = gfx_theme();
    app_header(view_title());
    int top = S(64) + S(10), bottom = gfx_h() - S(52) - S(8);
    int row = S(54);
    int listw = gfx_w() * 60 / 100;
    if (A.nview == 0) {
        const char *msg = A.query[0] ? tr("games.no_results")
                          : A.view_system == VIEW_FAVORITES ? tr("games.no_favorites")
                          : A.view_system == VIEW_RECENT ? tr("games.no_recent")
                                                         : tr("games.empty");
        gfx_text_wrap(FONT_M, S(40), top + S(40), gfx_w() - S(80), 8, t->dim, msg);
        app_footer(tr("games.hints_empty"));
        return;
    }
    int visible = (bottom - top) / row;
    if (A.games_sel < A.games_top)
        A.games_top = A.games_sel;
    if (A.games_sel >= A.games_top + visible)
        A.games_top = A.games_sel - visible + 1;
    int show_badge = A.view_system < 0;
    for (int i = 0; i < visible && (size_t)(A.games_top + i) < A.nview; i++) {
        const TmGame *g = &A.lib.games[A.view[A.games_top + i]];
        const TmSystem *sys = &A.cat.systems[g->system];
        int fav = tm_list_index(&A.fav, g->relpath) >= 0;
        app_draw_list_row(S(12), top + i * row, listw - S(20), row, A.games_top + i == A.games_sel, g->name, NULL,
                          1, sys->color, show_badge ? sys->short_name : NULL, fav);
    }
    int track = bottom - top;
    if ((int)A.nview > visible) {
        int bar = track * visible / (int)A.nview;
        if (bar < S(20))
            bar = S(20);
        int pos = (track - bar) * A.games_sel / (int)(A.nview - 1);
        gfx_rect(listw - S(5), top + pos, S(4), bar, t->dim);
    }
    char pos[32];
    snprintf(pos, sizeof pos, "%d / %zu", A.games_sel + 1, A.nview);
    gfx_text(FONT_S, gfx_w() - S(28), bottom - gfx_font_height(FONT_S) - S(6), t->dim, ALIGN_RIGHT, 0, pos);
    draw_panel(&A.lib.games[A.view[A.games_sel]], listw + S(2), top, gfx_w() - listw - S(16),
               bottom - top - gfx_font_height(FONT_S) - S(12));
    app_footer(tr("games.hints"));
}

static void jump_letter(int dir)
{
    if (!A.nview || A.view_system == VIEW_RECENT)
        return;
    char cur = A.lib.games[A.view[A.games_sel]].key[0];
    int i = A.games_sel;
    while (i + dir >= 0 && (size_t)(i + dir) < A.nview && A.lib.games[A.view[i + dir]].key[0] == cur)
        i += dir;
    if (i + dir >= 0 && (size_t)(i + dir) < A.nview)
        i += dir;
    if (dir < 0) { /* go to the first game of that letter */
        char c = A.lib.games[A.view[i]].key[0];
        while (i > 0 && A.lib.games[A.view[i - 1]].key[0] == c)
            i--;
    }
    A.games_sel = i;
}

void games_input(TmButton b)
{
    long gi = games_selected_index();
    switch (b) {
    case BTN_UP: if (A.nview) A.games_sel = A.games_sel > 0 ? A.games_sel - 1 : (int)A.nview - 1; break;
    case BTN_DOWN: if (A.nview) A.games_sel = (size_t)(A.games_sel + 1) < A.nview ? A.games_sel + 1 : 0; break;
    case BTN_LEFT:
    case BTN_L1: A.games_sel = A.games_sel > 9 ? A.games_sel - 10 : 0; break;
    case BTN_RIGHT:
    case BTN_R1: if (A.nview) A.games_sel = (size_t)(A.games_sel + 10) < A.nview ? A.games_sel + 10 : (int)A.nview - 1; break;
    case BTN_L2: jump_letter(-1); break;
    case BTN_R2: jump_letter(1); break;
    case BTN_A: app_launch(gi); break;
    case BTN_B:
        if (A.query[0]) {
            A.query[0] = '\0';
            games_rebuild();
        } else {
            A.screen = SCR_HOME;
            home_build();
        }
        break;
    case BTN_X:
        if (gi >= 0) {
            int r = tm_list_toggle(&A.fav, A.lib.games[gi].relpath);
            if (r >= 0 && tm_list_save(&A.fav, A.paths.favorites) != 0)
                LOGW("favorites not saved");
            app_toast(tr(r == 1 ? "games.fav_added" : r == 0 ? "games.fav_removed" : "games.fav_full"));
            if (A.view_system == VIEW_FAVORITES || A.fav_only)
                games_rebuild();
        }
        break;
    case BTN_Y: keyboard_open(A.query, SCR_GAMES); break;
    case BTN_SELECT:
        if (gi >= 0) {
            A.menu_return = SCR_GAMES;
            menu_open(PAGE_EMU_CHOOSE, gi, NULL);
        }
        break;
    case BTN_START:
        A.menu_return = SCR_GAMES;
        menu_open(PAGE_GAME_OPTIONS, gi, NULL);
        break;
    case BTN_MENU:
    case BTN_HOME:
        A.menu_return = SCR_GAMES;
        menu_open(PAGE_QUICK, 0, NULL);
        break;
    default:
        break;
    }
}
