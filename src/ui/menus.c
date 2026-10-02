/* Settings, quick menu, per-game options, emulator choice, info pages. */
#define _GNU_SOURCE
#include "app.h"
#include "../core/buttons.h"
#include "../core/fatgrow.h"
#include "../core/log.h"
#include "../core/util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define S(v) ((v) * gfx_h() / 768)

enum {
    ACT_NONE = 0, ACT_PAGE, ACT_LANG, ACT_THEME, ACT_TOGGLE, ACT_IDLE, ACT_SWAP_AB, ACT_CTRLTEST, ACT_PROFILE,
    ACT_POWER_DEFAULT, ACT_LED_MANAGED, ACT_LED_ON, ACT_LED_COLOR, ACT_LED_BRIGHT, ACT_LED_EFFECT, ACT_RESCAN,
    ACT_MKDIRS, ACT_SET_PLAT_EMU, ACT_RESTORE_EMU, ACT_SET_GAME_EMU, ACT_INFO, ACT_WIZARD, ACT_STOCK,
    ACT_REBOOT, ACT_POWEROFF, ACT_GROW, ACT_PLAY, ACT_FAV, ACT_REMOVE_RECENT, ACT_FAV_ONLY, ACT_SEARCH,
    ACT_KEY_ACTION, ACT_SWITCH_ACTION,
};

static const struct {
    unsigned rgb;
    const char *key;
} k_colors[] = {
    {0xFFFFFF, "color.white"}, {0xFF0000, "color.red"},   {0xFF6A00, "color.orange"},
    {0xFFD000, "color.yellow"}, {0x00FF00, "color.green"}, {0x00FFFF, "color.cyan"},
    {0x0060FF, "color.blue"},  {0x8000FF, "color.purple"}, {0xFF00FF, "color.pink"},
};


static Menu *cur(void) { return A.nmenus ? &A.menus[A.nmenus - 1] : NULL; }

static MenuItem *add(Menu *m, int id, const char *label, const char *value, const char *desc)
{
    if (m->n >= MENU_MAX_ITEMS)
        return &m->items[MENU_MAX_ITEMS - 1];
    MenuItem *it = &m->items[m->n++];
    memset(it, 0, sizeof *it);
    it->id = id;
    it->enabled = 1;
    tm_strlcpy(it->label, label ? label : "", sizeof it->label);
    tm_strlcpy(it->value, value ? value : "", sizeof it->value);
    tm_strlcpy(it->desc, desc ? desc : "", sizeof it->desc);
    return it;
}

static MenuItem *add_page(Menu *m, int page, const char *label, const char *desc)
{
    MenuItem *it = add(m, ACT_PAGE, label, "›", desc);
    it->arg = page;
    return it;
}

/* pt_BR (and most languages besides English) write decimals with a comma. */
static void localize_decimal(char *s)
{
    if (strncmp(tm_i18n_current(), "en", 2) == 0)
        return;
    for (; *s; s++)
        if (*s == '.' && s[1] >= '0' && s[1] <= '9')
            *s = ',';
}

static const char *onoff(int v) { return tr(v ? "common.on" : "common.off"); }

static long setting_long(const char *sec, const char *key, long def)
{
    return tm_ini_get_long(&A.settings, sec, key, def);
}

static void profile_desc(const char *id, char *out, size_t size)
{
    if (strcmp(id, "auto") == 0) {
        tm_strlcpy(out, tr("power.auto.desc"), size);
        return;
    }
    const TmPowerProfile *p = tm_power_profile(id);
    TmPowerTarget t;
    char freq[64] = "";
    if (p && A.power.has_cpufreq && tm_power_plan(&A.power, p, &t) == 0)
        snprintf(freq, sizeof freq, " (%.1f–%.1f GHz)", t.min_khz / 1e6, t.max_khz / 1e6);
    localize_decimal(freq);
    snprintf(out, size, "%s%s", p ? tr(p->desc_key) : "", freq);
}

static const char *profile_name(const char *id)
{
    if (strcmp(id, "auto") == 0)
        return tr("power.auto");
    const TmPowerProfile *p = tm_power_profile(id);
    return p ? tr(p->name_key) : id;
}

/* ------------------------------------------------------------ pages */

static void page_settings(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.title"), sizeof m->title);
    add_page(m, PAGE_LANGUAGE, tr("settings.language"), tr("settings.language.desc"));
    add_page(m, PAGE_APPEARANCE, tr("settings.appearance"), tr("settings.appearance.desc"));
    add_page(m, PAGE_CONTROLS, tr("settings.controls"), tr("settings.controls.desc"));
    add_page(m, PAGE_POWER, tr("settings.power"), tr("settings.power.desc"));
    add_page(m, PAGE_DISPLAY, tr("settings.display"), tr("settings.display.desc"));
    MenuItem *l = add_page(m, PAGE_LEDS, tr("settings.leds"), tr(A.leds.available ? "settings.leds.desc" : "leds.unavailable"));
    l->enabled = A.leds.available;
    add_page(m, PAGE_LIBRARY, tr("settings.library"), tr("settings.library.desc"));
    add_page(m, PAGE_EMULATORS, tr("settings.emulators"), tr("settings.emulators.desc"));
    add_page(m, PAGE_STORAGE, tr("settings.storage"), tr("settings.storage.desc"));
    add_page(m, PAGE_SYSTEM, tr("settings.system"), tr("settings.system.desc"));
}

static void page_language(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.language"), sizeof m->title);
    char dir[TM_PATH_MAX], codes[16][16];
    tm_path_join(dir, sizeof dir, A.paths.share, "i18n");
    size_t n = tm_i18n_available(dir, codes, 16);
    for (size_t i = 0; i < n; i++) {
        char name[64];
        tm_i18n_language_name(dir, codes[i], name, sizeof name);
        MenuItem *it = add(m, ACT_LANG, name, strcmp(codes[i], tm_i18n_current()) == 0 ? "✓" : "", tr("settings.language.desc"));
        tm_strlcpy(it->sarg, codes[i], sizeof it->sarg);
    }
}

static void page_appearance(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.appearance"), sizeof m->title);
    const char *th = tm_ini_get(&A.settings, "general", "theme", "dark");
    char key[32];
    snprintf(key, sizeof key, "theme.%s", th);
    add(m, ACT_THEME, tr("settings.theme"), tr(key), tr("settings.theme.desc"));
    MenuItem *it = add(m, ACT_TOGGLE, tr("settings.clean_names"), onoff((int)setting_long("general", "clean_names", 1)),
                       tr("settings.clean_names.desc"));
    tm_strlcpy(it->sarg, "general/clean_names", sizeof it->sarg);
    it = add(m, ACT_TOGGLE, tr("settings.show_empty"), onoff((int)setting_long("general", "show_empty", 0)),
             tr("settings.show_empty.desc"));
    tm_strlcpy(it->sarg, "general/show_empty", sizeof it->sarg);
}

static void page_controls(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.controls"), sizeof m->title);
    int swap = (int)setting_long("input", "swap_ab", 0);
    add(m, ACT_SWAP_AB, tr("controls.confirm"), swap ? "B" : "A", tr("controls.confirm.desc"));
    add(m, ACT_CTRLTEST, tr("controls.test"), "›", tr("controls.test.desc"));
    add_page(m, PAGE_BUTTONS, tr("buttons.title"), tr("buttons.desc"));
    add_page(m, PAGE_HOTKEYS, tr("controls.hotkeys"), tr("controls.hotkeys.desc"));
}

static void page_buttons(Menu *m)
{
    tm_strlcpy(m->title, tr("buttons.title"), sizeof m->title);
    static const char *const keys[] = {"f1", "f2"};
    for (int i = 0; i < 2; i++) {
        TmKeyAction a = tm_key_action_parse(tm_ini_get(&A.settings, "buttons", keys[i], NULL),
                                            i == 0 ? TM_KEY_FAVORITE : TM_KEY_RANDOM);
        char lk[32], vk[48];
        snprintf(lk, sizeof lk, "buttons.%s", keys[i]);
        snprintf(vk, sizeof vk, "keyaction.%s", tm_key_action_id(a));
        MenuItem *it = add(m, ACT_KEY_ACTION, tr(lk), tr(vk), tr("buttons.fkey.desc"));
        tm_strlcpy(it->sarg, keys[i], sizeof it->sarg);
    }
    int raw = tm_switch_raw();
    TmSwitchAction sa = tm_switch_action(&A.settings);
    char vk[48];
    snprintf(vk, sizeof vk, "switchaction.%s", tm_switch_action_id(sa));
    MenuItem *it = add(m, ACT_SWITCH_ACTION, tr("buttons.switch"), tr(vk),
                       tr(raw < 0 ? "buttons.switch.unavailable" : "buttons.switch.desc"));
    it->enabled = raw >= 0;
    it = add(m, ACT_TOGGLE, tr("buttons.switch_invert"), onoff((int)setting_long("buttons", "switch_invert", 0)),
             tr("buttons.switch_invert.desc"));
    tm_strlcpy(it->sarg, "buttons/switch_invert", sizeof it->sarg);
    it->enabled = raw >= 0;
    int on = tm_switch_on(&A.settings);
    char v[48];
    snprintf(v, sizeof v, "%s (%d)", tr(on < 0 ? "common.na" : on ? "buttons.pos_on" : "buttons.pos_off"), raw);
    add(m, ACT_NONE, tr("buttons.switch_now"), v, tr("buttons.switch_now.desc"));
    add(m, ACT_NONE, "HOME", tr("buttons.home.value"), tr("buttons.home.desc"));
    add(m, ACT_NONE, "MENU", tr("buttons.menu.value"), tr("buttons.menu.desc"));
}

static void page_power(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.power"), sizeof m->title);
    const char *prof = tm_ini_get(&A.settings, "power", "profile", "auto");
    char desc[320];
    profile_desc(prof, desc, sizeof desc);
    MenuItem *it = add(m, ACT_PROFILE, tr("power.profile"), profile_name(prof), desc);
    if (!A.power.has_cpufreq) {
        it->enabled = 0;
        snprintf(it->desc, sizeof it->desc, "%s %s", tr("power.unavailable"), tr(A.power.reason[0] ? A.power.reason : "power.reason.no_cpufreq"));
    }
    long idle = setting_long("general", "idle_poweroff_min", 0);
    char v[32];
    if (idle > 0)
        snprintf(v, sizeof v, tr("power.minutes"), idle);
    else
        tm_strlcpy(v, tr("power.never"), sizeof v);
    add(m, ACT_IDLE, tr("power.idle"), v, tr("power.idle.desc"));
    add(m, ACT_POWER_DEFAULT, tr("power.restore"), "", tr("power.restore.desc"));
    add(m, ACT_NONE, tr("power.thermal"), tr(A.power.has_temp ? "power.thermal.on" : "power.thermal.nosensor"),
        tr("power.thermal.desc"));
}

static void page_display(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.display"), sizeof m->title);
    /* Volume and brightness belong to the firmware's keymon service (it
     * stores them in /mnt/UDISK/system.json); TriMux only shows them. */
    char *js = tm_read_file("/mnt/UDISK/system.json", 65536, NULL);
    char vol[16] = "?", bri[16] = "?";
    if (js) {
        const char *p = strstr(js, "\"vol\"");
        if (p && (p = strchr(p, ':')))
            snprintf(vol, sizeof vol, "%d", atoi(p + 1));
        p = strstr(js, "\"brightness\"");
        if (p && (p = strchr(p, ':')))
            snprintf(bri, sizeof bri, "%d", atoi(p + 1));
        free(js);
    }
    add(m, ACT_NONE, tr("display.volume"), vol, tr("display.volume.desc"));
    add(m, ACT_NONE, tr("display.brightness"), bri, tr("display.brightness.desc"));
    add(m, ACT_NONE, tr("display.sleep"), "POWER", tr("display.sleep.desc"));
}

static void led_setting(const char *zone, TmLedSetting *s)
{
    char sec[32];
    snprintf(sec, sizeof sec, "leds.%s", zone);
    s->on = (int)setting_long(sec, "on", 1);
    s->color = (unsigned)strtoul(tm_ini_get(&A.settings, sec, "color", "FFFFFF"), NULL, 16);
    s->brightness = (int)setting_long(sec, "brightness", 60);
    s->effect = (int)setting_long(sec, "effect", TM_LED_EFFECT_STATIC);
}

static void led_store(const char *zone, const TmLedSetting *s)
{
    char sec[32], col[16];
    snprintf(sec, sizeof sec, "leds.%s", zone);
    snprintf(col, sizeof col, "%06X", s->color & 0xFFFFFF);
    tm_ini_set_long(&A.settings, sec, "on", s->on);
    tm_ini_set(&A.settings, sec, "color", col);
    tm_ini_set_long(&A.settings, sec, "brightness", s->brightness);
    tm_ini_set_long(&A.settings, sec, "effect", s->effect);
    tm_ini_set_long(&A.settings, "leds", "managed", 1);
    app_mark_settings();
    if (tm_leds_apply(&A.leds, zone, s) != 0)
        app_toast(tr("leds.apply_failed"));
}

static void page_leds(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.leds"), sizeof m->title);
    add(m, ACT_LED_MANAGED, tr("leds.managed"), onoff((int)setting_long("leds", "managed", 0)), tr("leds.managed.desc"));
    for (size_t i = 0; i < A.leds.nzones; i++) {
        MenuItem *it = add_page(m, PAGE_LED_ZONE, tr(A.leds.zones[i].name_key), tr("leds.zone.desc"));
        tm_strlcpy(it->sarg, A.leds.zones[i].id, sizeof it->sarg);
    }
}

static const char *color_name(unsigned rgb)
{
    for (size_t i = 0; i < TM_ARRAY_LEN(k_colors); i++)
        if (k_colors[i].rgb == (rgb & 0xFFFFFF))
            return tr(k_colors[i].key);
    return tr("color.custom");
}

static void page_led_zone(Menu *m)
{
    const TmLedZone *z = tm_leds_zone(&A.leds, m->sctx);
    if (!z)
        return;
    tm_strlcpy(m->title, tr(z->name_key), sizeof m->title);
    TmLedSetting s;
    led_setting(z->id, &s);
    MenuItem *it = add(m, ACT_LED_ON, tr("leds.on"), onoff(s.on), tr("leds.on.desc"));
    tm_strlcpy(it->sarg, z->id, sizeof it->sarg);
    it = add(m, ACT_LED_COLOR, tr("leds.color"), color_name(s.color), tr("leds.color.desc"));
    tm_strlcpy(it->sarg, z->id, sizeof it->sarg);
    it->badge_color = s.color;
    tm_strlcpy(it->badge, " ", sizeof it->badge);
    char v[16];
    snprintf(v, sizeof v, "%d%%", s.brightness);
    it = add(m, ACT_LED_BRIGHT, tr("leds.brightness"), z->has_brightness ? v : tr("common.na"),
             tr(z->has_brightness ? "leds.brightness.desc" : "leds.brightness.na"));
    it->enabled = z->has_brightness;
    tm_strlcpy(it->sarg, z->id, sizeof it->sarg);
    it = add(m, ACT_LED_EFFECT, tr("leds.effect"), tr(s.effect == TM_LED_EFFECT_BREATHE ? "leds.effect.breathe" : "leds.effect.static"),
             tr("leds.effect.desc"));
    tm_strlcpy(it->sarg, z->id, sizeof it->sarg);
}

static void page_library(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.library"), sizeof m->title);
    char v[32];
    snprintf(v, sizeof v, "%zu", A.lib.count);
    add(m, ACT_RESCAN, tr("library.rescan"), v, tr("library.rescan.desc"));
    add_page(m, PAGE_ADD_GAMES, tr("library.add"), tr("library.add.desc"));
    add_page(m, PAGE_FOLDERS, tr("library.folders"), tr("library.folders.desc"));
    add(m, ACT_MKDIRS, tr("library.mkdirs"), "", tr("library.mkdirs.desc"));
    add_page(m, PAGE_BIOS, tr("library.bios"), tr("library.bios.desc"));
}

static void page_add_games(Menu *m)
{
    tm_strlcpy(m->title, tr("library.add"), sizeof m->title);
    for (int i = 1; i <= 6; i++) {
        char k[32];
        snprintf(k, sizeof k, "add.step%d", i);
        add(m, ACT_NONE, tr(k), "", tr("add.desc"))->enabled = 0;
    }
}

static void page_folders(Menu *m)
{
    tm_strlcpy(m->title, tr("library.folders"), sizeof m->title);
    for (size_t s = 0; s < A.cat.nsystems; s++) {
        char dirs[8][256];
        size_t nd = tm_library_system_dirs(&A.cat.systems[s], A.paths.sd, dirs, 8);
        char desc[320] = "";
        for (size_t i = 0; i < nd; i++) {
            size_t l = strlen(desc);
            snprintf(desc + l, sizeof desc - l, "%s%s", i ? ", " : "", dirs[i]);
        }
        if (!nd)
            snprintf(desc, sizeof desc, tr("library.folder_suggest"), A.cat.systems[s].folders[0]);
        MenuItem *it = add(m, ACT_NONE, A.cat.systems[s].name, nd ? tr("library.found") : tr("library.missing"), desc);
        it->badge_color = A.cat.systems[s].color;
        tm_strlcpy(it->badge, A.cat.systems[s].short_name, sizeof it->badge);
    }
}

static void page_bios(Menu *m)
{
    tm_strlcpy(m->title, tr("library.bios"), sizeof m->title);
    for (size_t s = 0; s < A.cat.nsystems; s++) {
        const TmSystem *sys = &A.cat.systems[s];
        if (!sys->nbios)
            continue;
        char missing[256], desc[320];
        int nm = app_bios_status(sys, missing, sizeof missing);
        snprintf(desc, sizeof desc, tr(sys->bios_required ? "bios.desc_required" : "bios.desc_optional"), missing[0] ? missing : "-");
        MenuItem *it = add(m, ACT_NONE, sys->name, nm == 0 ? "OK" : tr(sys->bios_required ? "bios.missing" : "bios.optional"), desc);
        it->badge_color = sys->color;
        tm_strlcpy(it->badge, sys->short_name, sizeof it->badge);
    }
}

static void page_emulators(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.emulators"), sizeof m->title);
    for (size_t s = 0; s < A.cat.nsystems; s++) {
        const TmSystem *sys = &A.cat.systems[s];
        const TmEmulator *em = tm_catalog_resolve(&A.cat, sys, NULL, tm_ini_get(&A.settings, "emulators", sys->id, NULL), A.paths.cores);
        MenuItem *it = add(m, ACT_PAGE, sys->name, em ? em->name : tr("games.no_emulator"), tr("emulators.platform.desc"));
        it->arg = PAGE_EMU_PLATFORM;
        snprintf(it->sarg, sizeof it->sarg, "%zu", s);
        it->badge_color = sys->color;
        tm_strlcpy(it->badge, sys->short_name, sizeof it->badge);
    }
}

static void emu_items(Menu *m, const TmSystem *sys, int id, long arg, const char *current)
{
    for (int i = 0; i < sys->nemus; i++) {
        const TmEmulator *em = tm_catalog_emulator(&A.cat, sys->emulators[i]);
        if (!em)
            continue;
        int avail = tm_emulator_available(em, A.paths.cores);
        char desc[320];
        snprintf(desc, sizeof desc, "%s%s%s", em->note_key[0] ? tr(em->note_key) : "", em->note_key[0] ? " " : "",
                 avail ? "" : tr("emulators.not_installed"));
        MenuItem *it = add(m, id, em->name, !avail ? tr("emulators.missing") : (current && strcmp(current, em->id) == 0) ? "✓" : "", desc);
        it->enabled = avail;
        it->arg = arg;
        tm_strlcpy(it->sarg, em->id, sizeof it->sarg);
    }
}

static void page_emu_platform(Menu *m)
{
    long s = strtol(m->sctx, NULL, 10);
    if (s < 0 || (size_t)s >= A.cat.nsystems)
        return;
    const TmSystem *sys = &A.cat.systems[s];
    tm_strlcpy(m->title, sys->name, sizeof m->title);
    const TmEmulator *em = tm_catalog_resolve(&A.cat, sys, NULL, tm_ini_get(&A.settings, "emulators", sys->id, NULL), A.paths.cores);
    add(m, ACT_NONE, tr("emulators.default_for_platform"), "", tr("emulators.default_for_platform.desc"))->enabled = 0;
    emu_items(m, sys, ACT_SET_PLAT_EMU, s, em ? em->id : NULL);
    add(m, ACT_NONE, tr("emulators.restore_header"), "", tr("emulators.restore.desc"))->enabled = 0;
    for (int i = 0; i < sys->nemus; i++) {
        const TmEmulator *e = tm_catalog_emulator(&A.cat, sys->emulators[i]);
        if (!e || !tm_emulator_available(e, A.paths.cores))
            continue;
        char label[96];
        snprintf(label, sizeof label, tr("emulators.restore_item"), e->name);
        MenuItem *it = add(m, ACT_RESTORE_EMU, label, "", tr("emulators.restore.desc"));
        tm_strlcpy(it->sarg, e->id, sizeof it->sarg);
    }
}

static void page_emu_choose(Menu *m)
{
    long gi = m->ctx;
    if (gi < 0 || (size_t)gi >= A.lib.count)
        return;
    const TmGame *g = &A.lib.games[gi];
    const TmSystem *sys = &A.cat.systems[g->system];
    snprintf(m->title, sizeof m->title, "%s · %s", tr("emulators.choose"), g->name);
    const char *ov = tm_ini_get(&A.overrides, "games", g->relpath, NULL);
    add(m, ACT_NONE, tr("emulators.for_game"), "", tr("emulators.for_game.desc"))->enabled = 0;
    emu_items(m, sys, ACT_SET_GAME_EMU, gi, ov);
    MenuItem *it = add(m, ACT_SET_GAME_EMU, tr("emulators.use_platform_default"), ov ? "" : "✓", tr("emulators.use_platform_default.desc"));
    it->arg = gi;
    it->sarg[0] = '\0';
    add(m, ACT_NONE, tr("emulators.for_platform"), "", tr("emulators.default_for_platform.desc"))->enabled = 0;
    const TmEmulator *pe = tm_catalog_resolve(&A.cat, sys, NULL, tm_ini_get(&A.settings, "emulators", sys->id, NULL), A.paths.cores);
    emu_items(m, sys, ACT_SET_PLAT_EMU, g->system, pe ? pe->id : NULL);
}

static void page_game_options(Menu *m)
{
    long gi = m->ctx;
    tm_strlcpy(m->title, tr("game.options"), sizeof m->title);
    if (gi >= 0 && (size_t)gi < A.lib.count) {
        const TmGame *g = &A.lib.games[gi];
        tm_strlcpy(m->title, g->name, sizeof m->title);
        add(m, ACT_PLAY, tr("game.play"), "A", tr("game.play.desc"))->arg = gi;
        int fav = tm_list_index(&A.fav, g->relpath) >= 0;
        add(m, ACT_FAV, tr(fav ? "game.unfav" : "game.fav"), "X", tr("game.fav.desc"))->arg = gi;
        MenuItem *it = add(m, ACT_PAGE, tr("game.emulator"), "SELECT", tr("emulators.for_game.desc"));
        it->arg = PAGE_EMU_CHOOSE;
        snprintf(it->sarg, sizeof it->sarg, "%ld", gi);
        if (tm_list_index(&A.recent, g->relpath) >= 0)
            add(m, ACT_REMOVE_RECENT, tr("game.remove_recent"), "", tr("game.remove_recent.desc"))->arg = gi;
    }
    add(m, ACT_SEARCH, tr("game.search"), "Y", tr("game.search.desc"));
    add(m, ACT_FAV_ONLY, tr("game.fav_only"), onoff(A.fav_only), tr("game.fav_only.desc"));
}

static int grow_plan(TmFatGrowPlan *plan, char *err, size_t errsz)
{
    char part[256], disk[256];
    if (tm_card_device(A.paths.sd, part, sizeof part, disk, sizeof disk) != 0) {
        tm_strlcpy(err, tr("storage.grow.nodev"), errsz);
        return -1;
    }
    int fd = open(disk, O_RDONLY | O_CLOEXEC);
    uint64_t size;
    int rc = (fd >= 0 && tm_fatgrow_device_size(fd, &size) == 0) ? tm_fatgrow_plan(fd, size, plan, err, errsz) : -1;
    if (fd >= 0)
        close(fd);
    else
        tm_strlcpy(err, tr("storage.grow.nodev"), errsz);
    return rc;
}

static void page_storage(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.storage"), sizeof m->title);
    app_refresh_sysinfo(1);
    char a[32], b[32], v[80];
    tm_format_bytes(A.si.sd_free, a, sizeof a);
    tm_format_bytes(A.si.sd_total, b, sizeof b);
    snprintf(v, sizeof v, tr("storage.free_of"), a, b);
    int low = A.si.sd_total && A.si.sd_free < A.si.sd_total / 50;
    add(m, ACT_NONE, tr("storage.space"), v, tr(low ? "storage.low" : "storage.space.desc"));
    snprintf(v, sizeof v, "%s%s", A.si.sd_fstype[0] ? A.si.sd_fstype : "?", A.si.sd_readonly ? " (RO)" : "");
    add(m, ACT_NONE, tr("storage.fs"), v, tr(A.si.sd_readonly ? "storage.readonly" : "storage.fs.desc"));
    TmFatGrowPlan plan;
    char err[160] = "";
    int rc = grow_plan(&plan, err, sizeof err);
    MenuItem *it = add(m, ACT_GROW, tr("storage.grow"), "", "");
    if (rc == 0) {
        tm_format_bytes((unsigned long long)plan.fs_total * 512, a, sizeof a);
        tm_format_bytes((unsigned long long)plan.new_total * 512, b, sizeof b);
        snprintf(it->desc, sizeof it->desc, tr("storage.grow.can"), a, b);
    } else {
        it->enabled = 0;
        snprintf(it->desc, sizeof it->desc, "%s", rc == 1 ? tr("storage.grow.done") : err);
    }
}

static void page_system(Menu *m)
{
    tm_strlcpy(m->title, tr("settings.system"), sizeof m->title);
    add(m, ACT_INFO, tr("system.info"), "›", tr("system.info.desc"));
    add(m, ACT_WIZARD, tr("system.wizard"), "›", tr("system.wizard.desc"));
    add(m, ACT_STOCK, tr("system.stock"), "", tr("system.stock.desc"));
    add_page(m, PAGE_LOG, tr("system.log"), tr("system.log.desc"));
    add_page(m, PAGE_ABOUT, tr("system.about"), tr("system.about.desc"));
    add(m, ACT_REBOOT, tr("system.reboot"), "", tr("system.reboot.desc"));
    add(m, ACT_POWEROFF, tr("system.poweroff"), "", tr("system.poweroff.desc"));
}

static void page_quick(Menu *m)
{
    tm_strlcpy(m->title, tr("quick.title"), sizeof m->title);
    const char *prof = tm_ini_get(&A.settings, "power", "profile", "auto");
    char desc[320];
    profile_desc(prof, desc, sizeof desc);
    MenuItem *it = add(m, ACT_PROFILE, tr("power.profile"), profile_name(prof), desc);
    it->enabled = A.power.has_cpufreq;
    if (A.leds.available)
        add(m, ACT_LED_MANAGED, tr("leds.managed"), onoff((int)setting_long("leds", "managed", 0)), tr("leds.managed.desc"));
    add(m, ACT_RESCAN, tr("library.rescan"), "", tr("library.rescan.desc"));
    add_page(m, PAGE_SETTINGS, tr("home.settings"), tr("home.settings.desc"));
    add(m, ACT_STOCK, tr("system.stock"), "", tr("system.stock.desc"));
    add(m, ACT_REBOOT, tr("system.reboot"), "", tr("system.reboot.desc"));
    add(m, ACT_POWEROFF, tr("system.poweroff"), "", tr("system.poweroff.desc"));
}

static void page_hotkeys(Menu *m)
{
    tm_strlcpy(m->title, tr("controls.hotkeys"), sizeof m->title);
    static const char *const keys[][2] = {
        {"HOME", "hk.home"},           {"MENU + START", "hk.exit"},      {"MENU + R1", "hk.save"},
        {"MENU + L1", "hk.load"},      {"MENU + R2 / L2", "hk.slot"},    {"MENU + X", "hk.ff"},
        {"L3 + R3", "hk.menu_alt"},    {"F1 / F2", "hk.fkeys"},         {"Chave lateral", "hk.switch"},    {"+ / −", "hk.volume"},           {"MENU + (+ / −)", "hk.brightness"},
        {"POWER", "hk.sleep"},         {"POWER 6 s", "hk.poweroff"},     {"SELECT (boot)", "hk.stock"},
    };
    for (size_t i = 0; i < TM_ARRAY_LEN(keys); i++)
        add(m, ACT_NONE, tr(keys[i][1]), keys[i][0], tr("controls.hotkeys.desc"));
}

static void page_about(Menu *m)
{
    tm_strlcpy(m->title, tr("system.about"), sizeof m->title);
    add(m, ACT_NONE, "TriMux", TRIMUX_VERSION, tr("about.desc"));
    add(m, ACT_NONE, tr("about.license"), "MIT", tr("about.license.desc"));
    add(m, ACT_NONE, "RetroArch", "GPL-3.0", tr("about.retroarch"));
    add(m, ACT_NONE, tr("about.cores"), tr("about.cores.value"), tr("about.cores.desc"));
    add(m, ACT_NONE, tr("about.no_roms"), "", tr("about.no_roms.desc"));
    add(m, ACT_NONE, tr("about.font"), "DejaVu", tr("about.font.desc"));
}

static void page_log(Menu *m)
{
    tm_strlcpy(m->title, tr("system.log"), sizeof m->title);
    char p[TM_PATH_MAX];
    tm_path_join(p, sizeof p, A.paths.logdir, "trimux.log");
    size_t len = 0;
    char *txt = tm_read_file(p, 1u << 20, &len);
    if (!txt) {
        add(m, ACT_NONE, tr("log.empty"), "", "")->enabled = 0;
        return;
    }
    /* last 40 lines, newest first */
    int count = 0;
    for (size_t i = len; i > 0 && count < 40; i--) {
        if (txt[i - 1] == '\n' && i < len) {
            txt[i - 1] = '\0';
            if (txt[i])
                add(m, ACT_NONE, txt + i + (strlen(txt + i) > 20 ? 20 : 0), "", txt + i);
            count++;
        }
    }
    free(txt);
}

/* ------------------------------------------------------------ engine */

void menu_rebuild(void)
{
    Menu *m = cur();
    if (!m)
        return;
    int sel = m->sel, top = m->top;
    m->n = 0;
    m->title[0] = '\0';
    switch (m->page) {
    case PAGE_SETTINGS: page_settings(m); break;
    case PAGE_LANGUAGE: page_language(m); break;
    case PAGE_APPEARANCE: page_appearance(m); break;
    case PAGE_CONTROLS: page_controls(m); break;
    case PAGE_POWER: page_power(m); break;
    case PAGE_DISPLAY: page_display(m); break;
    case PAGE_LEDS: page_leds(m); break;
    case PAGE_LED_ZONE: page_led_zone(m); break;
    case PAGE_LIBRARY: page_library(m); break;
    case PAGE_ADD_GAMES: page_add_games(m); break;
    case PAGE_FOLDERS: page_folders(m); break;
    case PAGE_BIOS: page_bios(m); break;
    case PAGE_EMULATORS: page_emulators(m); break;
    case PAGE_EMU_PLATFORM: page_emu_platform(m); break;
    case PAGE_EMU_CHOOSE: page_emu_choose(m); break;
    case PAGE_GAME_OPTIONS: page_game_options(m); break;
    case PAGE_STORAGE: page_storage(m); break;
    case PAGE_SYSTEM: page_system(m); break;
    case PAGE_QUICK: page_quick(m); break;
    case PAGE_HOTKEYS: page_hotkeys(m); break;
    case PAGE_ABOUT: page_about(m); break;
    case PAGE_LOG: page_log(m); break;
    case PAGE_BUTTONS: page_buttons(m); break;
    }
    m->sel = sel < m->n ? sel : (m->n ? m->n - 1 : 0);
    m->top = top;
    /* never rest on a header row */
    for (int i = 0; i < m->n && !m->items[m->sel].enabled && m->items[m->sel].id == ACT_NONE &&
                    m->page != PAGE_LOG && m->page != PAGE_ADD_GAMES;
         i++)
        m->sel = (m->sel + 1) % m->n;
}

void menu_open(int page, long ctx, const char *sctx)
{
    if (A.nmenus >= MENU_STACK)
        A.nmenus = MENU_STACK - 1;
    Menu *m = &A.menus[A.nmenus++];
    memset(m, 0, sizeof *m);
    m->page = page;
    m->ctx = ctx;
    if (sctx)
        tm_strlcpy(m->sctx, sctx, sizeof m->sctx);
    A.screen = SCR_MENU;
    menu_rebuild();
}

void menu_close(void)
{
    if (A.nmenus > 0)
        A.nmenus--;
    if (A.nmenus == 0) {
        app_save_all();
        A.screen = A.menu_return;
        if (A.screen == SCR_GAMES)
            games_rebuild();
        else
            home_build();
    } else {
        menu_rebuild();
    }
}

void menu_draw(void)
{
    Menu *m = cur();
    if (!m)
        return;
    const TmTheme *t = gfx_theme();
    app_header(m->title);
    int top = S(64) + S(12), bottom = gfx_h() - S(52) - S(10);
    int row = S(58);
    int listw = gfx_w() * 58 / 100;
    int visible = (bottom - top) / row;
    if (m->sel < m->top)
        m->top = m->sel;
    if (m->sel >= m->top + visible)
        m->top = m->sel - visible + 1;
    for (int i = 0; i < visible && m->top + i < m->n; i++) {
        MenuItem *it = &m->items[m->top + i];
        int header = it->id == ACT_NONE && !it->enabled && m->page != PAGE_LOG && m->page != PAGE_ADD_GAMES;
        if (header) {
            gfx_text(FONT_S, S(30), top + i * row + row - gfx_font_height(FONT_S) - S(6), t->accent, ALIGN_LEFT,
                     listw - S(40), it->label);
            continue;
        }
        app_draw_list_row(S(14), top + i * row, listw - S(22), row, m->top + i == m->sel, it->label, it->value,
                          it->enabled, it->badge_color, it->badge[0] ? it->badge : NULL, 0);
    }
    int px = listw + S(4), pw = gfx_w() - listw - S(20);
    gfx_round_rect(px, top, pw, bottom - top, S(14), t->panel);
    if (m->n) {
        MenuItem *it = &m->items[m->sel];
        int y = top + S(20);
        y += gfx_text_wrap(FONT_M, px + S(22), y, pw - S(44), 3, t->text, it->label) + S(12);
        y += gfx_text_wrap(FONT_S, px + S(22), y, pw - S(44), 12, t->dim, it->desc) + S(16);
        if (m->page == PAGE_POWER || (m->page == PAGE_QUICK && it->id == ACT_PROFILE)) {
            long curf = -1, mn = -1, mx = -1;
            char gov[32] = "?", line[96];
            if (A.power.policy_dir[0])
                tm_power_read(&A.power, &curf, &mn, &mx, gov, sizeof gov);
            long temp = tm_power_temp_mc(&A.power);
            if (temp >= 0) {
                snprintf(line, sizeof line, tr("power.now_temp"), temp / 1000.0);
                localize_decimal(line);
            } else
                snprintf(line, sizeof line, "%s", tr("power.now_temp_na"));
            gfx_text(FONT_S, px + S(22), y, temp >= 70000 ? t->warn : t->text, ALIGN_LEFT, pw - S(44), line);
            y += S(34);
            if (curf > 0) {
                snprintf(line, sizeof line, tr("power.now_freq"), curf / 1000, mx / 1000, gov);
                gfx_text(FONT_S, px + S(22), y, t->text, ALIGN_LEFT, pw - S(44), line);
            }
        }
    }
    int has_change = m->n && (m->items[m->sel].id == ACT_PROFILE || m->items[m->sel].id == ACT_THEME ||
                              m->items[m->sel].id == ACT_LED_COLOR || m->items[m->sel].id == ACT_LED_BRIGHT ||
                              m->items[m->sel].id == ACT_IDLE || m->items[m->sel].id == ACT_KEY_ACTION ||
                              m->items[m->sel].id == ACT_SWITCH_ACTION);
    app_footer(tr(has_change ? "menu.hints_change" : "menu.hints"));
}

static void toggle_key(const char *sk)
{
    char sec[64], *key;
    tm_strlcpy(sec, sk, sizeof sec);
    key = strchr(sec, '/');
    if (!key)
        return;
    *key++ = '\0';
    tm_ini_set_long(&A.settings, sec, key, !tm_ini_get_long(&A.settings, sec, key, 0));
    app_mark_settings();
    if (strcmp(key, "clean_names") == 0)
        app_rescan();
}

static void cycle_profile(int dir) { app_cycle_profile(dir); }

static void restore_emulator(const char *emu_id)
{
    const TmEmulator *em = tm_catalog_emulator(&A.cat, emu_id);
    if (!em)
        return;
    /* Archive (never delete) the per-core overrides, options and remaps. */
    const char *subs[] = {"config", "remaps"};
    int moved = 0;
    char stamp[32];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tmv);
    for (size_t i = 0; i < TM_ARRAY_LEN(subs); i++) {
        char rel[256], src[TM_PATH_MAX], dst[TM_PATH_MAX];
        snprintf(rel, sizeof rel, "%s/%s", subs[i], em->config_name);
        if (tm_path_join(src, sizeof src, A.paths.ra_home, rel) != 0 || !tm_dir_exists(src))
            continue;
        if (tm_snprintf(dst, sizeof dst, "%s.bak-%s", src, stamp) == 0 && rename(src, dst) == 0) {
            moved++;
            LOGI("ui: restored defaults for %s (archived %s)", em->id, dst);
        }
    }
    app_toast(tr(moved ? "emulators.restored" : "emulators.already_default"));
}

static void activate(MenuItem *it, TmButton b)
{
    int dir = b == BTN_LEFT ? -1 : 1;
    char msg[512];
    switch (it->id) {
    case ACT_PAGE:
        if (b == BTN_A) {
            if (it->arg == PAGE_EMU_CHOOSE)
                menu_open((int)it->arg, strtol(it->sarg, NULL, 10), NULL);
            else
                menu_open((int)it->arg, 0, it->sarg);
        }
        return;
    case ACT_LANG:
        tm_ini_set(&A.settings, "general", "language", it->sarg);
        app_mark_settings();
        app_apply_language();
        break;
    case ACT_THEME: {
        static const char *const th[] = {"dark", "light", "contrast"};
        const char *c = tm_ini_get(&A.settings, "general", "theme", "dark");
        int i = 0;
        for (int k = 0; k < 3; k++)
            if (strcmp(c, th[k]) == 0)
                i = k;
        i = (i + dir + 3) % 3;
        tm_ini_set(&A.settings, "general", "theme", th[i]);
        gfx_set_theme(th[i]);
        app_mark_settings();
        break;
    }
    case ACT_TOGGLE: toggle_key(it->sarg); break;
    case ACT_IDLE: {
        static const long opts[] = {0, 5, 10, 15, 30};
        long c = setting_long("general", "idle_poweroff_min", 0);
        int i = 0;
        for (int k = 0; k < 5; k++)
            if (opts[k] == c)
                i = k;
        i = (i + dir + 5) % 5;
        tm_ini_set_long(&A.settings, "general", "idle_poweroff_min", opts[i]);
        app_mark_settings();
        break;
    }
    case ACT_SWAP_AB:
        tm_ini_set_long(&A.settings, "input", "swap_ab", !setting_long("input", "swap_ab", 0));
        app_mark_settings();
        input_reload(&A.settings);
        app_toast(tr("controls.swapped"));
        break;
    case ACT_CTRLTEST:
        if (b == BTN_A) {
            A.ctrl_start_held = 0;
            A.screen = SCR_CTRLTEST;
        }
        return;
    case ACT_PROFILE: cycle_profile(b == BTN_A ? 1 : dir); break;
    case ACT_POWER_DEFAULT:
        app_dialog(DLG_POWER_DEFAULT, tr("power.restore"), tr("power.restore.confirm"), 0, NULL, 0);
        return;
    case ACT_LED_MANAGED: {
        int v = !setting_long("leds", "managed", 0);
        tm_ini_set_long(&A.settings, "leds", "managed", v);
        app_mark_settings();
        for (size_t i = 0; v && i < A.leds.nzones; i++) {
            TmLedSetting s;
            led_setting(A.leds.zones[i].id, &s);
            tm_leds_apply(&A.leds, A.leds.zones[i].id, &s);
        }
        if (!v)
            app_toast(tr("leds.firmware_default"));
        break;
    }
    case ACT_LED_ON:
    case ACT_LED_COLOR:
    case ACT_LED_BRIGHT:
    case ACT_LED_EFFECT: {
        TmLedSetting s;
        led_setting(it->sarg, &s);
        if (it->id == ACT_LED_ON)
            s.on = !s.on;
        else if (it->id == ACT_LED_COLOR) {
            int i = 0, n = (int)TM_ARRAY_LEN(k_colors);
            for (int k = 0; k < n; k++)
                if (k_colors[k].rgb == (s.color & 0xFFFFFF))
                    i = k;
            s.color = k_colors[(i + dir + n) % n].rgb;
        } else if (it->id == ACT_LED_BRIGHT) {
            s.brightness += dir * 10;
            s.brightness = s.brightness < 0 ? 0 : s.brightness > 100 ? 100 : s.brightness;
        } else {
            s.effect = s.effect == TM_LED_EFFECT_STATIC ? TM_LED_EFFECT_BREATHE : TM_LED_EFFECT_STATIC;
        }
        led_store(it->sarg, &s);
        break;
    }
    case ACT_RESCAN: if (b == BTN_A) app_rescan(); break;
    case ACT_MKDIRS:
        if (b == BTN_A)
            app_dialog(DLG_MKDIRS, tr("library.mkdirs"), tr("library.mkdirs.confirm"), 0, NULL, 0);
        return;
    case ACT_SET_PLAT_EMU:
        if (b != BTN_A)
            return;
        if (it->arg >= 0 && (size_t)it->arg < A.cat.nsystems) {
            tm_ini_set(&A.settings, "emulators", A.cat.systems[it->arg].id, it->sarg);
            app_mark_settings();
            snprintf(msg, sizeof msg, tr("emulators.set_platform"), A.cat.systems[it->arg].name);
            app_toast(msg);
        }
        break;
    case ACT_SET_GAME_EMU:
        if (b != BTN_A || it->arg < 0 || (size_t)it->arg >= A.lib.count)
            return;
        if (it->sarg[0])
            tm_ini_set(&A.overrides, "games", A.lib.games[it->arg].relpath, it->sarg);
        else
            tm_ini_remove(&A.overrides, "games", A.lib.games[it->arg].relpath);
        A.overrides_dirty = 1;
        app_toast(tr(it->sarg[0] ? "emulators.set_game" : "emulators.cleared_game"));
        break;
    case ACT_RESTORE_EMU:
        if (b == BTN_A) {
            const TmEmulator *em = tm_catalog_emulator(&A.cat, it->sarg);
            snprintf(msg, sizeof msg, tr("emulators.restore.confirm"), em ? em->name : it->sarg);
            app_dialog(DLG_RESTORE_EMU, tr("emulators.restore_header"), msg, 0, it->sarg, 0);
        }
        return;
    case ACT_INFO: if (b == BTN_A) A.screen = SCR_INFO; return;
    case ACT_WIZARD: if (b == BTN_A) { A.nmenus = 0; wizard_open(); } return;
    case ACT_STOCK: if (b == BTN_A) app_dialog(DLG_STOCK, tr("system.stock"), tr("system.stock.confirm"), 0, NULL, 0); return;
    case ACT_REBOOT: if (b == BTN_A) app_dialog(DLG_REBOOT, tr("system.reboot"), tr("system.reboot.confirm"), 0, NULL, 0); return;
    case ACT_POWEROFF: if (b == BTN_A) app_dialog(DLG_POWEROFF, tr("system.poweroff"), tr("system.poweroff.confirm"), 0, NULL, 0); return;
    case ACT_GROW: if (b == BTN_A) app_dialog(DLG_GROW, tr("storage.grow"), tr("storage.grow.confirm"), 0, NULL, 0); return;
    case ACT_PLAY: if (b == BTN_A) { A.nmenus = 0; A.screen = A.menu_return; app_launch(it->arg); } return;
    case ACT_FAV:
        if (b == BTN_A && it->arg >= 0) {
            tm_list_toggle(&A.fav, A.lib.games[it->arg].relpath);
            tm_list_save(&A.fav, A.paths.favorites);
        }
        break;
    case ACT_REMOVE_RECENT:
        if (b == BTN_A && it->arg >= 0) {
            tm_list_remove(&A.recent, A.lib.games[it->arg].relpath);
            tm_list_save(&A.recent, A.paths.recents);
            app_toast(tr("game.removed_recent"));
        }
        break;
    case ACT_FAV_ONLY: A.fav_only = !A.fav_only; break;
    case ACT_KEY_ACTION: {
        TmKeyAction a = tm_key_action_parse(tm_ini_get(&A.settings, "buttons", it->sarg, NULL),
                                            strcmp(it->sarg, "f1") == 0 ? TM_KEY_FAVORITE : TM_KEY_RANDOM);
        a = (TmKeyAction)(((int)a + (b == BTN_LEFT ? -1 : 1) + TM_KEY_COUNT) % TM_KEY_COUNT);
        tm_ini_set(&A.settings, "buttons", it->sarg, tm_key_action_id(a));
        app_mark_settings();
        break;
    }
    case ACT_SWITCH_ACTION: {
        TmSwitchAction a = tm_switch_action(&A.settings);
        do /* skip "mute" when the firmware has no speaker mute file */
            a = (TmSwitchAction)(((int)a + (b == BTN_LEFT ? -1 : 1) + TM_SWITCH_COUNT) % TM_SWITCH_COUNT);
        while ((a == TM_SWITCH_MUTE && !tm_speaker_mute_available()) || (a == TM_SWITCH_LEDS_OFF && !A.leds.available) ||
               (a == TM_SWITCH_BOOST && !tm_power_boost_available(&A.power)));
        if (a == TM_SWITCH_BOOST && !setting_long("power", "boost_ack", 0)) {
            /* 2.0 GHz is above the manufacturer's rating: explicit consent first */
            app_dialog(DLG_BOOST, tr("boost.confirm.title"), tr("boost.confirm.text"), 0, NULL, 0);
            return;
        }
        tm_ini_set(&A.settings, "buttons", "switch", tm_switch_action_id(a));
        app_mark_settings();
        break;
    }
    case ACT_SEARCH:
        if (b == BTN_A) {
            A.nmenus = 0;
            keyboard_open(A.query, SCR_GAMES);
        }
        return;
    default: return;
    }
    menu_rebuild();
}

void menu_input(TmButton b)
{
    Menu *m = cur();
    if (!m)
        return;
    int n = m->n;
    switch (b) {
    case BTN_UP:
    case BTN_DOWN: {
        int d = b == BTN_UP ? -1 : 1;
        for (int k = 0; k < n; k++) { /* skip header rows */
            m->sel = (m->sel + d + n) % n;
            MenuItem *it = &m->items[m->sel];
            if (it->id != ACT_NONE || it->enabled || m->page == PAGE_LOG || m->page == PAGE_ADD_GAMES)
                break;
        }
        break;
    }
    case BTN_L1: m->sel = m->sel > 5 ? m->sel - 6 : 0; break;
    case BTN_R1: m->sel = m->sel + 6 < n ? m->sel + 6 : n - 1; break;
    case BTN_A:
    case BTN_LEFT:
    case BTN_RIGHT:
        if (n && m->items[m->sel].enabled)
            activate(&m->items[m->sel], b);
        break;
    case BTN_B: menu_close(); break;
    case BTN_MENU:
    case BTN_HOME:
        A.nmenus = 1;
        menu_close();
        break;
    default: break;
    }
}

void menu_dialog_result(int id, long arg, const char *sarg, int yes)
{
    if (!yes)
        return;
    switch (id) {
    case DLG_STOCK: app_exit(EXIT_STOCK); break;
    case DLG_POWEROFF: app_exit(EXIT_POWEROFF); break;
    case DLG_REBOOT: app_exit(EXIT_REBOOT); break;
    case DLG_GROW: app_exit(EXIT_CARD_GROW); break;
    case DLG_MKDIRS: {
        int n = tm_library_create_default_dirs(&A.cat, A.paths.sd);
        char msg[96];
        snprintf(msg, sizeof msg, tr("library.mkdirs.done"), n);
        app_toast(msg);
        menu_rebuild();
        break;
    }
    case DLG_RESTORE_EMU: restore_emulator(sarg); break;
    case DLG_BOOST:
        tm_ini_set_long(&A.settings, "power", "boost_ack", 1);
        tm_ini_set(&A.settings, "buttons", "switch", "boost");
        app_mark_settings();
        LOGW("ui: user enabled the 2.0 GHz side-switch boost");
        menu_rebuild();
        break;
    case DLG_POWER_DEFAULT:
        tm_ini_set(&A.settings, "power", "profile", "auto");
        tm_ini_set_long(&A.settings, "general", "idle_poweroff_min", 0);
        tm_ini_set_long(&A.settings, "power", "boost_ack", 0);
        if (strcmp(tm_ini_get(&A.settings, "buttons", "switch", "none"), "boost") == 0)
            tm_ini_set(&A.settings, "buttons", "switch", "none");
        app_mark_settings();
        if (A.power.has_cpufreq)
            tm_power_apply(&A.power, tm_power_profile(TM_POWER_DEFAULT));
        app_toast(tr("power.restored"));
        menu_rebuild();
        break;
    case DLG_WIZ_SKIP:
        tm_ini_set_long(&A.settings, "general", "wizard_done", 1);
        app_mark_settings();
        app_save_all();
        A.screen = SCR_HOME;
        home_build();
        break;
    default: break;
    }
}

/* ------------------------------------------------------------ info screen */

void info_draw(void)
{
    const TmTheme *t = gfx_theme();
    app_header(tr("system.info"));
    app_refresh_sysinfo(1);
    char rows[16][2][96];
    int n = 0;
#define ROW(k, ...)                                                                                     \
    do {                                                                                                \
        tm_strlcpy(rows[n][0], tr(k), 96);                                                              \
        snprintf(rows[n][1], 96, __VA_ARGS__);                                                          \
        n++;                                                                                            \
    } while (0)
    ROW("info.model", "%s", A.si.model);
    ROW("info.firmware", "%s", A.si.firmware);
    ROW("info.trimux", "%s", TRIMUX_VERSION);
    if (A.si.battery_pct >= 0)
        ROW("info.battery", "%d%%%s%s", A.si.battery_pct, A.si.charging == 1 ? " " : "",
            A.si.charging == 1 ? tr("info.charging") : "");
    else
        ROW("info.battery", "%s", tr("common.na"));
    long temp = tm_power_temp_mc(&A.power);
    if (temp >= 0) {
        ROW("info.temp", "%.1f °C", temp / 1000.0);
        localize_decimal(rows[n - 1][1]);
    }
    else
        ROW("info.temp", "%s", tr("common.na"));
    long cf = -1, mn = -1, mx = -1;
    char gov[32] = "?";
    if (A.power.policy_dir[0] && tm_power_read(&A.power, &cf, &mn, &mx, gov, sizeof gov) == 0 && cf > 0)
        ROW("info.cpu", "%ld MHz (%ld–%ld, %s)", cf / 1000, mn / 1000, mx / 1000, gov);
    else
        ROW("info.cpu", "%s", tr("common.na"));
    ROW("info.ram", "%ld / %ld MB", A.si.mem_avail_kb / 1024, A.si.mem_total_kb / 1024);
    ROW("info.swap", "%s", A.si.swap_total_kb > 0 ? tr("common.on") : tr("info.swap_off"));
    char a[32], b[32];
    tm_format_bytes(A.si.sd_free, a, sizeof a);
    tm_format_bytes(A.si.sd_total, b, sizeof b);
    ROW("info.card", "%s / %s · %s%s", a, b, A.si.sd_fstype[0] ? A.si.sd_fstype : "?", A.si.sd_readonly ? " RO" : "");
    ROW("info.games", "%zu", A.lib.count);
    ROW("info.pad", "%s", input_state()->pad_name[0] ? input_state()->pad_name : tr("info.no_pad"));
    ROW("info.leds", "%s", tr(A.leds.available ? "common.yes" : "common.no"));
    int y = S(64) + S(24), row = S(44);
    for (int i = 0; i < n; i++) {
        gfx_text(FONT_M, S(40), y + i * row, t->dim, ALIGN_LEFT, gfx_w() / 2 - S(60), rows[i][0]);
        gfx_text(FONT_M, gfx_w() / 2 - S(20), y + i * row, t->text, ALIGN_LEFT, gfx_w() / 2 - S(20), rows[i][1]);
    }
    app_footer(tr("info.hints"));
}

void info_input(TmButton b)
{
    if (b == BTN_B || b == BTN_A)
        A.screen = SCR_MENU;
}

/* ------------------------------------------------------------ controller test */

void ctrltest_draw(void)
{
    const TmTheme *t = gfx_theme();
    const TmInputState *st = input_state();
    app_header(tr("controls.test"));
    int x0 = S(40), y = S(64) + S(20);
    char line[128];
    snprintf(line, sizeof line, "%s: %s", tr("info.pad"), st->pad_name[0] ? st->pad_name : tr("info.no_pad"));
    gfx_text(FONT_M, x0, y, t->text, ALIGN_LEFT, gfx_w() - S(80), line);
    y += S(52);
    int bw = S(112), bh = S(52), cols = 8;
    for (int b = 0; b < BTN_COUNT; b++) {
        int cx = x0 + (b % cols) * (bw + S(10)), cy = y + (b / cols) * (bh + S(10));
        gfx_round_rect(cx, cy, bw, bh, S(10), st->pressed[b] ? t->sel : t->panel2);
        gfx_text(FONT_S, cx + bw / 2, cy + (bh - gfx_font_height(FONT_S)) / 2, st->pressed[b] ? t->accent_text : t->text,
                 ALIGN_CENTER, bw - S(8), input_button_name((TmButton)b));
    }
    y += 3 * (bh + S(10)) + S(16);
    gfx_text(FONT_S, x0, y, t->accent, ALIGN_LEFT, 0, tr("controls.raw"));
    y += S(36);
    char raw[256] = "";
    for (int i = 0; i < 32; i++)
        if (st->raw_buttons[i]) {
            size_t l = strlen(raw);
            snprintf(raw + l, sizeof raw - l, "%d ", i);
        }
    snprintf(line, sizeof line, "%s: %s", tr("controls.raw_buttons"), raw[0] ? raw : "-");
    gfx_text(FONT_S, x0, y, t->text, ALIGN_LEFT, gfx_w() - S(80), line);
    y += S(34);
    snprintf(line, sizeof line, "%s: %d   %s: %d %d %d %d %d %d", tr("controls.raw_hat"), st->raw_hat, tr("controls.raw_axes"),
             st->raw_axes[0], st->raw_axes[1], st->raw_axes[2], st->raw_axes[3], st->raw_axes[4], st->raw_axes[5]);
    gfx_text(FONT_S, x0, y, t->text, ALIGN_LEFT, gfx_w() - S(80), line);
    y += S(50);
    gfx_text_wrap(FONT_S, x0, y, gfx_w() - S(80), 4, t->dim, tr("controls.test.help"));
    app_footer(tr("controls.test.hints"));
    A.dirty = 1; /* live view while open */
}

void ctrltest_input(TmButton b)
{
    /* Every button is under test, so leaving needs START held for 2 s. */
    (void)b;
    if (input_state()->pressed[BTN_START]) {
        if (!A.ctrl_start_held)
            A.ctrl_start_held = tm_now_ms();
    } else {
        A.ctrl_start_held = 0;
    }
}

void ctrltest_tick(void)
{
    if (A.screen != SCR_CTRLTEST)
        return;
    if (!input_state()->pressed[BTN_START])
        A.ctrl_start_held = 0;
    else if (A.ctrl_start_held && tm_now_ms() - A.ctrl_start_held > 2000) {
        A.ctrl_start_held = 0;
        A.screen = SCR_MENU;
    }
}
