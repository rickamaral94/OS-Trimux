/* Unit tests for the portable core. Run natively: make test-unit */
#define _GNU_SOURCE
#include "../../src/core/apps.h"
#include "../../src/core/buttons.h"
#include "../../src/core/clock.h"
#include "../../src/core/catalog.h"
#include "../../src/core/i18n.h"
#include "../../src/core/ini.h"
#include "../../src/core/launch.h"
#include "../../src/core/leds.h"
#include "../../src/core/library.h"
#include "../../src/core/lists.h"
#include "../../src/core/net.h"
#include "../../src/core/log.h"
#include "../../src/core/paths.h"
#include "../../src/core/perf.h"
#include "../../src/core/popular.h"
#include "../../src/core/image.h"
#include "../../src/core/scrape.h"
#include "../../src/core/sha256.h"
#include "../../src/core/update.h"
#include "../../src/core/power.h"
#include "../../src/core/sysinfo.h"
#include "../../src/core/util.h"
#include "../../src/core/video.h"

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
    put("i18n/pt_BR.lang", "lang.name = Português (Brasil)\nhello = Olá\nonly.pt = só pt\nmulti = a\\nb\n");
    put("i18n/en_US.lang", "lang.name = English\nhello = Hello\n");
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/i18n", T);
    CHECK(tm_i18n_load(dir, "en_US") == 0);
    CHECK_STR(tm_tr("hello"), "Hello");
    CHECK_STR(tm_tr("only.pt"), "só pt"); /* falls back to pt_BR */
    CHECK_STR(tm_tr("missing.key"), "missing.key");
    CHECK_STR(tm_tr("multi"), "a\nb"); /* "\\n" in a .lang file is a line break */
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
    /* opt-in boost: up to the firmware's 2.0 GHz OPP, never above it */
    CHECK(tm_power_boost_available(&caps));
    {
        TmPowerTarget bt;
        CHECK(tm_power_plan(&caps, tm_power_profile("boost"), &bt) == 0 && bt.max_khz == 2000000);
        CHECK(strcmp(bt.governor, "performance") != 0);
        long saved = caps.freqs[caps.nfreqs - 1];
        caps.freqs[caps.nfreqs - 1] = 2208000; /* a hypothetical higher OPP is ignored */
        CHECK(tm_power_plan(&caps, tm_power_profile("boost"), &bt) == 0 && bt.max_khz == 1800000);
        caps.freqs[caps.nfreqs - 1] = saved;
    }
    CHECK(tm_power_apply(&caps, tm_power_profile("boost")) == 0);
    CHECK(sysfs_long(POL "/scaling_max_freq") == 2000000);
    CHECK(tm_power_apply(&caps, tm_power_profile("balanced")) == 0);
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
    snprintf(p, sizeof p, "%s/sys/class/led_anim/effect_cycles_m", root);
    tm_read_line(p, v, sizeof v);
    CHECK_STR(v, "30000"); /* never a single repetition, never negative */
    snprintf(p, sizeof p, "%s/sys/class/led_anim/effect_m", root);
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

static void test_buttons(void)
{
    char root[1024];
    snprintf(root, sizeof root, "%s/btn", T);
    setenv("TRIMUX_SYSFS_ROOT", root, 1);
    TmIni ini;
    tm_ini_init(&ini);
    CHECK(tm_switch_raw() == -1 && tm_switch_active(&ini) == TM_SWITCH_NONE);
    put("btn/sys/class/gpio/gpio243/value", "1\n");
    tm_ini_set(&ini, "buttons", "switch", "economy");
    CHECK(tm_switch_raw() == 1 && tm_switch_active(&ini) == TM_SWITCH_ECONOMY);
    tm_ini_set(&ini, "buttons", "switch_invert", "1");
    CHECK(tm_switch_on(&ini) == 0 && tm_switch_active(&ini) == TM_SWITCH_NONE);
    put("btn/sys/class/gpio/gpio243/value", "7\n"); /* implausible value: unreadable */
    CHECK(tm_switch_raw() == -1);
    CHECK(tm_key_action_parse("random", TM_KEY_NONE) == TM_KEY_RANDOM);
    CHECK(tm_key_action_parse("bogus", TM_KEY_FAVORITE) == TM_KEY_FAVORITE);
    CHECK(tm_switch_action_parse("../x") == TM_SWITCH_NONE);
    /* boost needs the explicit acknowledgement */
    tm_ini_set(&ini, "buttons", "switch_invert", "0");
    put("btn/sys/class/gpio/gpio243/value", "1\n");
    tm_ini_set(&ini, "buttons", "switch", "boost");
    CHECK(tm_switch_active(&ini) == TM_SWITCH_NONE);
    tm_ini_set(&ini, "power", "boost_ack", "1");
    CHECK(tm_switch_active(&ini) == TM_SWITCH_BOOST);
    CHECK(!tm_speaker_mute_available());
    put("btn/sys/class/speaker/mute", "0\n");
    CHECK(tm_speaker_mute_available() && tm_speaker_mute(1) == 0);
    tm_ini_free(&ini);
    unsetenv("TRIMUX_SYSFS_ROOT");
}

static void test_net(void)
{
    char out[512];
    tm_wpa_unescape("caf\\xc3\\xa9 \\\"x\\\" \\\\", out, sizeof out);
    CHECK_STR(out, "caf\xc3\xa9 \"x\" \\");
    tm_wpa_unescape("bad\\xZZ", out, sizeof out);
    CHECK_STR(out, "badxZZ");

    const char *scan = "bssid / frequency / signal level / flags / ssid\n"
                       "aa:bb:cc:dd:ee:01\t2437\t-71\t[WPA2-PSK-CCMP][ESS]\tCasa\n"
                       "aa:bb:cc:dd:ee:02\t5180\t-48\t[WPA2-PSK-CCMP][ESS]\tCasa\n"
                       "aa:bb:cc:dd:ee:03\t2412\t-60\t[ESS]\tCafe\\x20Livre\n"
                       "aa:bb:cc:dd:ee:04\t2412\t-50\t[WPA2-EAP-CCMP][ESS]\tEmpresa\n"
                       "aa:bb:cc:dd:ee:05\t2412\t-40\t[WPA2-PSK-CCMP][ESS]\t\n"
                       "aa:bb:cc:dd:ee:06\t2412\t-85\t[RSN-SAE-CCMP][ESS]\tSo WPA3\n"
                       "garbage line\n";
    TmWifiAp aps[8];
    size_t n = tm_wifi_parse_scan(scan, aps, 8);
    CHECK(n == 4); /* hidden skipped, duplicate merged */
    CHECK_STR(aps[0].ssid, "Casa");
    CHECK(aps[0].signal == -48 && aps[0].security == TM_WIFI_PSK);
    CHECK_STR(aps[1].ssid, "Empresa");
    CHECK(aps[1].security == TM_WIFI_UNSUPPORTED);
    CHECK_STR(aps[2].ssid, "Cafe Livre");
    CHECK(aps[2].security == TM_WIFI_OPEN);
    CHECK(aps[3].security == TM_WIFI_UNSUPPORTED);
    CHECK(tm_wifi_parse_scan(scan, aps, 1) == 1);

    TmWifiNet nets[4];
    size_t nn = tm_wifi_parse_networks("network id / ssid / bssid / flags\n0\tCasa\tany\t[CURRENT]\n"
                                       "1\tOutra\tany\t[DISABLED]\n",
                                       nets, 4);
    CHECK(nn == 2 && nets[0].id == 0 && nets[0].current && nets[1].id == 1 && !nets[1].current);
    n = tm_wifi_parse_scan(scan, aps, 8);
    tm_wifi_mark_saved(aps, n, nets, nn);
    CHECK(aps[0].saved_id == 0 && aps[0].current && aps[2].saved_id == -1);

    TmWifiStatus st;
    tm_wifi_parse_status("bssid=aa:bb\nssid=Casa\nwpa_state=COMPLETED\nip_address=192.168.0.23\n", &st);
    CHECK_STR(st.state, "COMPLETED");
    CHECK_STR(st.ssid, "Casa");
    CHECK_STR(st.ip, "192.168.0.23");

    CHECK(tm_wifi_ssid_hex("Ab \"1", out, sizeof out) == 0);
    CHECK_STR(out, "4162202231");
    CHECK(tm_wifi_ssid_hex("", out, sizeof out) == -1);
    CHECK(tm_wifi_ssid_hex("123456789012345678901234567890123", out, sizeof out) == -1);
    CHECK(tm_wifi_psk_valid("12345678") && tm_wifi_psk_valid("com espaço") == 0 && !tm_wifi_psk_valid("1234567"));
    CHECK(tm_wifi_psk_valid("a b\"c'd$e;f"));
    CHECK(tm_wifi_bars(-50) == 3 && tm_wifi_bars(-65) == 2 && tm_wifi_bars(-75) == 1 && tm_wifi_bars(-90) == 0);

    TmIni ini;
    tm_ini_init(&ini);
    char cfg[512];
    CHECK(tm_cheevos_cfg(&ini, cfg, sizeof cfg) == 0 && cfg[0] == '\0');
    tm_ini_set(&ini, "cheevos", "user", "jogador");
    tm_ini_set(&ini, "cheevos", "password", "s3nh@ x");
    CHECK(tm_cheevos_cfg(&ini, cfg, sizeof cfg) == 0 && cfg[0] == '\0'); /* not enabled */
    tm_ini_set_long(&ini, "cheevos", "enable", 1);
    CHECK(tm_cheevos_cfg(&ini, cfg, sizeof cfg) == 0);
    CHECK(strstr(cfg, "cheevos_enable = \"true\"") && strstr(cfg, "cheevos_username = \"jogador\"") &&
          strstr(cfg, "cheevos_password = \"s3nh@ x\"") && strstr(cfg, "cheevos_hardcore_mode_enable = \"false\""));
    tm_ini_set(&ini, "cheevos", "password", "a\"b");
    CHECK(tm_cheevos_cfg(&ini, cfg, sizeof cfg) == -1);
    tm_ini_free(&ini);

    /* command path, with fake firmware tools under TRIMUX_SYSFS_ROOT */
    char root[600], log[700];
    snprintf(root, sizeof root, "%s/netroot", T);
    snprintf(log, sizeof log, "%s/net.log", T);
    char script[1400];
    snprintf(script, sizeof script,
             "#!/bin/sh\necho \"cli $*\" >> '%s'\ncase \"$5\" in\n"
             "add_network) echo 'Selected interface' ; echo 3;;\n"
             "list_network) printf 'network id / ssid / bssid / flags\\n0\\tCasa\\tany\\t[CURRENT]\\n';;\n"
             "status) printf 'wpa_state=COMPLETED\\nssid=Casa\\nip_address=10.0.0.5\\n';;\n"
             "remove_network) [ \"$6\" = 9 ] && echo FAIL || echo OK;;\n*) echo OK;;\nesac\n",
             log);
    put("netroot/usr/sbin/wpa_cli", script);
    snprintf(script, sizeof script, "#!/bin/sh\necho \"bb $*\" >> '%s'\n[ \"$1\" = pidof ] && exit 1\nexit 0\n", log);
    put("netroot/bin/busybox", script);
    put("netroot/usr/sbin/wpa_supplicant", "#!/bin/sh\nexit 0\n");
    put("netroot/sys/class/net/wlan0/operstate", "up\n");
    char p[800];
    const char *bins[] = {"usr/sbin/wpa_cli", "bin/busybox", "usr/sbin/wpa_supplicant"};
    for (size_t i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/%s", root, bins[i]);
        chmod(p, 0755);
    }
    setenv("TRIMUX_SYSFS_ROOT", root, 1);
    CHECK(tm_wifi_available());
    CHECK(!tm_bt_available() && !tm_ssh_available());
    CHECK(tm_wifi_status(&st) == 0 && strcmp(st.ip, "10.0.0.5") == 0);
    CHECK(tm_wifi_connect("Casa", NULL, 0) == 0);           /* saved: select only */
    CHECK(tm_wifi_connect("Nova", "curta", 0) == -1);       /* invalid passphrase */
    CHECK(tm_wifi_connect("Nova Rede", "senha segura", 0) == 0);
    CHECK(tm_wifi_forget(9) == -1 && tm_wifi_forget(1) == 0);
    CHECK(tm_wifi_set(0) == 0);
    char *l = tm_read_file(log, 1 << 16, NULL);
    CHECK(l && strstr(l, "cli -p /etc/wifi/sockets -i wlan0 select_network 0"));
    CHECK(l && strstr(l, "set_network 3 ssid 4e6f76612052656465"));
    CHECK(l && strstr(l, "set_network 3 psk \"senha segura\""));
    CHECK(l && strstr(l, "save_config"));
    CHECK(l && strstr(l, "bb ifconfig wlan0 down") && strstr(l, "bb killall -15 wpa_supplicant"));
    CHECK(l && !strstr(l, "curta"));
    free(l);
    char pidfile[700];
    snprintf(pidfile, sizeof pidfile, "%s/ftp.pid", T);
    CHECK(tm_ftp_start("10.0.0.5; rm", 2121, T, pidfile) == -1); /* only digits and dots */
    CHECK(tm_ftp_start("", 2121, T, pidfile) == -1);
    unsetenv("TRIMUX_SYSFS_ROOT");
    char argvbuf[] = "/bin/echo";
    char *argv[] = {argvbuf, "ola", NULL};
    CHECK(tm_run(argv, out, sizeof out, 2000) == 0 && strcmp(out, "ola\n") == 0);
    char sl[] = "/bin/sleep";
    char *argv2[] = {sl, "5", NULL};
    uint64_t t0 = tm_now_ms();
    CHECK(tm_run(argv2, NULL, 0, 200) == -1 && tm_now_ms() - t0 < 2000);
}

static void test_perf(void)
{
    char logdir[700], p[900];
    snprintf(logdir, sizeof logdir, "%s/perflogs", T);
    TmPerf pf;
    CHECK(tm_perf_open(&pf, logdir, "GBA", "gpsp", "Roms/GBA/Jogo; com ponto.gba", "balanced") == 0);
    CHECK(strcmp(pf.game, "Jogo, com ponto.gba") == 0); /* name only, no separator */
    tm_perf_sample(&pf, 1000, 1200000, 1608000, 48400, 80, 0, "balanced");
    tm_perf_sample(&pf, 11000, 1608000, 1608000, 61600, 79, 0, "balanced");
    tm_perf_throttled(&pf);
    tm_perf_sample(&pf, 1801000, -1, 1200000, 55000, 70, 0, "economy");
    char file[1100];
    tm_strlcpy(file, pf.file, sizeof file);
    tm_perf_close(&pf, 1801000, 0);
    char *samples = tm_read_file(file, 1 << 16, NULL);
    CHECK(samples && strncmp(samples, "tempo_s;cpu_mhz;", 16) == 0);
    CHECK(samples && strstr(samples, "\n0;1200;1608;48;80;0;balanced\n"));
    CHECK(samples && strstr(samples, "\n1800;-1;1200;55;70;0;economy\n"));
    free(samples);
    TmPerfSummary r[4];
    CHECK(tm_perf_recent(logdir, r, 4) == 1);
    CHECK_STR(r[0].system, "GBA");
    CHECK(r[0].duration_s == 1800 && r[0].avg_mhz == 1404 && r[0].max_mhz == 1608);
    CHECK(r[0].temp_start_c == 48 && r[0].temp_max_c == 62 && r[0].temp_end_c == 55);
    CHECK(r[0].bat_start == 80 && r[0].bat_end == 70 && r[0].throttles == 1 && !r[0].charged);
    CHECK(tm_perf_drain_per_hour(&r[0]) == 20);
    TmPerfSummary c = r[0];
    c.charged = 1;
    CHECK(tm_perf_drain_per_hour(&c) == -1);
    c.charged = 0;
    c.duration_s = 120;
    CHECK(tm_perf_drain_per_hour(&c) == -1);

    /* newest first; damaged lines ignored */
    CHECK(tm_perf_open(&pf, logdir, "FC", "fceumm", "x.nes", "economy") == 0);
    tm_perf_sample(&pf, 0, 816000, 1200000, -1, -1, 1, NULL);
    tm_perf_close(&pf, 600000, 4);
    snprintf(p, sizeof p, "%s/perf/sessions.csv", logdir);
    FILE *f = fopen(p, "a");
    if (f) {
        fputs("lixo;sem;campos\n", f);
        fclose(f);
    }
    CHECK(tm_perf_recent(logdir, r, 4) == 2);
    CHECK_STR(r[0].system, "FC");
    CHECK(r[0].temp_max_c == -1 && r[0].bat_start == -1 && r[0].charged == 1 && r[0].exit_code == 4);
    CHECK_STR(r[1].system, "GBA");
    CHECK(tm_perf_recent(logdir, r, 1) == 1);

    /* round trip of the pure formatter */
    char line[512];
    TmPerfSummary back;
    CHECK(tm_perf_format_summary(&r[1], line, sizeof line) == 0 && tm_perf_parse_summary(line, &back) == 0);
    CHECK(back.duration_s == r[1].duration_s && strcmp(back.game, r[1].game) == 0);
    CHECK(tm_perf_parse_summary("inicio;plataforma", &back) == -1);

    /* pruning keeps the newest sample files; clearing removes only our files */
    char d[800];
    snprintf(d, sizeof d, "%s/perf", logdir);
    for (int i = 0; i < TM_PERF_KEEP_SESSIONS + 5; i++) {
        snprintf(p, sizeof p, "%s/20200101-0000%02d_GB.csv", d, i);
        f = fopen(p, "w");
        if (f)
            fclose(f);
    }
    snprintf(p, sizeof p, "%s/notas.txt", d);
    f = fopen(p, "w");
    if (f)
        fclose(f);
    CHECK(tm_perf_open(&pf, logdir, "GB", "gambatte", "y.gb", "economy") == 0);
    tm_perf_close(&pf, 0, 0);
    snprintf(p, sizeof p, "%s/20200101-000000_GB.csv", d);
    CHECK(!tm_file_exists(p)); /* oldest pruned */
    put("perflogs/retroarch/retroarch.log", "ra\n");
    CHECK(tm_perf_clear(logdir) >= TM_PERF_KEEP_SESSIONS);
    snprintf(p, sizeof p, "%s/notas.txt", d);
    CHECK(tm_file_exists(p)); /* not ours: kept */
    CHECK(tm_perf_recent(logdir, r, 4) == 0);
    snprintf(p, sizeof p, "%s/retroarch/retroarch.log", logdir);
    CHECK(!tm_file_exists(p));
}

static void test_scrape(void)
{
    char out[1024];
    tm_scrape_sanitize("Street Fighter II: The World Warrior / A&B?", out, sizeof out);
    CHECK_STR(out, "Street Fighter II_ The World Warrior _ A_B_");
    CHECK(tm_scrape_url("Nintendo - Game Boy Advance", TM_THUMB_BOXART, "Celeste Classic (World)", out, sizeof out) == 0);
    CHECK_STR(out, "https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/"
                   "Celeste%20Classic%20%28World%29.png");
    CHECK(tm_scrape_url("Sony - PlayStation", TM_THUMB_SNAP, "Café: Edição", out, sizeof out) == 0);
    CHECK(strstr(out, "/Named_Snaps/Caf%C3%A9_%20Edi%C3%A7%C3%A3o.png") != NULL);
    CHECK(tm_scrape_url("", TM_THUMB_TITLE, "x", out, sizeof out) == -1);
    CHECK(tm_scrape_url("R", TM_THUMB_TITLE, "x", out, 20) == -1);

    char c[8][256];
    size_t n = tm_scrape_candidates("Final Fantasy VII (USA) (Disc 1)", c, 8);
    CHECK(n == 2 && strcmp(c[1], "Final Fantasy VII (USA)") == 0);
    n = tm_scrape_candidates("Celeste", c, 8);
    CHECK(n == 5 && strcmp(c[1], "Celeste (USA)") == 0 && strcmp(c[4], "Celeste (Japan)") == 0);
    CHECK(tm_scrape_candidates("Micro Mages (World)", c, 8) == 1);
    CHECK(tm_scrape_candidates("", c, 8) == 0);

    CHECK(tm_scrape_cover_path("/sd", "Roms/NES/Micro Mages (World).nes", "FC", out, sizeof out) == 0);
    CHECK_STR(out, "/sd/Imgs/NES/Micro Mages (World).png");
    CHECK(tm_scrape_cover_path("/sd", "Games/x.gba", "GBA", out, sizeof out) == 0);
    CHECK_STR(out, "/sd/Imgs/GBA/x.png");
    CHECK(tm_scrape_cover_path("/sd", "Roms/../x.gba", "GBA", out, sizeof out) == -1);
    CHECK(tm_thumb_kind_parse("snap") == TM_THUMB_SNAP && tm_thumb_kind_parse("x") == TM_THUMB_BOXART);
    CHECK_STR(tm_thumb_kind_id(TM_THUMB_TITLE), "title");

    put("arcade.tsv", "sf2\tStreet Fighter II: The World Warrior (World 910522)\nmslug\tMetal Slug - Super Vehicle-001\r\n"
                      "lixo\n\tsem nome\n");
    char ap[700];
    snprintf(ap, sizeof ap, "%s/arcade.tsv", T);
    TmArcadeNames a;
    CHECK(tm_arcade_load(&a, ap) == 0 && a.count == 2);
    const char *t = tm_arcade_title(&a, "mslug");
    CHECK(t && strcmp(t, "Metal Slug - Super Vehicle-001") == 0);
    CHECK(tm_arcade_title(&a, "kof98") == NULL);
    tm_arcade_free(&a);
    CHECK(tm_arcade_load(&a, "/nonexistent") == -1);

    /* image: shrink keeps aspect ratio and never enlarges */
    char src[700], dst[700];
    snprintf(src, sizeof src, "%s/big.png", T);
    snprintf(dst, sizeof dst, "%s/small.png", T);
    unsigned char *px = calloc((size_t)600 * 900 * 4, 1);
    for (int i = 0; px && i < 600 * 900; i++)
        px[i * 4 + 3] = 255, px[i * 4] = (unsigned char)(i % 251);
    extern int tm_test_write_png(const char *, int, int, const unsigned char *);
    CHECK(px && tm_test_write_png(src, 600, 900, px) == 0);
    free(px);
    CHECK(tm_image_fit_png(src, dst, 480, 480) == 0);
    int w, h;
    unsigned char *r = tm_image_load_rgba(dst, &w, &h);
    CHECK(r && w == 320 && h == 480);
    tm_image_free(r);
    CHECK(tm_image_fit_png(dst, src, 1000, 1000) == 0); /* small stays small */
    r = tm_image_load_rgba(src, &w, &h);
    CHECK(r && w == 320 && h == 480);
    tm_image_free(r);
    put("notimage.png", "<html>404</html>");
    snprintf(src, sizeof src, "%s/notimage.png", T);
    CHECK(tm_image_fit_png(src, dst, 480, 480) == -1);
}

static void test_update(void)
{
    /* SHA-256 test vectors (FIPS 180-2) */
    char hex[65];
    TmSha256 c;
    unsigned char d[32];
    const char *msgs[] = {"", "abc", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    const char *want[] = {"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};
    for (int i = 0; i < 3; i++) {
        tm_sha256_init(&c);
        tm_sha256_update(&c, msgs[i], strlen(msgs[i]));
        tm_sha256_final(&c, d);
        for (int k = 0; k < 32; k++)
            snprintf(hex + 2 * k, 3, "%02x", d[k]);
        CHECK_STR(hex, want[i]);
    }
    /* a million "a", through the file helper */
    char path[512];
    snprintf(path, sizeof path, "%s/million", T);
    FILE *f = fopen(path, "wb");
    for (int i = 0; i < 1000000; i++)
        fputc('a', f);
    fclose(f);
    CHECK(tm_sha256_file(path, hex) == 0);
    CHECK_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    CHECK(tm_version_cmp("0.4.0", "0.3.0") > 0);
    CHECK(tm_version_cmp("v0.10.0", "0.9.3") > 0);
    CHECK(tm_version_cmp("0.3", "0.3.0") == 0);
    CHECK(tm_version_cmp("0.3.1", "0.3") > 0);
    CHECK(tm_version_cmp("1.0.0", "v1.0.0") == 0);
    CHECK(tm_version_cmp("0.4.0-rc1", "0.4.0") == 0);
    CHECK(tm_version_cmp("0.2.9", "0.3.0") < 0);

    const char *json =
        "[{\"tag_name\":\"v0.6.0\",\"draft\":true,\"prerelease\":false,\"body\":\"x\",\"assets\":["
        "{\"name\":\"TriMux-0.6.0-update.tar.gz\",\"browser_download_url\":\"https://e/6.tgz\",\"size\":9},"
        "{\"name\":\"TriMux-0.6.0-brickpro.sha256\",\"browser_download_url\":\"https://e/6.sha\",\"size\":1}]},"
        "{\"tag_name\":\"v0.5.0\",\"draft\":false,\"prerelease\":true,\"body\":\"pre\",\"assets\":["
        "{\"name\":\"TriMux-0.5.0-update.tar.gz\",\"browser_download_url\":\"https://e/5.tgz\",\"size\":5},"
        "{\"name\":\"TriMux-0.5.0-brickpro.sha256\",\"browser_download_url\":\"https://e/5.sha\",\"size\":1}]},"
        "{\"tag_name\":\"v0.4.1\",\"draft\":false,\"prerelease\":false,\"assets\":["
        "{\"name\":\"TriMux-0.4.1-update.zip\",\"browser_download_url\":\"https://e/41.zip\",\"size\":5}]},"
        "{\"tag_name\":\"v0.4.0\",\"draft\":false,\"prerelease\":false,\"author\":{\"login\":\"a\",\"n\":[1,{\"x\":2}]},"
        "\"body\":\"# TriMux 0.4.0\\r\\n\\r\\n**Novo:** atualiza\\u00e7\\u00e3o [online](docs/A.md) `trimuxctl`\\n"
        "## Detalhes\\nfim\\n# 0.3.0\\nantigo\",\"assets\":["
        "{\"name\":\"TriMux-0.4.0-brickpro.sha256\",\"browser_download_url\":\"https://e/4.sha\",\"size\":1},"
        "{\"name\":\"TriMux-0.4.0-update.tar.gz\",\"browser_download_url\":\"https://e/4.tgz\",\"size\":1234}]},"
        "{\"tag_name\":\"v0.9.0\",\"draft\":false,\"prerelease\":false,\"assets\":["
        "{\"name\":\"TriMux-0.9.0-update.tar.gz\",\"browser_download_url\":\"http://e/9.tgz\",\"size\":5},"
        "{\"name\":\"TriMux-0.9.0-brickpro.sha256\",\"browser_download_url\":\"https://e/9.sha\",\"size\":1}]},"
        "{\"tag_name\":\"v1.0/../x\",\"draft\":false,\"prerelease\":false,\"assets\":[]}]";
    TmRelease r;
    /* stable only: drafts, pre-releases, releases without the tar.gz and plain-http assets are skipped */
    CHECK(tm_update_pick(json, strlen(json), "0.3.0", 0, &r) == 1);
    CHECK_STR(r.version, "0.4.0");
    CHECK_STR(r.tag, "v0.4.0");
    CHECK_STR(r.pkg_url, "https://e/4.tgz");
    CHECK_STR(r.sha_url, "https://e/4.sha");
    CHECK_STR(r.pkg_name, "TriMux-0.4.0-update.tar.gz");
    CHECK(r.pkg_size == 1234 && !r.prerelease);
    /* notes: first part only, markdown removed, \uXXXX decoded */
    CHECK(strstr(r.notes, "Novo: atualização online trimuxctl") != NULL);
    CHECK(strstr(r.notes, "Detalhes") != NULL);
    CHECK(strstr(r.notes, "antigo") == NULL && strchr(r.notes, '#') == NULL && strchr(r.notes, '\r') == NULL);
    /* with pre-releases, the newest wins */
    CHECK(tm_update_pick(json, strlen(json), "0.3.0", 1, &r) == 1);
    CHECK_STR(r.version, "0.5.0");
    CHECK(r.prerelease == 1);
    /* nothing newer */
    CHECK(tm_update_pick(json, strlen(json), "0.5.0", 1, &r) == 0);
    CHECK(tm_update_pick(json, strlen(json), "0.4.0", 0, &r) == 0);
    /* broken replies */
    CHECK(tm_update_pick("{\"message\":\"rate limited\"}", 26, "0.3.0", 1, &r) == -1);
    CHECK(tm_update_pick("[{\"tag_name\":", 13, "0.3.0", 1, &r) == -1);
    CHECK(tm_update_pick("[]", 2, "0.3.0", 1, &r) == 0);

    const char *sums = "1111111111111111111111111111111111111111111111111111111111111111  TriMux-0.4.0-brickpro.img.xz\n"
                       "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789 *TriMux-0.4.0-update.tar.gz\n"
                       "zz  bad\n";
    CHECK(tm_update_sha_lookup(sums, "TriMux-0.4.0-update.tar.gz", hex) == 0);
    CHECK_STR(hex, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    CHECK(tm_update_sha_lookup(sums, "TriMux-0.4.0-update.zip", hex) == -1);
    CHECK(tm_update_sha_lookup("g111111111111111111111111111111111111111111111111111111111111111  a\n", "a", hex) == -1);
}

static void test_clock_apps(void)
{
    CHECK(tm_days_in_month(2024, 2) == 29 && tm_days_in_month(2026, 2) == 28 && tm_days_in_month(2100, 2) == 28);
    CHECK(tm_days_in_month(2026, 4) == 30 && tm_days_in_month(2026, 13) == 0);
    CHECK(tm_zone_index("America/Sao_Paulo") == 0 && tm_zone_index("Mars/Base") == -1);
    CHECK(tm_clock_set_local(2026, 2, 30, 10, 0) == -1); /* invalid date: nothing runs */
    CHECK(tm_clock_set_local(2026, 1, 1, 24, 0) == -1);
    /* app config: nested values skipped, launch must stay in the folder */
    char dir[600], p[700];
    snprintf(dir, sizeof dir, "%s/app1", T);
    tm_mkdir_p(dir);
    snprintf(p, sizeof p, "%s/config.json", dir);
    FILE *f = fopen(p, "w");
    fputs("{\"extra\":{\"label\":\"wrong\",\"x\":[1,2]},\"label\":\"Leitor\",\"label.pt_BR.lang\":\"Leitor PT\","
          "\"launch\":\"launch.sh\",\"description\":\"Livros\"}", f);
    fclose(f);
    snprintf(p, sizeof p, "%s/launch.sh", dir);
    f = fopen(p, "w");
    fputs("#!/bin/sh\n", f);
    fclose(f);
    TmApp a;
    CHECK(tm_app_load(dir, "pt_BR", &a) == 0);
    CHECK_STR(a.label, "Leitor PT");
    CHECK(tm_app_load(dir, "en_US", &a) == 0);
    CHECK_STR(a.label, "Leitor");
    CHECK_STR(a.desc, "Livros");
    snprintf(p, sizeof p, "%s/config.json", dir);
    f = fopen(p, "w");
    fputs("{\"label\":\"x\",\"launch\":\"../launch.sh\"}", f);
    fclose(f);
    CHECK(tm_app_load(dir, NULL, &a) == -1);
}

static void test_popular_video(void)
{
    char k1[128], k2[128];
    tm_popular_key("Legend of Zelda, The - A Link to the Past", k1, sizeof k1);
    tm_popular_key("The Legend of Zelda: A Link to the Past", k2, sizeof k2);
    CHECK_STR(k1, k2);
    tm_popular_key("Pok\xc3\xa9mon Pinball: Ruby & Sapphire", k1, sizeof k1);
    CHECK_STR(k1, "pokemonpinballrubyandsapphire");
    tm_popular_key("Theme Park", k1, sizeof k1); /* only a whole leading "the " goes */
    CHECK_STR(k1, "themepark");

    char dir[600], p[700];
    snprintf(dir, sizeof dir, "%s/share/popular", T);
    tm_mkdir_p(dir);
    snprintf(p, sizeof p, "%s/SFC.txt", dir);
    FILE *f = fopen(p, "w");
    fputs("# header\nSuper Mario World\nThe Legend of Zelda: A Link to the Past\n"
          "Final Fantasy VI|Final Fantasy III\nSuper Mario World|duplicate keeps rank 1\n", f);
    fclose(f);
    TmSystem sys[2];
    memset(sys, 0, sizeof sys);
    tm_strlcpy(sys[0].popular, "SFC", sizeof sys[0].popular);
    tm_strlcpy(sys[1].popular, "NONE", sizeof sys[1].popular);
    TmCatalog cat = {.systems = sys, .nsystems = 2};
    TmPopular pop;
    snprintf(dir, sizeof dir, "%s/share", T);
    CHECK(tm_popular_load(&pop, &cat, dir) == 0);
    CHECK(tm_popular_rank(&pop, 0, "Roms/SFC/Super Mario World (USA).sfc") == 1);
    CHECK(tm_popular_rank(&pop, 0, "Roms/SFC/Legend of Zelda, The - A Link to the Past (USA).zip") == 2);
    CHECK(tm_popular_rank(&pop, 0, "Roms/SFC/Final Fantasy III (USA) (Rev 1).sfc") == 3);
    CHECK(tm_popular_rank(&pop, 0, "Roms/SFC/Homebrew.sfc") == 0);
    CHECK(tm_popular_rank(&pop, 1, "Roms/X/Super Mario World.sfc") == 0); /* no list */
    CHECK(tm_popular_count(&pop, 0) == 4 && tm_popular_count(&pop, 1) == 0);
    tm_popular_free(&pop);

    /* play statistics: short sessions (failed starts) are not counted */
    snprintf(p, sizeof p, "%s/plays.ini", T);
    CHECK(tm_plays_add(p, "Roms/SFC/a b=c.sfc", 5, 100, 10) == 0);
    CHECK(!tm_file_exists(p));
    CHECK(tm_plays_add(p, "Roms/SFC/a b=c.sfc", 600, 100, 10) == 0);
    CHECK(tm_plays_add(p, "Roms/SFC/a b=c.sfc", 60, 200, 10) == 0);
    TmIni ini;
    tm_ini_init(&ini);
    tm_ini_load(&ini, p);
    TmPlays pl;
    tm_plays_get(&ini, "Roms/SFC/a b=c.sfc", &pl);
    CHECK(pl.times == 2 && pl.seconds == 660 && pl.last == 200);
    tm_plays_get(&ini, "Roms/SFC/other.sfc", &pl);
    CHECK(pl.times == 0 && pl.seconds == 0);
    tm_ini_free(&ini);

    /* image: core options keep other lines; a longer key with the same
     * prefix is not mistaken for ours */
    snprintf(p, sizeof p, "%s/core/x.opt", T);
    tm_mkdir_p(T);
    TmIni st;
    tm_ini_init(&st);
    tm_ini_set(&st, "video.GBA", "colors", "1");
    tm_ini_set(&st, "video.GBA", "ghost", "0");
    const TmVideoCaps *caps = tm_video_caps("gpsp");
    CHECK(caps != NULL && tm_video_caps("snes9x2005") == NULL);
    snprintf(dir, sizeof dir, "%s/core", T);
    tm_mkdir_p(dir);
    f = fopen(p, "w");
    fputs("gpsp_color_correction_extra = \"keep\"\ngpsp_frameskip = \"auto\"", f);
    fclose(f);
    CHECK(tm_video_core_options(&st, "GBA", caps, p) == 0);
    size_t len;
    char *o = tm_read_file(p, 4096, &len);
    CHECK(o && strstr(o, "gpsp_color_correction_extra = \"keep\"") && strstr(o, "gpsp_frameskip = \"auto\"\n") &&
          strstr(o, "gpsp_color_correction = \"enabled\"") && strstr(o, "gpsp_frame_mixing = \"disabled\""));
    free(o);
    char lines[256] = "", shader[256];
    tm_ini_set(&st, "video.GBA", "aspect", "bogus");
    CHECK(tm_video_ra_lines(&st, "GBA", T, lines, sizeof lines, shader, sizeof shader) == 0 && !lines[0] && !shader[0]);
    tm_ini_free(&st);
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
    test_buttons();
    test_net();
    test_perf();
    test_scrape();
    test_update();
    test_clock_apps();
    test_popular_video();
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", T);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: could not clean %s\n", T);
    printf("%d checks, %d failures\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
