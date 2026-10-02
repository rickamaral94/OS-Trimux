/* TriMux menu: state, shared widgets, launch handoff and the main loop. */
#define _GNU_SOURCE
#include "app.h"
#include "../core/buttons.h"
#include "../core/launch.h"
#include "../core/log.h"
#include "../core/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

App A;

const char *tr(const char *key) { return tm_tr(key); }

#define S(v) ((v) * gfx_h() / 768)

void app_toast(const char *msg)
{
    tm_strlcpy(A.toast, msg, sizeof A.toast);
    A.toast_until = tm_now_ms() + 2600;
    A.dirty = 1;
}

void app_dialog(int id, const char *title, const char *text, long arg, const char *sarg, int info_only)
{
    Dialog *d = &A.dlg;
    memset(d, 0, sizeof *d);
    d->active = 1;
    d->id = id;
    d->arg = arg;
    d->info_only = info_only;
    d->sel = info_only ? 0 : 1; /* destructive choices default to "No" */
    tm_strlcpy(d->title, title, sizeof d->title);
    tm_strlcpy(d->text, text, sizeof d->text);
    if (sarg)
        tm_strlcpy(d->sarg, sarg, sizeof d->sarg);
    A.dirty = 1;
}

void app_mark_settings(void) { A.settings_dirty = 1; }

void app_save_all(void)
{
    if (A.settings_dirty && tm_settings_save(&A.settings, &A.paths) == 0)
        A.settings_dirty = 0;
    if (A.overrides_dirty && tm_ini_save(&A.overrides, A.paths.overrides) == 0)
        A.overrides_dirty = 0;
}

void app_refresh_sysinfo(int force)
{
    uint64_t now = tm_now_ms();
    if (!force && now - A.si_time < 30000)
        return;
    tm_sysinfo_read(&A.si, A.paths.sd);
    A.si_time = now;
}

void app_apply_language(void)
{
    char dir[TM_PATH_MAX];
    tm_path_join(dir, sizeof dir, A.paths.share, "i18n");
    tm_i18n_load(dir, tm_ini_get(&A.settings, "general", "language", TM_DEFAULT_LANG));
}

static void draw_progress_message(const char *msg)
{
    gfx_clear();
    const TmTheme *t = gfx_theme();
    gfx_text(FONT_L, gfx_w() / 2, gfx_h() / 2 - gfx_font_height(FONT_L), t->text, ALIGN_CENTER, 0, msg);
    gfx_present();
}

void app_rescan(void)
{
    draw_progress_message(tr("library.scanning"));
    tm_library_scan(&A.lib, &A.cat, A.paths.sd, (int)tm_ini_get_long(&A.settings, "general", "clean_names", 1));
    if (tm_library_save(&A.lib, &A.cat, A.paths.library) != 0)
        LOGW("library: index not saved (card read-only or full?)");
    home_build();
    if (A.screen == SCR_GAMES)
        games_rebuild();
    char msg[128];
    snprintf(msg, sizeof msg, tr("library.found_n"), A.lib.count);
    app_toast(msg);
    covers_start(1, 0); /* new games get covers if automatic covers are on */
}

void app_exit(int code)
{
    net_ftp_stop(); /* the file server never outlives the menu */
    app_save_all();
    A.exit_code = code;
    A.running = 0;
}

const TmEmulator *app_resolve_emu(const TmGame *g, int *is_override)
{
    const TmSystem *sys = &A.cat.systems[g->system];
    const char *ov = tm_ini_get(&A.overrides, "games", g->relpath, NULL);
    const char *pref = tm_ini_get(&A.settings, "emulators", sys->id, NULL);
    const TmEmulator *em = tm_catalog_resolve(&A.cat, sys, ov, pref, A.paths.cores);
    if (is_override)
        *is_override = ov && em && strcmp(em->id, ov) == 0;
    return em;
}

int app_bios_status(const TmSystem *sys, char *missing, size_t size)
{
    int nmiss = 0;
    if (missing)
        missing[0] = '\0';
    for (int i = 0; i < sys->nbios; i++) {
        char rel[256], abs[TM_PATH_MAX];
        snprintf(rel, sizeof rel, "Bios/%s", sys->bios[i]);
        if (tm_path_join(abs, sizeof abs, A.paths.sd, rel) == 0 && tm_file_exists(abs))
            continue;
        nmiss++;
        if (missing && strlen(missing) + strlen(sys->bios[i]) + 3 < size) {
            if (missing[0])
                strcat(missing, ", ");
            strcat(missing, sys->bios[i]);
        }
    }
    return nmiss;
}

static void save_ui_state(void)
{
    TmIni s;
    tm_ini_init(&s);
    tm_ini_set_long(&s, "ui", "screen", A.screen == SCR_GAMES ? 1 : 0);
    tm_ini_set_long(&s, "ui", "home_sel", A.home_sel);
    tm_ini_set_long(&s, "ui", "view", A.view_system);
    tm_ini_set(&s, "ui", "query", A.query);
    long gi = games_selected_index();
    if (gi >= 0)
        tm_ini_set(&s, "ui", "game", A.lib.games[gi].relpath);
    char p[TM_PATH_MAX];
    if (tm_path_join(p, sizeof p, A.paths.tmp, "ui_state.ini") == 0)
        tm_ini_save(&s, p);
    tm_ini_free(&s);
}

static void restore_ui_state(void)
{
    char p[TM_PATH_MAX];
    if (tm_path_join(p, sizeof p, A.paths.tmp, "ui_state.ini") != 0 || !tm_file_exists(p))
        return;
    TmIni s;
    tm_ini_init(&s);
    tm_ini_load(&s, p);
    unlink(p);
    A.home_sel = (int)tm_ini_get_long(&s, "ui", "home_sel", 0);
    if ((size_t)A.home_sel >= home_count())
        A.home_sel = 0;
    if (tm_ini_get_long(&s, "ui", "screen", 0) == 1) {
        tm_strlcpy(A.query, tm_ini_get(&s, "ui", "query", ""), sizeof A.query);
        int v = (int)tm_ini_get_long(&s, "ui", "view", VIEW_ALL);
        if (v >= (int)A.cat.nsystems)
            v = VIEW_ALL;
        games_open(v);
        const char *rel = tm_ini_get(&s, "ui", "game", NULL);
        for (size_t i = 0; rel && i < A.nview; i++)
            if (strcmp(A.lib.games[A.view[i]].relpath, rel) == 0) {
                A.games_sel = (int)i;
                break;
            }
    }
    tm_ini_free(&s);
}

void app_launch(long gi)
{
    if (gi < 0 || (size_t)gi >= A.lib.count)
        return;
    const TmGame *g = &A.lib.games[gi];
    const TmSystem *sys = &A.cat.systems[g->system];
    const TmEmulator *em = app_resolve_emu(g, NULL);
    char msg[512];
    if (!em) {
        snprintf(msg, sizeof msg, tr("launch.no_emu"), sys->name);
        app_dialog(DLG_INFO, tr("launch.cannot"), msg, 0, NULL, 1);
        return;
    }
    char missing[256];
    if (sys->bios_required && app_bios_status(sys, missing, sizeof missing) == sys->nbios) {
        snprintf(msg, sizeof msg, tr("launch.bios_missing"), missing);
        app_dialog(DLG_INFO, tr("launch.cannot"), msg, 0, NULL, 1);
        return;
    }
    char abs[TM_PATH_MAX];
    if (tm_path_join(abs, sizeof abs, A.paths.sd, g->relpath) != 0 || !tm_file_exists(abs)) {
        app_dialog(DLG_INFO, tr("launch.cannot"), tr("launch.err.rom_missing"), 0, NULL, 1);
        return;
    }
    if ((sys->experimental || em->experimental) && A.dlg.id != DLG_EXPERIMENTAL) {
        snprintf(msg, sizeof msg, "%s", tr(em->note_key[0] ? em->note_key : "launch.experimental"));
        app_dialog(DLG_EXPERIMENTAL, tr("launch.experimental.title"), msg, gi, NULL, 0);
        return;
    }
    tm_list_push_front(&A.recent, g->relpath);
    if (tm_list_save(&A.recent, A.paths.recents) != 0)
        LOGW("recent list not saved");
    if (tm_launch_write(&A.paths, sys->id, g->relpath, em->id) != 0) {
        app_dialog(DLG_INFO, tr("launch.cannot"), tr("launch.err.request"), 0, NULL, 1);
        return;
    }
    save_ui_state();
    LOGI("ui: launch %s via %s", g->relpath, em->id);
    app_exit(EXIT_LAUNCH);
}

/* ------------------------------------------------------------ extra controls */

void app_cycle_profile(int dir)
{
    static const char *const ids[] = {"auto", "economy", "balanced", "performance"};
    const char *p = tm_ini_get(&A.settings, "power", "profile", "auto");
    int i = 0, n = (int)TM_ARRAY_LEN(ids);
    for (int k = 0; k < n; k++)
        if (strcmp(p, ids[k]) == 0)
            i = k;
    i = (i + dir + n) % n;
    tm_ini_set(&A.settings, "power", "profile", ids[i]);
    app_mark_settings();
    LOGI("ui: power profile set to %s", ids[i]);
}

static void leds_set_all(int off)
{
    for (size_t i = 0; i < A.leds.nzones; i++) {
        char sec[32];
        snprintf(sec, sizeof sec, "leds.%s", A.leds.zones[i].id);
        TmLedSetting st = {
            .on = off ? 0 : (int)tm_ini_get_long(&A.settings, sec, "on", 1),
            .color = (unsigned)strtoul(tm_ini_get(&A.settings, sec, "color", "FFFFFF"), NULL, 16),
            .brightness = (int)tm_ini_get_long(&A.settings, sec, "brightness", 60),
            .effect = (int)tm_ini_get_long(&A.settings, sec, "effect", TM_LED_EFFECT_STATIC),
        };
        tm_leds_apply(&A.leds, A.leds.zones[i].id, &st);
    }
}

void app_key_action(TmButton b)
{
    const char *key = b == BTN_F1 ? "f1" : "f2";
    TmKeyAction a = tm_key_action_parse(tm_ini_get(&A.settings, "buttons", key, NULL),
                                        b == BTN_F1 ? TM_KEY_FAVORITE : TM_KEY_RANDOM);
    long gi = games_selected_index();
    switch (a) {
    case TM_KEY_QUICK:
        A.menu_return = A.screen == SCR_MENU ? A.menu_return : A.screen;
        menu_open(PAGE_QUICK, 0, NULL);
        break;
    case TM_KEY_FAVORITE:
        if (gi >= 0) {
            int r = tm_list_toggle(&A.fav, A.lib.games[gi].relpath);
            tm_list_save(&A.fav, A.paths.favorites);
            app_toast(tr(r == 1 ? "games.fav_added" : r == 0 ? "games.fav_removed" : "games.fav_full"));
        } else {
            app_toast(tr("keys.no_game"));
        }
        break;
    case TM_KEY_SEARCH:
        if (A.screen != SCR_GAMES)
            games_open(VIEW_ALL);
        keyboard_open(A.query, SCR_GAMES);
        break;
    case TM_KEY_RANDOM:
        if (A.lib.count) {
            if (A.screen != SCR_GAMES || A.nview == 0)
                games_open(VIEW_ALL);
            if (A.nview) {
                A.games_sel = rand() % (int)A.nview;
                app_toast(tr("keys.random_done"));
            }
        }
        break;
    case TM_KEY_RECENT: A.query[0] = '\0'; A.nmenus = 0; games_open(VIEW_RECENT); break;
    case TM_KEY_POWER: {
        if (!A.power.has_cpufreq) {
            app_toast(tr("power.unavailable"));
            break;
        }
        app_cycle_profile(1);
        const char *p = tm_ini_get(&A.settings, "power", "profile", "auto");
        char k[48], msg[128];
        snprintf(k, sizeof k, strcmp(p, "auto") == 0 ? "power.auto" : "power.%s", p);
        snprintf(msg, sizeof msg, "%s: %s", tr("power.profile"), tr(k));
        app_toast(msg);
        if (A.screen == SCR_MENU)
            menu_rebuild();
        break;
    }
    case TM_KEY_LEDS:
        if (!A.leds.available) {
            app_toast(tr("leds.unavailable"));
            break;
        }
        {
            int off = !tm_ini_get_long(&A.settings, "leds", "user_off", 0);
            tm_ini_set_long(&A.settings, "leds", "user_off", off);
            tm_ini_set_long(&A.settings, "leds", "managed", 1);
            app_mark_settings();
            leds_set_all(off);
            app_toast(tr(off ? "keys.leds_off" : "keys.leds_on"));
        }
        break;
    default:
        break;
    }
}

/* Called once per second: reacts to the side switch and keeps the CPU limit
 * at or below the menu profile (firmware FN shortcuts can raise it). */
void app_switch_tick(void)
{
    static int last = -2;
    static int ticks;
    TmSwitchAction now = tm_switch_active(&A.settings);
    if (last == -2) {
        last = now;
    } else if ((int)now != last) {
        TmSwitchAction before = (TmSwitchAction)last;
        if ((before == TM_SWITCH_LEDS_OFF || now == TM_SWITCH_LEDS_OFF) && A.leds.available)
            leds_set_all(now == TM_SWITCH_LEDS_OFF);
        if ((before == TM_SWITCH_MUTE || now == TM_SWITCH_MUTE) && tm_speaker_mute_available())
            tm_speaker_mute(now == TM_SWITCH_MUTE);
        if (A.power.has_cpufreq && (before == TM_SWITCH_BOOST || now == TM_SWITCH_BOOST || before == TM_SWITCH_ECONOMY ||
                                    now == TM_SWITCH_ECONOMY))
            tm_power_apply(&A.power, tm_power_profile(now == TM_SWITCH_BOOST     ? "boost"
                                                      : now == TM_SWITCH_ECONOMY ? "economy"
                                                                                 : TM_POWER_DEFAULT));
        char k[48];
        snprintf(k, sizeof k, "switch.toast.%s", tm_switch_action_id(now == TM_SWITCH_NONE ? before : now));
        app_toast(now == TM_SWITCH_NONE ? tr("switch.toast.off") : tr(k));
        LOGI("ui: side switch %s -> %s", tm_switch_action_id(before), tm_switch_action_id(now));
        last = now;
        if (A.screen == SCR_MENU)
            menu_rebuild();
    }
    if (++ticks >= 10 && A.power.has_cpufreq) {
        ticks = 0;
        const TmPowerProfile *menu = tm_power_profile(now == TM_SWITCH_ECONOMY ? "economy"
                                                      : now == TM_SWITCH_BOOST ? "boost"
                                                                               : TM_POWER_DEFAULT);
        TmPowerTarget t;
        long mx = -1;
        if (tm_power_plan(&A.power, menu, &t) == 0 && tm_power_read(&A.power, NULL, NULL, &mx, NULL, 0) == 0 &&
            mx > t.max_khz) {
            LOGW("ui: CPU limit raised externally to %ld kHz, restoring %s", mx, menu->id);
            tm_power_apply(&A.power, menu);
        }
    }
}

/* ------------------------------------------------------------ widgets */

void app_header(const char *title)
{
    const TmTheme *t = gfx_theme();
    int h = S(64);
    gfx_rect(0, 0, gfx_w(), h, t->panel);
    gfx_text(FONT_M, S(24), (h - gfx_font_height(FONT_M)) / 2, t->text, ALIGN_LEFT, gfx_w() - S(330), title);
    int x = gfx_w() - S(24);
    app_refresh_sysinfo(0);
    if (A.si.battery_pct >= 0) {
        char b[16];
        snprintf(b, sizeof b, "%d%%", A.si.battery_pct);
        int bh = S(22);
        gfx_battery(x - bh * 2 - S(3), (h - bh) / 2, bh, A.si.battery_pct, A.si.charging);
        x -= bh * 2 + S(14);
        x -= gfx_text(FONT_S, x, (h - gfx_font_height(FONT_S)) / 2, t->text, ALIGN_RIGHT, 0, b) + S(18);
    }
    time_t now = time(NULL);
    struct tm tmv;
    if (localtime_r(&now, &tmv) && tmv.tm_year + 1900 >= 2024) { /* RTC set */
        char c[16];
        strftime(c, sizeof c, "%H:%M", &tmv);
        gfx_text(FONT_S, x, (h - gfx_font_height(FONT_S)) / 2, t->dim, ALIGN_RIGHT, 0, c);
    }
}

/* hints: "A:Abrir|B:Voltar" */
void app_footer(const char *hints)
{
    const TmTheme *t = gfx_theme();
    int h = S(52), y = gfx_h() - h;
    gfx_rect(0, y, gfx_w(), h, t->panel);
    char buf[512];
    tm_strlcpy(buf, hints, sizeof buf);
    int x = S(20);
    char *save = NULL;
    for (char *tok = strtok_r(buf, "|", &save); tok; tok = strtok_r(NULL, "|", &save)) {
        char *colon = strchr(tok, ':');
        if (!colon)
            continue;
        *colon = '\0';
        int bh = S(32), by = y + (h - bh) / 2;
        int bw = gfx_text_width(FONT_S, tok) + S(16);
        if (bw < bh)
            bw = bh;
        gfx_round_rect(x, by, bw, bh, S(8), t->panel2);
        gfx_text(FONT_S, x + bw / 2, by + (bh - gfx_font_height(FONT_S)) / 2, t->text, ALIGN_CENTER, 0, tok);
        x += bw + S(8);
        x += gfx_text(FONT_S, x, y + (h - gfx_font_height(FONT_S)) / 2, t->dim, ALIGN_LEFT, 0, colon + 1) + S(22);
        if (x > gfx_w() - S(80))
            break;
    }
}

void app_draw_list_row(int x, int y, int w, int h, int selected, const char *label, const char *value,
                       int enabled, uint32_t badge_color, const char *badge, int star)
{
    const TmTheme *t = gfx_theme();
    if (selected)
        gfx_round_rect(x, y + S(2), w, h - S(4), S(10), t->sel);
    uint32_t fg = selected ? t->accent_text : enabled ? t->text : t->dim;
    int tx = x + S(18);
    int fy = y + (h - gfx_font_height(FONT_M)) / 2;
    if (badge && *badge) {
        int bh = S(30);
        tx += gfx_badge(tx, y + (h - bh) / 2, bh, badge_color, badge) + S(14);
    }
    int vw = 0;
    if (value && *value)
        vw = gfx_text(FONT_S, x + w - S(18), y + (h - gfx_font_height(FONT_S)) / 2,
                      selected ? t->accent_text : t->dim, ALIGN_RIGHT, w / 2, value) + S(16);
    int sw = star ? S(30) : 0;
    gfx_text(FONT_M, tx, fy, fg, ALIGN_LEFT, x + w - tx - vw - sw - S(12), label);
    if (star)
        gfx_star(x + w - vw - S(30), y + h / 2, S(11), selected ? t->accent_text : t->warn);
}

static void draw_dialog(void)
{
    Dialog *d = &A.dlg;
    const TmTheme *t = gfx_theme();
    gfx_rect_a(0, 0, gfx_w(), gfx_h(), 0x000000, 170);
    int w = S(760), x = (gfx_w() - w) / 2;
    int th = gfx_font_height(FONT_S) * 8;
    int h = S(120) + th + S(70);
    int y = (gfx_h() - h) / 2;
    gfx_round_rect(x, y, w, h, S(16), t->panel);
    gfx_frame(x, y, w, h, 2, t->accent);
    gfx_text(FONT_L, x + S(28), y + S(20), t->text, ALIGN_LEFT, w - S(56), d->title);
    gfx_text_wrap(FONT_S, x + S(28), y + S(84), w - S(56), 8, t->text, d->text);
    int by = y + h - S(70), bw = S(200), bh = S(50);
    if (d->info_only) {
        gfx_round_rect(x + (w - bw) / 2, by, bw, bh, S(10), t->sel);
        gfx_text(FONT_M, x + w / 2, by + (bh - gfx_font_height(FONT_M)) / 2, t->accent_text, ALIGN_CENTER, 0,
                 tr("common.ok"));
        return;
    }
    const char *labels[2] = {tr("common.yes"), tr("common.no")};
    for (int i = 0; i < 2; i++) {
        int bx = x + w / 2 + (i == 0 ? -bw - S(14) : S(14));
        gfx_round_rect(bx, by, bw, bh, S(10), d->sel == i ? t->sel : t->panel2);
        gfx_text(FONT_M, bx + bw / 2, by + (bh - gfx_font_height(FONT_M)) / 2, d->sel == i ? t->accent_text : t->text,
                 ALIGN_CENTER, 0, labels[i]);
    }
}

static void dialog_input(TmButton b)
{
    Dialog *d = &A.dlg;
    if (d->info_only) {
        if (b == BTN_A || b == BTN_B || b == BTN_START) {
            d->active = 0;
            if (d->id == DLG_IDLE)
                A.idle_warned = 0;
            if (d->id == DLG_FTP)
                menu_dialog_result(DLG_FTP, 0, NULL, 1);
        }
        return;
    }
    if (b == BTN_LEFT || b == BTN_RIGHT || b == BTN_UP || b == BTN_DOWN)
        d->sel = !d->sel;
    else if (b == BTN_B) {
        d->active = 0;
        menu_dialog_result(d->id, d->arg, d->sarg, 0);
    } else if (b == BTN_A) {
        d->active = 0;
        int yes = d->sel == 0;
        if (d->id == DLG_EXPERIMENTAL) {
            if (yes) {
                d->id = DLG_EXPERIMENTAL; /* marks the warning as acknowledged */
                app_launch(d->arg);
            }
            d->id = DLG_NONE;
            return;
        }
        menu_dialog_result(d->id, d->arg, d->sarg, yes);
    }
}

static void draw_toast(void)
{
    if (!A.toast[0] || tm_now_ms() > A.toast_until)
        return;
    const TmTheme *t = gfx_theme();
    int w = gfx_text_width(FONT_S, A.toast) + S(48);
    int h = S(52), x = (gfx_w() - w) / 2, y = gfx_h() - S(52) - h - S(18);
    gfx_round_rect(x, y, w, h, S(12), t->panel2);
    gfx_frame(x, y, w, h, 2, t->accent);
    gfx_text(FONT_S, gfx_w() / 2, y + (h - gfx_font_height(FONT_S)) / 2, t->text, ALIGN_CENTER, 0, A.toast);
}

static void draw(void)
{
    gfx_clear();
    switch (A.screen) {
    case SCR_HOME: home_draw(); break;
    case SCR_GAMES: games_draw(); break;
    case SCR_MENU: menu_draw(); break;
    case SCR_WIZARD: wizard_draw(); break;
    case SCR_KEYBOARD: keyboard_draw(); break;
    case SCR_CTRLTEST: ctrltest_draw(); break;
    case SCR_INFO: info_draw(); break;
    }
    if (A.dlg.active)
        draw_dialog();
    draw_toast();
    gfx_present();
}

static void dispatch(TmButton b)
{
    if (b == BTN_NONE)
        return;
    A.last_input = tm_now_ms();
    A.dirty = 1;
    if (A.dlg.active) {
        dialog_input(b);
        return;
    }
    /* F1/F2 run the user's assigned action on the main screens; the
     * controller test and text entry keep seeing them as plain buttons. */
    if ((b == BTN_F1 || b == BTN_F2) && (A.screen == SCR_HOME || A.screen == SCR_GAMES || A.screen == SCR_MENU)) {
        app_key_action(b);
        return;
    }
    switch (A.screen) {
    case SCR_HOME: home_input(b); break;
    case SCR_GAMES: games_input(b); break;
    case SCR_MENU: menu_input(b); break;
    case SCR_WIZARD: wizard_input(b); break;
    case SCR_KEYBOARD: keyboard_input(b); break;
    case SCR_CTRLTEST: ctrltest_input(b); break;
    case SCR_INFO: info_input(b); break;
    }
}

static void check_idle(void)
{
    long mins = tm_ini_get_long(&A.settings, "general", "idle_poweroff_min", 0);
    if (mins <= 0 || A.si.charging == 1)
        return;
    uint64_t idle = tm_now_ms() - A.last_input;
    if (!A.idle_warned && idle > (uint64_t)mins * 60000u) {
        A.idle_warned = 1;
        app_dialog(DLG_IDLE, tr("idle.title"), tr("idle.text"), 0, NULL, 1);
    } else if (A.idle_warned && A.dlg.active && A.dlg.id == DLG_IDLE && idle > (uint64_t)mins * 60000u + 15000u) {
        LOGI("ui: idle power-off after %ld min", mins);
        app_exit(EXIT_POWEROFF);
    }
}

static int load_everything(void)
{
    if (tm_paths_init(&A.paths) != 0)
        return -1;
    tm_paths_ensure(&A.paths);
    char log[TM_PATH_MAX];
    tm_path_join(log, sizeof log, A.paths.logdir, "trimux.log");
    tm_log_init(log, 256 * 1024, "ui");
    int created = tm_settings_load(&A.settings, &A.paths);
    if (created)
        A.settings_dirty = 1;
    if (tm_ini_get_long(&A.settings, "diag", "verbose", 0))
        tm_log_set_level(TM_LOG_DEBUG);
    tm_ini_init(&A.overrides);
    tm_ini_load(&A.overrides, A.paths.overrides);
    app_apply_language();
    char s[TM_PATH_MAX], e[TM_PATH_MAX];
    tm_path_join(s, sizeof s, A.paths.share, "systems.ini");
    tm_path_join(e, sizeof e, A.paths.share, "emulators.ini");
    if (tm_catalog_load(&A.cat, s, e) != 0 || A.cat.nsystems == 0) {
        LOGE("ui: catalog missing in %s", A.paths.share);
        return -1;
    }
    tm_list_init(&A.fav, TM_LIST_MAX);
    tm_list_init(&A.recent, TM_RECENT_MAX);
    tm_list_load(&A.fav, A.paths.favorites);
    tm_list_load(&A.recent, A.paths.recents);
    tm_library_init(&A.lib);
    tm_power_detect(&A.power);
    tm_leds_detect(&A.leds);
    return 0;
}

static void boot_ok(void)
{
    char p[TM_PATH_MAX];
    if (tm_path_join(p, sizeof p, A.paths.state, "bootcount") == 0)
        tm_atomic_write(p, "0\n", 2);
}

int app_main(int argc, char **argv)
{
    int win_w = 0, win_h = 0;
    const char *shot = NULL, *script = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--window") == 0 && i + 2 < argc) {
            win_w = atoi(argv[++i]);
            win_h = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shot = argv[++i]; /* render one frame and exit (docs/tests) */
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            /* "DOWN,A,shot=x.bmp,B": scripted input for automated UI tests */
            script = argv[++i];
        }
    }
    memset(&A, 0, sizeof A);
    if (load_everything() != 0)
        return 2;
    gfx_set_theme(tm_ini_get(&A.settings, "general", "theme", "dark"));
    char font[TM_PATH_MAX];
    tm_path_join(font, sizeof font, A.paths.share, "fonts/DejaVuSans.ttf");
    if (gfx_init(font, "/usr/trimui/res/regular.ttf", win_w, win_h) != 0)
        return 2;
    input_init(&A.settings);

    /* Holding SELECT while the menu starts opens the stock TrimUI interface. */
    uint64_t t0 = tm_now_ms();
    SDL_Event ev;
    while (tm_now_ms() - t0 < 250) {
        while (SDL_PollEvent(&ev))
            input_event(&ev);
        SDL_Delay(10);
    }
    if (input_state()->pressed[BTN_SELECT] && !shot && !script) {
        LOGI("ui: SELECT held at start, opening stock UI");
        gfx_quit();
        return EXIT_STOCK;
    }

    draw_progress_message(tr("app.loading"));
    if (tm_library_load(&A.lib, &A.cat, A.paths.library) != 0 || tm_library_is_stale(&A.lib, &A.cat, A.paths.sd))
        app_rescan();
    A.toast[0] = '\0';
    home_build();
    A.screen = SCR_HOME;
    A.view_system = VIEW_ALL;
    restore_ui_state();
    if (!tm_ini_get_long(&A.settings, "general", "wizard_done", 0))
        wizard_open();
    app_refresh_sysinfo(1);
    A.running = 1;
    A.dirty = 1;
    A.last_input = tm_now_ms();

    if (shot) {
        draw();
        gfx_screenshot(shot);
        A.running = 0;
    }
    if (script) {
        char buf[2048];
        tm_strlcpy(buf, script, sizeof buf);
        char *save = NULL;
        for (char *tok = strtok_r(buf, ",", &save); tok && A.running; tok = strtok_r(NULL, ",", &save)) {
            if (strncmp(tok, "shot=", 5) == 0) {
                draw();
                gfx_screenshot(tok + 5);
                continue;
            }
            if (strncmp(tok, "wait=", 5) == 0) { /* let background polling run */
                usleep((useconds_t)atoi(tok + 5) * 1000u);
                net_tick();
                continue;
            }
            for (int b = 0; b < BTN_COUNT; b++)
                if (strcmp(tok, input_button_name((TmButton)b)) == 0)
                    dispatch((TmButton)b);
        }
        if (A.running)
            A.running = 0;
    }
    int first_frame = 1;
    while (A.running) {
        if (A.dirty) {
            draw();
            A.dirty = 0;
            if (first_frame) {
                boot_ok();
                first_frame = 0;
            }
        }
        int timeout = (input_any_held() || A.screen == SCR_CTRLTEST) ? 16 : 1000;
        if (SDL_WaitEventTimeout(&ev, timeout)) {
            do {
                if (ev.type == SDL_QUIT)
                    app_exit(EXIT_RESTART);
                dispatch(input_event(&ev));
            } while (A.running && SDL_PollEvent(&ev));
        }
        dispatch(input_repeat());
        ctrltest_tick();
        if (A.toast[0] && tm_now_ms() > A.toast_until) {
            A.toast[0] = '\0';
            A.dirty = 1;
        }
        static uint64_t last_tick;
        if (tm_now_ms() - last_tick > 1000) { /* header clock/battery, live info pages */
            last_tick = tm_now_ms();
            if (A.screen == SCR_INFO || A.screen == SCR_MENU)
                A.dirty = 1;
            check_idle();
            app_switch_tick();
            net_tick();
        }
    }
    net_ftp_stop();
    app_save_all();
    input_quit();
    gfx_quit();
    tm_library_free(&A.lib);
    tm_catalog_free(&A.cat);
    free(A.view);
    return A.exit_code;
}
