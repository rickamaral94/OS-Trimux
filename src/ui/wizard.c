/* First-run guide: language, folders, game search, controls, power. Every
 * step can be skipped; START skips the whole guide (reopen it in
 * Configurações > Sistema > Assistente inicial). */
#define _GNU_SOURCE
#include "app.h"
#include "../core/util.h"

#include <stdio.h>
#include <string.h>

#define S(v) ((v) * gfx_h() / 768)

enum { W_LANG, W_FOLDERS, W_SCAN, W_CONTROLS, W_POWER, W_DONE, W_COUNT };

static char g_codes[16][16];
static size_t g_ncodes;
static int g_scanned;

void wizard_open(void)
{
    A.wiz_step = W_LANG;
    A.wiz_sel = 0;
    g_scanned = 0;
    char dir[TM_PATH_MAX];
    tm_path_join(dir, sizeof dir, A.paths.share, "i18n");
    g_ncodes = tm_i18n_available(dir, g_codes, 16);
    for (size_t i = 0; i < g_ncodes; i++)
        if (strcmp(g_codes[i], tm_i18n_current()) == 0)
            A.wiz_sel = (int)i;
    A.screen = SCR_WIZARD;
}

static int options(char out[][96], int max)
{
    int n = 0;
    switch (A.wiz_step) {
    case W_LANG: {
        char dir[TM_PATH_MAX];
        tm_path_join(dir, sizeof dir, A.paths.share, "i18n");
        for (size_t i = 0; i < g_ncodes && n < max; i++)
            tm_i18n_language_name(dir, g_codes[i], out[n++], 96);
        break;
    }
    case W_FOLDERS:
        tm_strlcpy(out[n++], tr("wizard.folders.create"), 96);
        tm_strlcpy(out[n++], tr("wizard.skip_step"), 96);
        break;
    case W_SCAN:
        tm_strlcpy(out[n++], tr("wizard.scan.now"), 96);
        tm_strlcpy(out[n++], tr("wizard.skip_step"), 96);
        break;
    case W_CONTROLS:
        tm_strlcpy(out[n++], tr("wizard.controls.a"), 96);
        tm_strlcpy(out[n++], tr("wizard.controls.b"), 96);
        break;
    case W_POWER:
        tm_strlcpy(out[n++], tr("power.auto"), 96);
        tm_strlcpy(out[n++], tr("power.economy"), 96);
        tm_strlcpy(out[n++], tr("power.balanced"), 96);
        break;
    default:
        tm_strlcpy(out[n++], tr("wizard.finish"), 96);
    }
    return n;
}

void wizard_draw(void)
{
    const TmTheme *t = gfx_theme();
    char title[128], key[48];
    snprintf(title, sizeof title, "%s · %d/%d", tr("wizard.title"), A.wiz_step + 1, W_COUNT);
    app_header(title);
    /* progress dots */
    int dot = S(14), gap = S(12), total = W_COUNT * dot + (W_COUNT - 1) * gap;
    for (int i = 0; i < W_COUNT; i++)
        gfx_round_rect((gfx_w() - total) / 2 + i * (dot + gap), S(84), dot, dot, dot / 2,
                       i <= A.wiz_step ? t->accent : t->panel2);
    snprintf(key, sizeof key, "wizard.step%d.title", A.wiz_step);
    gfx_text(FONT_XL, gfx_w() / 2, S(120), t->text, ALIGN_CENTER, gfx_w() - S(80), tr(key));
    snprintf(key, sizeof key, "wizard.step%d.text", A.wiz_step);
    int y = S(200);
    y += gfx_text_wrap(FONT_S, S(90), y, gfx_w() - S(180), 6, t->dim, tr(key)) + S(20);
    if (A.wiz_step == W_SCAN && g_scanned) {
        char msg[128];
        snprintf(msg, sizeof msg, tr("library.found_n"), A.lib.count);
        gfx_text(FONT_M, gfx_w() / 2, y, t->ok, ALIGN_CENTER, 0, msg);
        y += S(50);
    }
    char opts[16][96];
    int n = options(opts, 16);
    int row = S(60), w = S(560), x = (gfx_w() - w) / 2;
    int visible = (gfx_h() - S(52) - S(20) - y) / row;
    int first = A.wiz_sel >= visible ? A.wiz_sel - visible + 1 : 0;
    for (int i = first; i < n && i - first < visible; i++)
        app_draw_list_row(x, y + (i - first) * row, w, row, i == A.wiz_sel, opts[i], NULL, 1, 0, NULL, 0);
    app_footer(tr("wizard.hints"));
}

static void finish(void)
{
    tm_ini_set_long(&A.settings, "general", "wizard_done", 1);
    app_mark_settings();
    app_save_all();
    A.screen = SCR_HOME;
    home_build();
}

static void next(void)
{
    A.wiz_step++;
    A.wiz_sel = 0;
    if (A.wiz_step >= W_COUNT)
        finish();
}

void wizard_input(TmButton b)
{
    char opts[16][96];
    int n = options(opts, 16);
    switch (b) {
    case BTN_UP: A.wiz_sel = (A.wiz_sel - 1 + n) % n; break;
    case BTN_DOWN: A.wiz_sel = (A.wiz_sel + 1) % n; break;
    case BTN_B:
        if (A.wiz_step > 0) {
            A.wiz_step--;
            A.wiz_sel = 0;
        }
        break;
    case BTN_START:
        app_dialog(DLG_WIZ_SKIP, tr("wizard.skip_all"), tr("wizard.skip_all.text"), 0, NULL, 0);
        break;
    case BTN_A:
        switch (A.wiz_step) {
        case W_LANG:
            if ((size_t)A.wiz_sel < g_ncodes) {
                tm_ini_set(&A.settings, "general", "language", g_codes[A.wiz_sel]);
                app_mark_settings();
                app_apply_language();
            }
            next();
            break;
        case W_FOLDERS:
            if (A.wiz_sel == 0) {
                int c = tm_library_create_default_dirs(&A.cat, A.paths.sd);
                char msg[96];
                snprintf(msg, sizeof msg, tr("library.mkdirs.done"), c);
                app_toast(msg);
            }
            next();
            break;
        case W_SCAN:
            if (A.wiz_sel == 0 && !g_scanned) {
                app_rescan();
                g_scanned = 1;
                A.wiz_sel = 1; /* "continue" */
                return;
            }
            next();
            break;
        case W_CONTROLS:
            tm_ini_set_long(&A.settings, "input", "swap_ab", A.wiz_sel == 1);
            app_mark_settings();
            input_reload(&A.settings);
            next();
            break;
        case W_POWER: {
            static const char *const p[] = {"auto", "economy", "balanced"};
            tm_ini_set(&A.settings, "power", "profile", p[A.wiz_sel % 3]);
            app_mark_settings();
            next();
            break;
        }
        default:
            finish();
        }
        break;
    default:
        break;
    }
}
