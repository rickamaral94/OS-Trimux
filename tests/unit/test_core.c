/* Unit tests for the portable core. Run natively: make test-unit */
#define _GNU_SOURCE
#include "../../src/core/catalog.h"
#include "../../src/core/i18n.h"
#include "../../src/core/ini.h"
#include "../../src/core/launch.h"
#include "../../src/core/leds.h"
#include "../../src/core/library.h"
#include "../../src/core/lists.h"
#include "../../src/core/log.h"
#include "../../src/core/paths.h"
#include "../../src/core/power.h"
#include "../../src/core/sysinfo.h"
#include "../../src/core/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail, g_run;
#define CHECK(c)                                                                                        \
    do {                                                                                                \
        g_run++;                                                                                        \
        if (!(c)) {                                                                                     \
            g_fail++;                                                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                \
        }                                                                                               \
    } while (0)
#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)

static char T[512]; /* temp root */

static void put(const char *rel, const char *content)
{
    char p[1024], d[1024];
    snprintf(p, sizeof p, "%s/%s", T, rel);
    tm_strlcpy(d, p, sizeof d);
    char *s = strrchr(d, '/');
    *s = '\0';
    tm_mkdir_p(d);
    FILE *f = fopen(p, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static void test_util(void)
{
    char b[8];
    CHECK(tm_strlcpy(b, "abc", sizeof b) == 0);
    CHECK(tm_strlcpy(b, "abcdefghij", sizeof b) == -1 && strlen(b) == 7);
    CHECK(tm_path_is_safe_under("/mnt/SDCARD", "/mnt/SDCARD/Roms/GBA/a.gba"));
    CHECK(!tm_path_is_safe_under("/mnt/SDCARD", "/mnt/SDCARD/../etc/passwd"));
    CHECK(!tm_path_is_safe_under("/mnt/SDCARD", "/mnt/SDCARDX/a"));
    CHECK(!tm_path_is_safe_under("/mnt/SDCARD", "/etc/passwd"));
    CHECK(!tm_path_is_safe_under("/mnt/SDCARD", "/mnt/SDCARD/a\nb"));
    CHECK(tm_path_is_safe_under("/mnt/SDCARD", "/mnt/SDCARD/Roms/a..b.gba"));
    CHECK(!tm_name_is_safe("..") && !tm_name_is_safe("a/b") && tm_name_is_safe("GBA"));
    char k[64];
    tm_fold_key("Pokémon Édição AÇÃO", k, sizeof k);
    CHECK_STR(k, "pokemon edicao acao");
    char path[1024];
    snprintf(path, sizeof path, "%s/atomic.txt", T);
    CHECK(tm_atomic_write(path, "one", 3) == 0);
    CHECK(tm_atomic_write(path, "two!", 4) == 0);
    char *data = tm_read_file(path, 100, NULL);
    CHECK(data && strcmp(data, "two!") == 0);
    free(data);
    snprintf(path, sizeof path, "%s/atomic.txt.tmp", T);
    CHECK(!tm_file_exists(path));
}

static void test_ini(void)
{
    TmIni ini;
    tm_ini_init(&ini);
    const char *txt = "\xEF\xBB\xBFtop = 1\n[a]\nx = \"quoted\"\nbroken line\n; comment\n[b]\ny=2\n[a]\nz = 3\n";
    int skipped = tm_ini_parse(&ini, txt, strlen(txt));
    CHECK(skipped == 1);
    CHECK_STR(tm_ini_get(&ini, "", "top", "?"), "1");
    CHECK_STR(tm_ini_get(&ini, "a", "x", "?"), "quoted");
    CHECK(tm_ini_get_long(&ini, "b", "y", 0) == 2);
    CHECK(tm_ini_get_long(&ini, "a", "z", 0) == 3);
    CHECK(tm_ini_get_long(&ini, "a", "x", 7) == 7); /* not a number */
    CHECK(tm_ini_set(&ini, "a", "bad=key", "v") != 0);
    CHECK(tm_ini_set(&ini, "a", "k", "line\nbreak") != 0);
    tm_ini_set(&ini, "a", "new", "n");
    char p[1024];
    snprintf(p, sizeof p, "%s/t.ini", T);
    CHECK(tm_ini_save(&ini, p) == 0);
    TmIni b;
    tm_ini_init(&b);
    CHECK(tm_ini_load(&b, p) == 0);
    CHECK_STR(tm_ini_get(&b, "a", "new", "?"), "n");
    CHECK_STR(tm_ini_get(&b, "a", "z", "?"), "3");
    CHECK_STR(tm_ini_get(&b, "", "top", "?"), "1");
    tm_ini_free(&b);
    tm_ini_free(&ini);
    TmIni missing;
    tm_ini_init(&missing);
    snprintf(p, sizeof p, "%s/none.ini", T);
    CHECK(tm_ini_load(&missing, p) == 0 && missing.count == 0);
    tm_ini_free(&missing);
}

static void test_i18n(void)
{
    put("i18n/pt_BR.lang", "lang.name = Português (Brasil)\nhello = Olá\nonly.pt = só pt\n");
    put("i18n/en_US.lang", "lang.name = English\nhello = Hello\n");
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/i18n", T);
    CHECK(tm_i18n_load(dir, "en_US") == 0);
    CHECK_STR(tm_tr("hello"), "Hello");
    CHECK_STR(tm_tr("only.pt"), "só pt"); /* falls back to pt_BR */
    CHECK_STR(tm_tr("missing.key"), "missing.key");
    tm_i18n_load(dir, "../../etc");
    CHECK_STR(tm_i18n_current(), "pt_BR");
    char codes[8][16];
    size_t n = tm_i18n_available(dir, codes, 8);
    CHECK(n == 2 && strcmp(codes[0], "pt_BR") == 0);
    tm_i18n_free();
}

static const char *SYSTEMS =
    "[GBA]\nname = Game Boy Advance\nfolders = GBA, ../evil\nextensions = gba, zip\n"
    "emulators = gpsp, mgba\nbios = gba_bios.bin\n"
    "[PS]\nname = PlayStation\nfolders = PS, PSX\nextensions = cue, chd, m3u, pbp\nemulators = pcsx\n"
    "[EMPTY]\nname = No exts\nfolders = X\n";
static const char *EMUS =
    "[gpsp]\nname = gpSP\ncore = gpsp_libretro.so\n"
    "[mgba]\nname = mGBA\ncore = mgba_libretro.so\nconfig_name = mGBA\n"
    "[pcsx]\nname = PCSX ReARMed\ncore = pcsx_rearmed_libretro.so\n"
    "[evil]\nname = Evil\ncore = ../../bin/sh\n";

static void load_cat(TmCatalog *cat)
{
    put("share/systems.ini", SYSTEMS);
    put("share/emulators.ini", EMUS);
    char s[1024], e[1024];
    snprintf(s, sizeof s, "%s/share/systems.ini", T);
    snprintf(e, sizeof e, "%s/share/emulators.ini", T);
    CHECK(tm_catalog_load(cat, s, e) == 0);
}

static void test_catalog(void)
{
    TmCatalog cat;
    load_cat(&cat);
    CHECK(cat.nsystems == 2); /* EMPTY dropped */
    CHECK(cat.nemus == 3);    /* evil core path rejected */
    const TmSystem *gba = tm_catalog_system(&cat, "GBA");
    CHECK(gba && gba->nfolders == 1); /* ../evil dropped */
    CHECK(tm_system_has_ext(gba, "x.GBA") && !tm_system_has_ext(gba, "x.gb") && !tm_system_has_ext(gba, "gba"));
    char cores[1024];
    snprintf(cores, sizeof cores, "%s/cores", T);
    tm_mkdir_p(cores);
    CHECK(tm_catalog_resolve(&cat, gba, NULL, NULL, cores) == NULL);
    put("cores/mgba_libretro.so", "x");
    const TmEmulator *em = tm_catalog_resolve(&cat, gba, NULL, NULL, cores);
    CHECK(em && strcmp(em->id, "mgba") == 0); /* gpsp listed first but missing */
    put("cores/gpsp_libretro.so", "x");
    em = tm_catalog_resolve(&cat, gba, NULL, NULL, cores);
    CHECK(em && strcmp(em->id, "gpsp") == 0);
    em = tm_catalog_resolve(&cat, gba, NULL, "mgba", cores);
    CHECK(em && strcmp(em->id, "mgba") == 0);
    em = tm_catalog_resolve(&cat, gba, "gpsp", "mgba", cores);
    CHECK(em && strcmp(em->id, "gpsp") == 0);
    em = tm_catalog_resolve(&cat, gba, "pcsx", NULL, cores); /* not a GBA emulator */
    CHECK(em && strcmp(em->id, "gpsp") == 0);
    tm_catalog_free(&cat);
}

static void test_library(void)
{
    TmCatalog cat;
    load_cat(&cat);
    char sd[1024];
    snprintf(sd, sizeof sd, "%s/sd", T);
    tm_mkdir_p(sd);
    put("sd/Roms/GBA/Metroid Fusion (USA).gba", "");
    put("sd/Roms/GBA/sub/Golden Sun (Europe) [!].zip", "");
    put("sd/Roms/GBA/._Metroid Fusion (USA).gba", "");
    put("sd/Roms/GBA/readme.txt", "");
    put("sd/Roms/GBA/Imgs/cover.gba", "");
    put("sd/Roms/PSX/FF7 (Disc 1).cue", "");
    put("sd/Roms/PSX/FF7 (Disc 2).cue", "");
    put("sd/Roms/PSX/FF7.m3u", "FF7 (Disc 1).cue\nFF7 (Disc 2).cue\n");
    put("sd/Roms/PSX/Crash (USA) (Disc 1).chd", "");
    put("sd/Roms/Game Boy Advance (GBA)/Tagged.gba", "");
    TmLibrary lib;
    tm_library_init(&lib);
    tm_library_scan(&lib, &cat, sd, 1);
    CHECK(lib.count == 5);
    CHECK(tm_library_find(&lib, "Roms/Game Boy Advance (GBA)/Tagged.gba") >= 0);
    CHECK(tm_library_find(&lib, "Roms/GBA/Metroid Fusion (USA).gba") >= 0);
    CHECK(tm_library_find(&lib, "Roms/GBA/sub/Golden Sun (Europe) [!].zip") >= 0);
    CHECK(tm_library_find(&lib, "Roms/PSX/FF7.m3u") >= 0);
    CHECK(tm_library_find(&lib, "Roms/PSX/FF7 (Disc 1).cue") < 0);
    long i = tm_library_find(&lib, "Roms/GBA/Metroid Fusion (USA).gba");
    CHECK(i >= 0 && strcmp(lib.games[i].name, "Metroid Fusion") == 0);
    i = tm_library_find(&lib, "Roms/PSX/Crash (USA) (Disc 1).chd");
    CHECK(i >= 0 && strcmp(lib.games[i].name, "Crash (Disc 1)") == 0);
    CHECK(strcmp(lib.games[0].key, lib.games[1].key) <= 0);
    char idx[1024];
    snprintf(idx, sizeof idx, "%s/library.tsv", T);
    CHECK(tm_library_save(&lib, &cat, idx) == 0);
    TmLibrary b;
    tm_library_init(&b);
    CHECK(tm_library_load(&b, &cat, idx) == 0);
    CHECK(b.count == lib.count && b.ndirs == lib.ndirs);
    CHECK(!tm_library_is_stale(&b, &cat, sd));
    put("sd/Roms/PS/new.cue", ""); /* new known folder appears */
    CHECK(tm_library_is_stale(&b, &cat, sd));
    char tn[64];
    tm_clean_name("Roms/X/(Beta).gba", 1, tn, sizeof tn);
    CHECK_STR(tn, "(Beta)");
    tm_clean_name("Super Game (USA, Europe) (Rev 1).sfc", 0, tn, sizeof tn);
    CHECK_STR(tn, "Super Game (USA, Europe) (Rev 1)");
    char q[32];
    tm_fold_key("METRO", q, sizeof q);
    i = tm_library_find(&lib, "Roms/GBA/Metroid Fusion (USA).gba");
    CHECK(tm_game_matches(&lib.games[i], q));
    put("bad.tsv", "garbage\n");
    snprintf(idx, sizeof idx, "%s/bad.tsv", T);
    CHECK(tm_library_load(&b, &cat, idx) == -1);
    tm_library_free(&b);
    tm_library_free(&lib);
    char sd2[1024];
    snprintf(sd2, sizeof sd2, "%s/sd2", T);
    tm_mkdir_p(sd2);
    CHECK(tm_library_create_default_dirs(&cat, sd2) == 2);
    CHECK(tm_library_create_default_dirs(&cat, sd2) == 0);
    tm_catalog_free(&cat);
}

static void test_lists(void)
{
    TmList l;
    tm_list_init(&l, 3);
    tm_list_push_front(&l, "a");
    tm_list_push_front(&l, "b");
    tm_list_push_front(&l, "c");
    tm_list_push_front(&l, "a");
    CHECK(l.count == 3 && strcmp(l.items[0], "a") == 0 && strcmp(l.items[2], "b") == 0);
    tm_list_push_front(&l, "d");
    CHECK(l.count == 3 && tm_list_index(&l, "b") < 0);
    CHECK(tm_list_push_front(&l, "bad\nitem") == -1);
    char p[1024];
    snprintf(p, sizeof p, "%s/list.txt", T);
    CHECK(tm_list_save(&l, p) == 0);
    TmList b;
    tm_list_init(&b, 10);
    tm_list_load(&b, p);
    CHECK(b.count == 3 && strcmp(b.items[0], "d") == 0);
    CHECK(tm_list_toggle(&b, "x") == 1 && tm_list_toggle(&b, "x") == 0);
    tm_list_free(&b);
    tm_list_free(&l);
}

static void sysfs_put(const char *rel, const char *v)
{
    char r[600];
    snprintf(r, sizeof r, "sysfs%s", rel);
    put(r, v);
}

static long sysfs_long(const char *rel)
{
    char p[1024];
    long v = -1;
    snprintf(p, sizeof p, "%s/sysfs%s", T, rel);
    tm_read_long(p, &v);
    return v;
}

#define POL "/sys/devices/system/cpu/cpufreq/policy0"

static void test_power(void)
{
    char root[1024];
    snprintf(root, sizeof root, "%s/sysfs", T);
    setenv("TRIMUX_SYSFS_ROOT", root, 1);
    TmPowerCaps caps;
    CHECK(tm_power_detect(&caps) == -1 && !caps.has_cpufreq);
    sysfs_put(POL "/scaling_available_frequencies",
              "408000 600000 816000 1008000 1200000 1416000 1608000 1800000 2000000 \n");
    sysfs_put(POL "/scaling_available_governors", "interactive conservative ondemand userspace powersave performance schedutil\n");
    sysfs_put(POL "/cpuinfo_min_freq", "408000\n");
    sysfs_put(POL "/cpuinfo_max_freq", "2000000\n");
    sysfs_put(POL "/scaling_min_freq", "408000\n");
    sysfs_put(POL "/scaling_max_freq", "2000000\n");
    sysfs_put(POL "/scaling_cur_freq", "1008000\n");
    sysfs_put(POL "/scaling_governor", "ondemand\n");
    sysfs_put("/sys/class/thermal/thermal_zone0/type", "cpu_thermal_zone\n");
    sysfs_put("/sys/class/thermal/thermal_zone0/temp", "47000\n");
    sysfs_put("/sys/class/thermal/thermal_zone1/type", "gpu_thermal_zone\n");
    sysfs_put("/sys/class/thermal/thermal_zone1/temp", "99\n"); /* implausible unit: ignored */
    CHECK(tm_power_detect(&caps) == 0 && caps.has_cpufreq && caps.nfreqs == 9);
    CHECK(tm_power_temp_mc(&caps) == 47000);
    const TmPowerProfile *pr;
    size_t n = tm_power_profiles(&pr);
    for (size_t i = 0; i < n; i++) {
        TmPowerTarget t;
        CHECK(tm_power_plan(&caps, &pr[i], &t) == 0);
        CHECK(t.max_khz <= TM_POWER_HARD_CAP_KHZ);
        CHECK(t.min_khz <= t.max_khz);
        CHECK(strcmp(t.governor, "performance") != 0);
    }
    CHECK(tm_power_apply(&caps, tm_power_profile("performance")) == 0);
    CHECK(sysfs_long(POL "/scaling_max_freq") == 1800000);
    CHECK(sysfs_long(POL "/scaling_min_freq") == 1008000);
    CHECK(tm_power_apply(&caps, tm_power_profile("economy")) == 0);
    CHECK(sysfs_long(POL "/scaling_max_freq") == 1200000);
    CHECK(sysfs_long(POL "/scaling_min_freq") == 408000);
    CHECK(tm_power_apply(&caps, tm_power_profile("balanced")) == 0);
    CHECK(sysfs_long(POL "/scaling_max_freq") == 1608000);
    /* a table without any OPP under the cap must be refused, not exceeded */
    caps.nfreqs = 1;
    caps.freqs[0] = 2000000;
    TmPowerTarget t;
    CHECK(tm_power_plan(&caps, tm_power_profile("economy"), &t) == -1);
    /* unwritable files: feature reported unavailable */
    char p[1024];
    snprintf(p, sizeof p, "%s/sysfs" POL "/scaling_max_freq", T);
    chmod(p, 0444);
    if (access(p, W_OK) != 0) { /* skipped when running as root */
        CHECK(tm_power_detect(&caps) == -1);
        CHECK_STR(caps.reason, "power.reason.not_writable");
    }
    chmod(p, 0644);
    unsetenv("TRIMUX_SYSFS_ROOT");
}

static void test_leds(void)
{
    char root[1024];
    snprintf(root, sizeof root, "%s/ledsys", T);
    setenv("TRIMUX_SYSFS_ROOT", root, 1);
    TmLeds leds;
    CHECK(tm_leds_detect(&leds) == -1 && !leds.available);
    const char *zone_files[] = {"effect_%s", "effect_rgb_hex_%s", "effect_duration_%s", "effect_cycles_%s"};
    const char *zones[] = {"m", "f1"};
    for (size_t z = 0; z < 2; z++)
        for (size_t f = 0; f < 4; f++) {
            char rel[128], name[64];
            snprintf(name, sizeof name, zone_files[f], zones[z]);
            snprintf(rel, sizeof rel, "ledsys/sys/class/led_anim/%s", name);
            put(rel, "0\n");
        }
    put("ledsys/sys/class/led_anim/effect_lr", "0\n"); /* incomplete zone: hidden */
    put("ledsys/sys/class/led_anim/max_scale", "0\n");
    put("ledsys/sys/class/led_anim/effect_enable", "0\n");
    CHECK(tm_leds_detect(&leds) == 0 && leds.nzones == 2);
    CHECK(tm_leds_zone(&leds, "lr") == NULL);
    const TmLedZone *m = tm_leds_zone(&leds, "m");
    CHECK(m && m->has_brightness && strcmp(m->brightness_attr, "max_scale") == 0);
    const TmLedZone *f1 = tm_leds_zone(&leds, "f1");
    CHECK(f1 && !f1->has_brightness);
    TmLedSetting s = {.on = 1, .color = 0x12ab34, .brightness = 250, .effect = 99};
    CHECK(tm_leds_apply(&leds, "m", &s) == 0);
    char p[1024], v[64];
    snprintf(p, sizeof p, "%s/sys/class/led_anim/effect_rgb_hex_m", root);
    tm_read_line(p, v, sizeof v);
    CHECK_STR(v, "12AB34");
    snprintf(p, sizeof p, "%s/sys/class/led_anim/max_scale", root);
    tm_read_line(p, v, sizeof v);
    CHECK_STR(v, "100"); /* clamped */
    snprintf(p, sizeof p, "%s/sys/class/led_anim/effect_m", root);
    tm_read_line(p, v, sizeof v);
    CHECK_STR(v, "4"); /* invalid effect -> static */
    s.on = 0;
    tm_leds_apply(&leds, "m", &s);
    tm_read_line(p, v, sizeof v);
    CHECK_STR(v, "0");
    CHECK(tm_leds_apply(&leds, "rear", &s) == -1);
    unsetenv("TRIMUX_SYSFS_ROOT");
}

static void test_thermal(void)
{
    TmThermalGuard g;
    tm_thermal_init(&g, 75000, 65000, 3);
    CHECK(tm_thermal_step(&g, 76000) == TM_THERMAL_NONE);
    CHECK(tm_thermal_step(&g, 76000) == TM_THERMAL_NONE);
    CHECK(tm_thermal_step(&g, 70000) == TM_THERMAL_NONE); /* resets streak */
    CHECK(tm_thermal_step(&g, 76000) == TM_THERMAL_NONE);
    CHECK(tm_thermal_step(&g, 77000) == TM_THERMAL_NONE);
    CHECK(tm_thermal_step(&g, 78000) == TM_THERMAL_THROTTLE);
    CHECK(tm_thermal_step(&g, -1) == TM_THERMAL_NONE);
    CHECK(tm_thermal_step(&g, 60000) == TM_THERMAL_NONE);
    CHECK(tm_thermal_step(&g, 60000) == TM_THERMAL_NONE);
    CHECK(tm_thermal_step(&g, 60000) == TM_THERMAL_RESTORE);
}

static void test_launch(void)
{
    char sd[1024], tmp[1024];
    snprintf(sd, sizeof sd, "%s/lsd", T);
    snprintf(tmp, sizeof tmp, "%s/ltmp", T);
    setenv("TRIMUX_SDCARD", sd, 1);
    setenv("TRIMUX_TMP", tmp, 1);
    TmPaths p;
    CHECK(tm_paths_init(&p) == 0);
    tm_paths_ensure(&p);
    put("lsd/TriMux/share/systems.ini", SYSTEMS);
    put("lsd/TriMux/share/emulators.ini", EMUS);
    put("lsd/TriMux/retroarch/cores/mgba_libretro.so", "x");
    put("lsd/Roms/GBA/game.gba", "x");
    put("lsd/Roms/GBA/notes.txt", "x");
    TmCatalog cat;
    CHECK(tm_catalog_load(&cat, "/nonexistent", "/nonexistent") == 0); /* missing files = empty */
    tm_catalog_free(&cat);
    char s[1024], e[1024];
    snprintf(s, sizeof s, "%s/systems.ini", p.share);
    snprintf(e, sizeof e, "%s/emulators.ini", p.share);
    CHECK(tm_catalog_load(&cat, s, e) == 0);
    TmLaunch l;
    char err[64];
    CHECK(tm_launch_write(&p, "GBA", "Roms/GBA/game.gba", "mgba") == 0);
    CHECK(tm_launch_read(&p, &cat, &l, err, sizeof err) == 0);
    CHECK(tm_launch_write(&p, "GBA", "Roms/GBA/game.gba", "gpsp") == 0);
    CHECK(tm_launch_read(&p, &cat, &l, err, sizeof err) == -1);
    CHECK_STR(err, "launch.err.core_missing");
    CHECK(tm_launch_write(&p, "GBA", "../../etc/passwd.gba", "mgba") == 0);
    CHECK(tm_launch_read(&p, &cat, &l, err, sizeof err) == -1);
    CHECK(tm_launch_write(&p, "GBA", "Roms/GBA/notes.txt", "mgba") == 0);
    CHECK(tm_launch_read(&p, &cat, &l, err, sizeof err) == -1);
    CHECK(tm_launch_write(&p, "PS", "Roms/GBA/game.gba", "mgba") == 0);
    CHECK(tm_launch_read(&p, &cat, &l, err, sizeof err) == -1);
    CHECK_STR(err, "launch.err.emulator");
    CHECK(tm_launch_write(&p, "GBA", "Roms/GBA/game.gba", "mgba") == 0);
    CHECK(tm_launch_read(&p, &cat, &l, err, sizeof err) == 0);
    char app[1024];
    CHECK(tm_launch_write_ra_append(&p, &l, "user_language = \"7\"", app, sizeof app) == 0);
    char *cfg = tm_read_file(app, 4096, NULL);
    CHECK(cfg && strstr(cfg, "savefile_directory = \"") && strstr(cfg, "/Saves/GBA\""));
    CHECK(cfg && strstr(cfg, "user_language = \"7\""));
    CHECK(tm_ra_language("pt_BR") == 7 && tm_ra_language("xx") == 0);
    free(cfg);
    TmIni ini;
    CHECK(tm_settings_load(&ini, &p) == 1);
    CHECK_STR(tm_ini_get(&ini, "power", "profile", "?"), "auto");
    CHECK_STR(tm_ini_get(&ini, "general", "language", "?"), "pt_BR");
    tm_ini_set(&ini, "meta", "version", "0");
    tm_settings_save(&ini, &p);
    tm_ini_free(&ini);
    CHECK(tm_settings_load(&ini, &p) == 0);
    char bak[1100];
    snprintf(bak, sizeof bak, "%s.bak-v0", p.config);
    CHECK(tm_file_exists(bak));
    tm_ini_free(&ini);
    tm_catalog_free(&cat);
}

static void test_sysinfo(void)
{
    char root[1024];
    snprintf(root, sizeof root, "%s/si", T);
    setenv("TRIMUX_SYSFS_ROOT", root, 1);
    put("si/sys/class/power_supply/axp2202-battery/type", "Battery\n");
    put("si/sys/class/power_supply/axp2202-battery/capacity", "87\n");
    put("si/sys/class/power_supply/axp2202-battery/status", "Charging\n");
    put("si/sys/class/power_supply/axp2202-usb/type", "USB\n");
    put("si/proc/meminfo", "MemTotal: 1000000 kB\nMemAvailable: 600000 kB\nSwapTotal: 0 kB\n");
    put("si/etc/version", "1.1.1\n");
    TmSysInfo si;
    tm_sysinfo_read(&si, T);
    CHECK(si.battery_pct == 87 && si.charging == 1);
    CHECK(si.mem_total_kb == 1000000 && si.mem_avail_kb == 600000);
    CHECK_STR(si.firmware, "1.1.1");
    CHECK(si.sd_total > 0);
    char b[32];
    tm_format_bytes(1536ull * 1024 * 1024, b, sizeof b);
    CHECK_STR(b, "1,5 GB");
    unsetenv("TRIMUX_SYSFS_ROOT");
}

int main(void)
{
    snprintf(T, sizeof T, "/tmp/trimux-unit-%d", (int)getpid());
    tm_mkdir_p(T);
    tm_log_init(NULL, 0, "test");
    test_util();
    test_ini();
    test_i18n();
    test_catalog();
    test_library();
    test_lists();
    test_power();
    test_leds();
    test_thermal();
    test_launch();
    test_sysinfo();
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", T);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: could not clean %s\n", T);
    printf("%d checks, %d failures\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
