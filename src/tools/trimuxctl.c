/* trimuxctl — TriMux command-line helper used by the boot scripts.
 *
 *   trimuxctl power list|status|apply <profile>|default
 *   trimuxctl leds detect|apply|keep <pid>
 *   trimuxctl switch                 show the side switch state and apply its LED/mute action
 *   trimuxctl sysinfo
 *   trimuxctl device                 exit 0 only on a TrimUI Brick Pro (TG4040) firmware
 *   trimuxctl scan
 *   trimuxctl launch                 run the game described in /tmp/trimux/launch.ini
 *   trimuxctl boot begin|ok|status   crash-loop protection counter
 *   trimuxctl fat-grow plan|apply <device-or-image>
 *   trimuxctl card-grow [--auto|--dry-run]  grow the mounted SD card (remounts read-only first)
 *   trimuxctl update check|install|rollback|status   online updates from the GitHub releases
 *   trimuxctl app                    run the app chosen in the menu (/tmp/trimux/app.ini)
 *   trimuxctl time tz|sync [--auto] [--wait N]   time zone for TZ, internet time
 */
#define _GNU_SOURCE
#include "../core/apps.h"
#include "../core/buttons.h"
#include "../core/clock.h"
#include "../core/catalog.h"
#include "../core/fatgrow.h"
#include "../core/ini.h"
#include "../core/launch.h"
#include "../core/leds.h"
#include "../core/library.h"
#include "../core/log.h"
#include "../core/net.h"
#include "../core/perf.h"
#include "../core/scrape.h"
#include "../core/paths.h"
#include "../core/power.h"
#include "../core/sysinfo.h"
#include "../core/update.h"
#include "../core/util.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BOOT_FAIL_LIMIT 3

static TmPaths P;

static void open_log(void)
{
    char log[TM_PATH_MAX];
    if (tm_path_join(log, sizeof log, P.logdir, "trimux.log") == 0 && tm_dir_exists(P.logdir))
        tm_log_init(log, 256 * 1024, "ctl");
    else
        tm_log_init(NULL, 0, "ctl");
    TmIni ini; /* [diag] verbose: detailed log, chosen in the menu */
    if (tm_settings_load(&ini, &P) == 0 && tm_ini_get_long(&ini, "diag", "verbose", 0))
        tm_log_set_level(TM_LOG_DEBUG);
    tm_ini_free(&ini);
}

static int load_catalog(TmCatalog *cat)
{
    char s[TM_PATH_MAX], e[TM_PATH_MAX];
    tm_path_join(s, sizeof s, P.share, "systems.ini");
    tm_path_join(e, sizeof e, P.share, "emulators.ini");
    if (tm_catalog_load(cat, s, e) != 0) {
        fprintf(stderr, "trimuxctl: cannot load catalog from %s\n", P.share);
        return -1;
    }
    return 0;
}

/* Profile for a launch: the user's choice, or the emulator's recommendation
 * when the setting is "auto". Anything unknown falls back to the default. */
static const char *chosen_profile(const TmEmulator *emu)
{
    static char prof[32];
    TmIni ini;
    tm_settings_load(&ini, &P);
    const char *v = tm_ini_get(&ini, "power", "profile", "auto");
    if (strcmp(v, "auto") == 0)
        v = emu ? emu->profile : TM_POWER_DEFAULT;
    tm_strlcpy(prof, tm_power_profile(v) ? v : TM_POWER_DEFAULT, sizeof prof);
    tm_ini_free(&ini);
    return prof;
}

static int cmd_power(int argc, char **argv)
{
    TmPowerCaps caps;
    int ok = tm_power_detect(&caps) == 0;
    const char *sub = argc > 0 ? argv[0] : "status";
    if (strcmp(sub, "list") == 0) {
        const TmPowerProfile *pr;
        size_t n = tm_power_profiles(&pr);
        for (size_t i = 0; i < n; i++) {
            TmPowerTarget t;
            if (ok && tm_power_plan(&caps, &pr[i], &t) == 0)
                printf("%s gov=%s min=%ld max=%ld\n", pr[i].id, t.governor, t.min_khz, t.max_khz);
            else
                printf("%s unavailable\n", pr[i].id);
        }
        return 0;
    }
    if (strcmp(sub, "status") == 0) {
        long cur = -1, mn = -1, mx = -1;
        char gov[32] = "?";
        if (ok)
            tm_power_read(&caps, &cur, &mn, &mx, gov, sizeof gov);
        printf("cpufreq=%s%s%s\ncur=%ld min=%ld max=%ld governor=%s\ntemp_mc=%ld\n", ok ? "yes" : "no",
               ok ? "" : " reason=", ok ? "" : caps.reason, cur, mn, mx, gov, tm_power_temp_mc(&caps));
        return ok ? 0 : 2;
    }
    if (!ok) {
        fprintf(stderr, "cpufreq unavailable: %s\n", caps.reason);
        return 2;
    }
    const char *id = NULL;
    if (strcmp(sub, "apply") == 0 && argc > 1)
        id = argv[1];
    else if (strcmp(sub, "default") == 0)
        id = TM_POWER_DEFAULT;
    const TmPowerProfile *p = tm_power_profile(id);
    if (!p) {
        fprintf(stderr, "unknown profile\n");
        return 1;
    }
    return tm_power_apply(&caps, p) == 0 ? 0 : 1;
}

/* What each zone should show: the user's settings, or all off (side switch
 * "LEDs off", or turned off with an F1/F2 shortcut). */
static void leds_desired(const TmIni *ini, const TmLeds *leds, int all_off, TmLedSetting *out)
{
    if (tm_ini_get_long(ini, "leds", "user_off", 0))
        all_off = 1;
    for (size_t i = 0; i < leds->nzones; i++) {
        char sec[32];
        snprintf(sec, sizeof sec, "leds.%s", leds->zones[i].id);
        out[i] = (TmLedSetting){
            .on = all_off ? 0 : (int)tm_ini_get_long(ini, sec, "on", 1),
            .color = (unsigned)strtoul(tm_ini_get(ini, sec, "color", "FFFFFF"), NULL, 16),
            .brightness = (int)tm_ini_get_long(ini, sec, "brightness", 60),
            .effect = (int)tm_ini_get_long(ini, sec, "effect", TM_LED_EFFECT_STATIC),
        };
    }
}

/* Applies the user's LED settings; all_off forces every zone off. Does
 * nothing unless the user let TriMux manage LEDs. only_changed: rewrite only
 * zones whose state differs from the setting (or is not reported), so a
 * running effect is not restarted. Returns the number of zones written. */
static int leds_apply_settings_ex(const TmIni *ini, const TmLeds *leds, int all_off, int only_changed, int *err)
{
    if (!tm_ini_get_long(ini, "leds", "managed", 0) && !all_off)
        return 0; /* user never changed LEDs: leave firmware behaviour alone */
    TmLedSetting want[TM_LED_MAX_ZONES];
    leds_desired(ini, leds, all_off, want);
    int written = 0, any_on = 0;
    for (size_t i = 0; i < leds->nzones; i++) {
        any_on |= want[i].on;
        /* unknown (driver does not report): rewritten only when forced, so
         * a breathing effect is not restarted every check */
        if (only_changed && tm_leds_matches(leds, leds->zones[i].id, &want[i]) != 0)
            continue;
        if (tm_leds_apply(leds, leds->zones[i].id, &want[i]) != 0 && err)
            *err = 1;
        written++;
    }
    /* everything off: the firmware's master switch too (the strongest off) */
    if (!any_on && tm_leds_master(leds, 0) != 0 && err)
        *err = 1;
    return written;
}

static int leds_apply_settings(const TmIni *ini, int all_off)
{
    TmLeds leds;
    if (tm_leds_detect(&leds) != 0)
        return 2;
    int err = 0;
    leds_apply_settings_ex(ini, &leds, all_off, 0, &err);
    return err ? 1 : 0;
}

static long file_mtime(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_mtime : 0;
}

/* trimuxctl leds keep <pid>: while process <pid> (the supervisor) lives,
 * keeps the LEDs as the user chose. The firmware's keymon and
 * hardwareservice, which TriMux runs for the keys and battery warnings,
 * re-apply the stock LED settings from /mnt/UDISK/system.json whenever that
 * file changes (keymon itself rewrites it on every volume change). Checked
 * every 2 s, reading the driver back, so unchanged zones are not touched. */
static int cmd_leds_keep(pid_t watch)
{
    TmLeds leds;
    if (tm_leds_detect(&leds) != 0)
        return 2;
    char pidf[TM_PATH_MAX], sysjson[TM_PATH_MAX], buf[32];
    tm_path_join(pidf, sizeof pidf, P.tmp, "leds-keep.pid");
    long other = 0;
    if (tm_read_long(pidf, &other) == 0 && other > 1 && other != getpid() && kill((pid_t)other, 0) == 0)
        return 0; /* already running */
    snprintf(buf, sizeof buf, "%d\n", (int)getpid());
    tm_atomic_write(pidf, buf, strlen(buf));
    tm_fw_path(sysjson, sizeof sysjson, "/mnt/UDISK/system.json");
    long seen = file_mtime(sysjson);
    LOGI("leds: keeping the user's LED settings (watching pid %d)", (int)watch);
    while (watch <= 1 || kill(watch, 0) == 0) {
        sleep(2);
        TmIni ini;
        tm_settings_load(&ini, &P);
        int bat = -1, chg = -1;
        tm_battery_read(&bat, &chg);
        long now = file_mtime(sysjson);
        int force = now != seen; /* the stock settings were just re-applied */
        seen = now;
        if (bat >= 0 && bat <= 10 && chg != 1) {
            /* the firmware's low-battery warning (red LEDs) has priority */
        } else {
            int err = 0;
            int n = leds_apply_settings_ex(&ini, &leds, tm_switch_active(&ini) == TM_SWITCH_LEDS_OFF, !force, &err);
            if (n > 0)
                LOGD("leds: %d zone(s) restored%s", n, force ? " after a firmware settings change" : "");
        }
        tm_ini_free(&ini);
    }
    unlink(pidf);
    return 0;
}

/* Side switch side effects that are not about CPU (LEDs, speaker). */
static void switch_effects(const TmIni *ini, TmSwitchAction now, TmSwitchAction before)
{
    if (now == before)
        return;
    if (before == TM_SWITCH_LEDS_OFF || now == TM_SWITCH_LEDS_OFF)
        leds_apply_settings(ini, now == TM_SWITCH_LEDS_OFF);
    if ((before == TM_SWITCH_MUTE || now == TM_SWITCH_MUTE) && tm_speaker_mute_available())
        tm_speaker_mute(now == TM_SWITCH_MUTE);
    LOGI("switch: %s -> %s", tm_switch_action_id(before), tm_switch_action_id(now));
}

static int cmd_switch(void)
{
    TmIni ini;
    tm_settings_load(&ini, &P);
    TmSwitchAction a = tm_switch_active(&ini);
    printf("switch_raw=%d switch_on=%d action=%s active=%s\n", tm_switch_raw(), tm_switch_on(&ini),
           tm_switch_action_id(tm_switch_action(&ini)), tm_switch_action_id(a));
    switch_effects(&ini, a, TM_SWITCH_NONE);
    TmPowerCaps caps;
    if ((a == TM_SWITCH_ECONOMY || a == TM_SWITCH_BOOST) && tm_power_detect(&caps) == 0)
        tm_power_apply(&caps, tm_power_profile(a == TM_SWITCH_BOOST ? "boost" : "economy"));
    tm_ini_free(&ini);
    return 0;
}

static int cmd_leds(int argc, char **argv)
{
    TmLeds leds;
    int ok = tm_leds_detect(&leds) == 0;
    const char *sub = argc > 0 ? argv[0] : "detect";
    if (strcmp(sub, "detect") == 0) {
        printf("leds=%s\n", ok ? "yes" : "no");
        for (size_t i = 0; i < leds.nzones; i++)
            printf("zone %s brightness=%s\n", leds.zones[i].id,
                   leds.zones[i].has_brightness ? leds.zones[i].brightness_attr : "no");
        return ok ? 0 : 2;
    }
    if (strcmp(sub, "keep") == 0)
        return ok ? cmd_leds_keep(argc > 1 ? (pid_t)atol(argv[1]) : getppid()) : 2;
    if (strcmp(sub, "apply") == 0) {
        if (!ok)
            return 2;
        TmIni ini;
        tm_settings_load(&ini, &P);
        int rc = leds_apply_settings(&ini, tm_switch_active(&ini) == TM_SWITCH_LEDS_OFF);
        tm_ini_free(&ini);
        return rc;
    }
    return 1;
}

static int cmd_sysinfo(void)
{
    TmSysInfo si;
    tm_sysinfo_read(&si, P.sd);
    TmPowerCaps caps;
    tm_power_detect(&caps);
    char tot[32], fr[32];
    tm_format_bytes(si.sd_total, tot, sizeof tot);
    tm_format_bytes(si.sd_free, fr, sizeof fr);
    printf("model=%s\nfirmware=%s\nbattery=%d\ncharging=%d\nmem_total_kb=%ld\nmem_avail_kb=%ld\n"
           "swap_total_kb=%ld\nsd_mounted=%d\nsd_fs=%s\nsd_ro=%d\nsd_total=%s\nsd_free=%s\ntemp_mc=%ld\n",
           si.model, si.firmware, si.battery_pct, si.charging, si.mem_total_kb, si.mem_avail_kb,
           si.swap_total_kb, si.sd_mounted, si.sd_fstype, si.sd_readonly, tot, fr, tm_power_temp_mc(&caps));
    return 0;
}

static int cmd_scan(void)
{
    TmCatalog cat;
    if (load_catalog(&cat) != 0)
        return 1;
    TmIni ini;
    tm_settings_load(&ini, &P);
    TmLibrary lib;
    tm_library_init(&lib);
    tm_library_scan(&lib, &cat, P.sd, (int)tm_ini_get_long(&ini, "general", "clean_names", 1));
    int rc = tm_library_save(&lib, &cat, P.library);
    printf("games=%zu folders=%zu\n", lib.count, lib.ndirs);
    tm_library_free(&lib);
    tm_ini_free(&ini);
    tm_catalog_free(&cat);
    return rc ? 1 : 0;
}

static volatile sig_atomic_t g_child = 0;
static void forward_signal(int sig)
{
    if (g_child > 0)
        kill(g_child, sig);
}

static int ensure_ra_config(char *cfg, size_t size)
{
    if (tm_mkdir_p(P.ra_home) != 0 || tm_path_join(cfg, size, P.ra_home, "retroarch.cfg") != 0)
        return -1;
    if (tm_file_exists(cfg))
        return 0;
    char base[TM_PATH_MAX];
    tm_path_join(base, sizeof base, P.retroarch, "retroarch.base.cfg");
    return tm_copy_file(base, cfg);
}

/* CPU profile while the side switch action is active: Economy or the opt-in
 * 2.0 GHz boost; otherwise the game's normal profile. */
static const TmPowerProfile *switch_profile(TmSwitchAction sw, const TmPowerProfile *normal)
{
    if (sw == TM_SWITCH_ECONOMY)
        return tm_power_profile("economy");
    if (sw == TM_SWITCH_BOOST)
        return tm_power_profile("boost");
    return normal;
}

/* Runs RetroArch once and supervises temperature until it exits. */
/* One performance sample (only when the log is on). */
static void perf_sample(TmPerf *perf, TmPowerCaps *caps, int have_power, const char *profile)
{
    if (!perf)
        return;
    long cur = -1, mx = -1;
    int bat = -1, chg = -1;
    if (have_power)
        tm_power_read(caps, &cur, NULL, &mx, NULL, 0);
    tm_battery_read(&bat, &chg);
    tm_perf_sample(perf, tm_now_ms(), cur, mx, caps->has_temp ? tm_power_temp_mc(caps) : -1, bat, chg, profile);
}

static int run_retroarch(const char *ra, const char *cfg, const char *append, const TmLaunch *l, TmPowerCaps *caps,
                         int have_power, int guard_on, const TmPowerProfile *prof, uint64_t *elapsed_ms,
                         TmPerf *perf)
{
    uint64_t t0 = tm_now_ms();
    pid_t pid = fork();
    if (pid < 0) {
        LOGE("launch: fork failed");
        return -1;
    }
    if (pid == 0) {
        if (strcmp(l->emu->type, "script") == 0) {
            /* Ports: the game is a launcher script on the card. It runs with
             * the firmware's shell from its own folder; no shell parsing of
             * the path happens here (execv). */
            char dir[TM_PATH_MAX];
            tm_strlcpy(dir, l->rom_abs, sizeof dir);
            char *slash = strrchr(dir, '/');
            if (slash)
                *slash = '\0';
            setenv("TRIMUX", "1", 1);
            setenv("TRIMUX_DEVICE", "brickpro", 1);
            if (chdir(dir) != 0)
                _exit(127);
            char *args[] = {"/bin/sh", (char *)l->rom_abs, NULL};
            execv("/bin/sh", args);
            _exit(127);
        }
        setenv("HOME", P.ra_home, 1);
        if (chdir(P.retroarch) != 0)
            _exit(127);
        char *args[] = {(char *)ra, "-c", (char *)cfg, "--appendconfig", (char *)append, "-L",
                        (char *)l->core_abs, (char *)l->rom_abs, NULL};
        execv(ra, args);
        _exit(127);
    }
    g_child = pid;
    signal(SIGTERM, forward_signal);
    signal(SIGINT, forward_signal);
    TmThermalGuard g;
    tm_thermal_init(&g, 75000, 65000, 3);
    TmIni ini;
    tm_settings_load(&ini, &P);
    const TmPowerProfile *economy = tm_power_profile("economy");
    TmSwitchAction sw = tm_switch_active(&ini);
    int status = 0, tick = 0;
    perf_sample(perf, caps, have_power, switch_profile(sw, prof)->id);
    for (;;) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid || (w < 0 && errno != EINTR))
            break;
        sleep(1);
        /* side switch: one GPIO read per second */
        TmSwitchAction now = tm_switch_active(&ini);
        if (now != sw) {
            switch_effects(&ini, now, sw);
            if (have_power && !g.throttled)
                tm_power_apply(caps, switch_profile(now, prof));
            sw = now;
        }
        if (++tick < 10)
            continue;
        tick = 0; /* every 10 s: temperature and CPU limit enforcement */
        /* thermal protection wins over everything, including boost */
        const TmPowerProfile *want = g.throttled ? economy : switch_profile(sw, prof);
        perf_sample(perf, caps, have_power, want->id);
        if (guard_on) {
            long t = tm_power_temp_mc(caps);
            int act = tm_thermal_step(&g, t);
            if (act == TM_THERMAL_THROTTLE && have_power) {
                if (perf)
                    tm_perf_throttled(perf);
                LOGW("thermal: %ld mC sustained, limiting to economy", t);
                tm_power_apply(caps, economy);
                continue;
            } else if (act == TM_THERMAL_RESTORE && have_power) {
                want = switch_profile(sw, prof);
                LOGI("thermal: %ld mC, restoring profile %s", t, want->id);
                tm_power_apply(caps, want);
                continue;
            }
        }
        /* Firmware FN shortcuts (keymon) can raise the CPU limit up to
         * 2.0 GHz behind our back; put the profile's limit back. */
        TmPowerTarget tgt;
        long cur_max = -1;
        if (have_power && tm_power_plan(caps, want, &tgt) == 0 &&
            tm_power_read(caps, NULL, NULL, &cur_max, NULL, 0) == 0 && cur_max > tgt.max_khz) {
            LOGW("power: limit raised externally to %ld kHz, restoring %s", cur_max, want->id);
            tm_power_apply(caps, want);
        }
    }
    tm_ini_free(&ini);
    g_child = 0;
    *elapsed_ms = tm_now_ms() - t0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

static int cmd_launch(void)
{
    TmCatalog cat;
    if (load_catalog(&cat) != 0)
        return 1;
    TmLaunch l;
    char err[64];
    if (tm_launch_read(&P, &cat, &l, err, sizeof err) != 0) {
        LOGE("launch refused: %s", err);
        fprintf(stderr, "%s\n", err);
        tm_catalog_free(&cat);
        return 3;
    }
    TmIni ini;
    tm_settings_load(&ini, &P);
    char extra[2048], cheevos[640];
    int len = snprintf(extra, sizeof extra, "user_language = \"%d\"",
                       tm_ra_language(tm_ini_get(&ini, "general", "language", "pt_BR")));
    /* RetroAchievements login (lines written only to the RAM config) */
    if (tm_cheevos_cfg(&ini, cheevos, sizeof cheevos) != 0)
        LOGW("launch: RetroAchievements account has unsupported characters, ignored");
    else if (cheevos[0] && len > 0 && (size_t)len < sizeof extra)
        len += snprintf(extra + len, sizeof extra - (size_t)len, "\n%s", cheevos);
    /* Settings > System > Logs: FPS on screen and RetroArch's own log file
     * (overwritten on every game, so it never grows across sessions) */
    if (tm_ini_get_long(&ini, "diag", "show_fps", 0) && len > 0 && (size_t)len < sizeof extra)
        len += snprintf(extra + len, sizeof extra - (size_t)len, "\nfps_show = \"true\"");
    char ralog[TM_PATH_MAX];
    if (tm_ini_get_long(&ini, "diag", "retroarch_log", 0) && tm_path_join(ralog, sizeof ralog, P.logdir, "retroarch") == 0 &&
        !strchr(ralog, '"') && tm_mkdir_p(ralog) == 0 && len > 0 && (size_t)len < sizeof extra)
        len += snprintf(extra + len, sizeof extra - (size_t)len,
                        "\nlog_verbosity = \"true\"\nlog_to_file = \"true\"\nlog_to_file_timestamp = \"false\"\n"
                        "frontend_log_level = \"1\"\nlibretro_log_level = \"1\"\nlog_dir = \"%s\"",
                        ralog);
    int perf_on = (int)tm_ini_get_long(&ini, "diag", "perf", 0);
    char ra[TM_PATH_MAX], cfg[TM_PATH_MAX], append[TM_PATH_MAX], cache[TM_PATH_MAX];
    tm_path_join(ra, sizeof ra, P.retroarch, "retroarch");
    tm_path_join(cache, sizeof cache, P.tmp, "cache"); /* RetroArch archive extraction, in RAM */
    tm_mkdir_p(cache);
    if (!tm_file_exists(ra) || ensure_ra_config(cfg, sizeof cfg) != 0 ||
        tm_launch_write_ra_append(&P, &l, extra, append, sizeof append) != 0) {
        LOGE("launch: RetroArch or its configuration is missing");
        tm_ini_free(&ini);
        tm_catalog_free(&cat);
        return 3;
    }

    /* power limits are applied BEFORE the emulator starts */
    TmPowerCaps caps;
    int have_power = tm_power_detect(&caps) == 0;
    const TmPowerProfile *prof = tm_power_profile(chosen_profile(l.emu));
    if (have_power)
        tm_power_apply(&caps, switch_profile(tm_switch_active(&ini), prof));
    int guard_on = (int)tm_ini_get_long(&ini, "power", "thermal_guard", 1) && caps.has_temp;
    tm_ini_free(&ini);

    LOGI("launch: %s with %s (%s) profile=%s", l.rom_rel, l.emu->id, l.system->id, prof->id);
    char marker[TM_PATH_MAX];
    tm_path_join(marker, sizeof marker, P.state, "in_game");
    tm_atomic_write(marker, l.rom_rel, strlen(l.rom_rel));

    TmPerf perf_s, *perf = NULL;
    if (perf_on) {
        if (tm_perf_open(&perf_s, P.logdir, l.system->id, l.emu->id, l.rom_rel, prof->id) == 0)
            perf = &perf_s;
        else
            LOGW("perf: could not create the performance log");
    }
    uint64_t elapsed = 0;
    int code = run_retroarch(ra, cfg, append, &l, &caps, have_power, guard_on, prof, &elapsed, perf);
    if (code != 0 && elapsed < 5000 && strcmp(l.emu->type, "retroarch") == 0) {
        /* Failed right away: retry once with RetroArch's SDL2 renderer, in
         * case the GLES context could not be created on this firmware. */
        LOGW("launch: RetroArch failed in %llu ms (status %d); retrying with video_driver=sdl2",
             (unsigned long long)elapsed, code);
        char extra2[2100];
        snprintf(extra2, sizeof extra2, "%s\nvideo_driver = \"sdl2\"", extra);
        if (tm_launch_write_ra_append(&P, &l, extra2, append, sizeof append) == 0)
            code = run_retroarch(ra, cfg, append, &l, &caps, have_power, guard_on, prof, &elapsed, perf);
    }
    unlink(marker);
    if (perf) {
        perf_sample(perf, &caps, have_power, NULL);
        tm_perf_close(perf, tm_now_ms(), code);
    }
    if (have_power) { /* menu profile, still honouring the side switch */
        TmIni after;
        tm_settings_load(&after, &P);
        tm_power_apply(&caps, switch_profile(tm_switch_active(&after), tm_power_profile(TM_POWER_DEFAULT)));
        tm_ini_free(&after);
    }
    LOGI("launch: emulator exited with %d after %llu s", code, (unsigned long long)elapsed / 1000);
    sync();
    tm_catalog_free(&cat);
    return code == 0 ? 0 : 4;
}

static int cmd_boot(int argc, char **argv)
{
    char path[TM_PATH_MAX];
    tm_mkdir_p(P.state);
    tm_path_join(path, sizeof path, P.state, "bootcount");
    long n = 0;
    tm_read_long(path, &n);
    const char *sub = argc > 0 ? argv[0] : "status";
    if (strcmp(sub, "begin") == 0) {
        /* counts boots that never reached the menu: a crash loop */
        char buf[16];
        snprintf(buf, sizeof buf, "%ld\n", n + 1);
        tm_atomic_write(path, buf, strlen(buf));
        if (n + 1 > BOOT_FAIL_LIMIT) {
            /* Safe mode lasts one boot: the stock UI opens now and the next
             * power-on tries TriMux again (the counter starts over). */
            LOGW("boot: %ld unfinished boots, starting safe mode", n + 1);
            tm_atomic_write(path, "0\n", 2);
            return 10;
        }
        return 0;
    }
    if (strcmp(sub, "ok") == 0) {
        tm_update_confirm(&P); /* a freshly installed update reached the menu */
        return tm_atomic_write(path, "0\n", 2) == 0 ? 0 : 1;
    }
    printf("bootcount=%ld\n", n);
    return 0;
}

static int cmd_fatgrow(int argc, char **argv)
{
    if (argc < 2)
        return 1;
    int apply = strcmp(argv[0], "apply") == 0;
    int fd = open(argv[1], (apply ? O_RDWR : O_RDONLY) | O_CLOEXEC);
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }
    uint64_t size;
    TmFatGrowPlan plan;
    char err[160] = "";
    int rc = tm_fatgrow_device_size(fd, &size) == 0 ? tm_fatgrow_plan(fd, size, &plan, err, sizeof err) : -1;
    if (rc < 0) {
        fprintf(stderr, "fat-grow: %s\n", err[0] ? err : "cannot size device");
        close(fd);
        return 1;
    }
    printf("partition_start=%u fs_sectors=%u max_sectors=%u new_sectors=%u\n", plan.part_start, plan.fs_total,
           plan.max_total, plan.new_total);
    if (rc == 1) {
        printf("nothing to do\n");
        close(fd);
        return 0;
    }
    if (apply && tm_fatgrow_apply(fd, &plan, err, sizeof err) != 0) {
        fprintf(stderr, "fat-grow: %s\n", err);
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}

/* Grows the mounted card to the whole card (metadata only, see fatgrow.h).
 *   card-grow            requested from the menu (Armazenamento › Expandir)
 *   card-grow --dry-run  only report
 *   card-grow --auto     first boot of a flashed image: only while the marker
 *                        TriMuxData/state/autogrow exists; tried once
 * Exit 0 = grown, 2 = failed after the read-only remount (both: reboot now),
 * 1 = nothing done, card untouched. */
static int cmd_card_grow(int argc, char **argv)
{
    int dry = 0, autorun = 0;
    for (int i = 0; i < argc; i++) {
        dry |= strcmp(argv[i], "--dry-run") == 0;
        autorun |= strcmp(argv[i], "--auto") == 0;
    }
    char auto_marker[TM_PATH_MAX], report[TM_PATH_MAX], marker[TM_PATH_MAX];
    tm_path_join(auto_marker, sizeof auto_marker, P.state, "autogrow");
    tm_path_join(report, sizeof report, P.state, "card-grown");
    if (autorun) {
        if (!tm_file_exists(auto_marker))
            return 1;
        unlink(auto_marker); /* one attempt only, whatever happens next */
        sync();
        LOGI("card-grow: first boot of a flashed card, growing the partition to the whole card");
    }
    tm_path_join(marker, sizeof marker, P.sys, "VERSION");
    if (!tm_file_exists(marker)) {
        fprintf(stderr, "not a TriMux card\n");
        return 1;
    }
    char part[256], disk[256];
    if (tm_card_device(P.sd, part, sizeof part, disk, sizeof disk) != 0) {
        LOGW("card-grow: the card is not partition 1 of an mmcblk disk mounted at %s; nothing changed", P.sd);
        fprintf(stderr, "card device not found or not a partitioned card\n");
        return 1;
    }
    int fd = open(disk, (dry ? O_RDONLY : O_RDWR) | O_CLOEXEC);
    uint64_t size;
    TmFatGrowPlan plan;
    char err[160] = "";
    int rc = (fd >= 0 && tm_fatgrow_device_size(fd, &size) == 0) ? tm_fatgrow_plan(fd, size, &plan, err, sizeof err)
                                                                  : -1;
    if (rc != 0 || dry) {
        if (rc < 0) {
            if (!err[0])
                snprintf(err, sizeof err, "cannot open %s: %s", disk, strerror(errno));
            LOGW("card-grow: %s; nothing changed", err);
            fprintf(stderr, "card-grow: %s\n", err);
        } else {
            if (rc == 1)
                LOGI("card-grow: %s already uses the whole card", part);
            printf("%s %u -> %u sectors\n", rc == 1 ? "nothing to do:" : "can grow:", plan.fs_total, plan.new_total);
        }
        if (fd >= 0)
            close(fd);
        return rc < 0 ? 1 : dry ? 0 : 1; /* here: a report, or nothing to grow */
    }
    if (tm_update_running(&P)) {
        LOGW("card-grow: an update is running; nothing changed");
        close(fd);
        return 1;
    }
    /* nothing may hold a file open for writing during the remount */
    for (int t = 0; t < 100 && tm_scrape_running(&P); t++) {
        if (t == 0)
            tm_scrape_request_stop(&P);
        usleep(100000);
    }
    LOGI("card-grow: %s %u -> %u sectors (%llu -> %llu MiB)", disk, plan.fs_total, plan.new_total,
         (unsigned long long)plan.fs_total / 2048, (unsigned long long)plan.new_total / 2048);
    /* for the menu after the reboot: the size before, to tell whether it worked */
    char buf[32];
    snprintf(buf, sizeof buf, "%llu\n", (unsigned long long)plan.fs_total * 512);
    tm_atomic_write(report, buf, strlen(buf));
    sync();
    /* read-only while the metadata changes: the kernel will not write the
     * boot sector/FSInfo behind our back. Fails safely if files are open. */
    if (mount(part, P.sd, NULL, MS_REMOUNT | MS_RDONLY, NULL) != 0) {
        LOGW("card-grow: cannot remount read-only (%s); nothing changed", strerror(errno));
        fprintf(stderr, "card-grow: cannot remount read-only (%s); nothing changed\n", strerror(errno));
        unlink(report);
        close(fd);
        return 1;
    }
    rc = tm_fatgrow_apply(fd, &plan, err, sizeof err);
    close(fd);
    sync();
    /* the card stays read-only: remounting it read-write now would let the
     * kernel write back its cached (old) boot sector. The caller reboots. */
    if (rc != 0) {
        fprintf(stderr, "card-grow: %s\n", err);
        return 2; /* read-only now: reboot anyway */
    }
    printf("grown %u -> %u sectors; reboot required\n", plan.fs_total, plan.new_total);
    return 0;
}

static int cmd_net(int argc, char **argv)
{
    const char *sub = argc > 0 ? argv[0] : "status";
    TmIni ini;
    tm_settings_load(&ini, &P);
    int rc = 0;
    if (strcmp(sub, "apply") == 0) {
        /* boot: any FTP server left by a crashed menu is stopped first */
        char pf[TM_PATH_MAX];
        if (tm_path_join(pf, sizeof pf, P.tmp, "ftp.pid") == 0)
            tm_ftp_stop(pf);
        tm_net_apply(&ini);
    } else if (strcmp(sub, "status") == 0) {
        TmWifiStatus st;
        int avail = tm_wifi_available(), on = avail && tm_wifi_running();
        if (on)
            tm_wifi_status(&st);
        printf("wifi_available=%d\nwifi_on=%d\n", avail, on);
        if (on)
            printf("wifi_state=%s\nwifi_ssid=%s\nwifi_ip=%s\n", st.state, st.ssid, st.ip);
        printf("bluetooth_available=%d\nbluetooth_on=%d\n", tm_bt_available(), tm_bt_available() && tm_bt_running());
        printf("ssh_available=%d\nssh_on=%d\n", tm_ssh_available(), tm_ssh_available() && tm_ssh_running());
    } else {
        fprintf(stderr, "usage: trimuxctl net apply|status\n");
        rc = 1;
    }
    tm_ini_free(&ini);
    return rc;
}

/* Game covers. --auto: only if [covers] auto = 1 (boot / after a rescan).
 * --retry: also games that were not found before. --wait N: seconds to wait
 * for Wi-Fi. --stop: ask a running scraper to stop. */
static int cmd_scrape(int argc, char **argv)
{
    int autorun = 0, retry = 0, wait_s = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--auto") == 0)
            autorun = 1;
        else if (strcmp(argv[i], "--retry") == 0)
            retry = 1;
        else if (strcmp(argv[i], "--wait") == 0 && i + 1 < argc)
            wait_s = atoi(argv[++i]);
        else if (strcmp(argv[i], "--stop") == 0) {
            tm_scrape_request_stop(&P);
            return 0;
        } else if (strcmp(argv[i], "--status") == 0) {
            TmScrapeStatus st;
            if (tm_scrape_status_read(&P, &st) != 0)
                return 1;
            printf("state=%s done=%d total=%d found=%d missing=%d running=%d\n", st.state, st.done, st.total,
                   st.found, st.missing, tm_scrape_running(&P));
            return 0;
        }
    }
    TmIni ini;
    tm_settings_load(&ini, &P);
    TmScrapeOptions o = {tm_thumb_kind_parse(tm_ini_get(&ini, "covers", "kind", "boxart")), retry, wait_s};
    int enabled = (int)tm_ini_get_long(&ini, "covers", "auto", 0);
    int clean = (int)tm_ini_get_long(&ini, "general", "clean_names", 1);
    tm_ini_free(&ini);
    if (autorun && !enabled)
        return 0;
    TmCatalog cat;
    if (load_catalog(&cat) != 0)
        return 1;
    TmLibrary lib;
    tm_library_init(&lib);
    if (tm_library_load(&lib, &cat, P.library) != 0 || tm_library_is_stale(&lib, &cat, P.sd)) {
        tm_library_free(&lib);
        tm_library_init(&lib);
        tm_library_scan(&lib, &cat, P.sd, clean);
        tm_library_save(&lib, &cat, P.library);
    }
    int rc = tm_scrape_run(&P, &cat, &lib, &o);
    tm_library_free(&lib);
    tm_catalog_free(&cat);
    return rc;
}

/* Online updates. check [--auto] [--wait N]: --auto only if [update]
 * auto_check = 1 (boot). install: the release found by the last check.
 * Exit status: check 0 = up to date, 2 = update available, 1 = error. */
static int cmd_update(int argc, char **argv)
{
    const char *sub = argc > 0 ? argv[0] : "status";
    TmIni ini;
    tm_settings_load(&ini, &P);
    int allow_pre = (int)tm_ini_get_long(&ini, "update", "prerelease", 1);
    int auto_check = (int)tm_ini_get_long(&ini, "update", "auto_check", 0);
    tm_ini_free(&ini);
    if (strcmp(sub, "check") == 0) {
        int autorun = 0, wait_s = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--auto") == 0)
                autorun = 1;
            else if (strcmp(argv[i], "--wait") == 0 && i + 1 < argc)
                wait_s = atoi(argv[++i]);
        }
        if ((autorun && !auto_check) || tm_update_running(&P))
            return 0;
        TmRelease r;
        int rc = tm_update_check(&P, allow_pre, wait_s, &r);
        if (rc > 0)
            printf("available=%s\n", r.version);
        else if (rc == 0)
            printf("uptodate\n");
        return rc < 0 ? 1 : rc > 0 ? 2 : 0;
    }
    if (strcmp(sub, "install") == 0) {
        TmRelease r;
        if (tm_update_info_read(&P, &r) != 0) {
            fprintf(stderr, "no update found; run: trimuxctl update check\n");
            return 1;
        }
        int rc = tm_update_install(&P, &r);
        if (rc == 1)
            fprintf(stderr, "an update is already running\n");
        return rc == 0 ? 0 : 1;
    }
    if (strcmp(sub, "rollback") == 0) {
        if (tm_update_rollback(&P) != 0) {
            fprintf(stderr, "no previous version to go back to\n");
            return 1;
        }
        printf("previous version restored; reboot to use it\n");
        return 0;
    }
    if (strcmp(sub, "status") == 0) {
        char cur[32];
        TmUpdateStatus st;
        tm_update_current(&P, cur, sizeof cur);
        printf("installed=%s\nbackup=%d\nrunning=%d\n", cur, tm_update_has_backup(&P), tm_update_running(&P));
        if (tm_update_status_read(&P, &st) == 0)
            printf("state=%s\nversion=%s\nerror=%s\npercent=%d\n", st.state, st.version, st.error, st.percent);
        return 0;
    }
    fprintf(stderr, "usage: trimuxctl update check [--auto] [--wait N]|install|rollback|status\n");
    return 1;
}

/* Runs the app the menu asked for (TrimUI app format), from its folder with
 * the firmware's shell, at the menu's power profile. */
static int cmd_app(void)
{
    char dir[TM_PATH_MAX];
    TmApp app;
    if (tm_app_request_read(&P, dir, sizeof dir) != 0)
        return 1;
    if (!tm_app_allowed(&P, dir, &app)) {
        LOGW("app: %s refused (not an app folder TriMux may start)", dir);
        return 1;
    }
    TmPowerCaps caps;
    if (tm_power_detect(&caps) == 0)
        tm_power_apply(&caps, tm_power_profile(TM_POWER_DEFAULT));
    char script[TM_PATH_MAX];
    tm_path_join(script, sizeof script, app.dir, app.launch);
    LOGI("app: starting %s (%s)", app.label, script);
    uint64_t t0 = tm_now_ms();
    pid_t pid = fork();
    if (pid < 0)
        return 1;
    if (pid == 0) {
        setenv("TRIMUX", "1", 1);
        setenv("TRIMUX_DEVICE", "brickpro", 1);
        if (chdir(app.dir) != 0)
            _exit(127);
        char *args[] = {"/bin/sh", script, NULL};
        execv("/bin/sh", args);
        _exit(127);
    }
    g_child = pid;
    signal(SIGTERM, forward_signal);
    signal(SIGINT, forward_signal);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    g_child = 0;
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    LOGI("app: %s exited with %d after %llu s", app.label, code, (unsigned long long)(tm_now_ms() - t0) / 1000);
    sync();
    return 0;
}

/* time tz: the TZ value for the zone chosen in TriMux (empty = firmware's).
 * time sync [--auto] [--wait N]: internet time (--auto: only if [time] ntp). */
static int cmd_time(int argc, char **argv)
{
    const char *sub = argc > 0 ? argv[0] : "tz";
    TmIni ini;
    tm_settings_load(&ini, &P);
    char zone[64];
    tm_strlcpy(zone, tm_ini_get(&ini, "time", "zone", ""), sizeof zone);
    int ntp = (int)tm_ini_get_long(&ini, "time", "ntp", 1);
    tm_ini_free(&ini);
    if (strcmp(sub, "tz") == 0) {
        char tz[600];
        if (zone[0] && tm_zone_tz(zone, tz, sizeof tz) == 0)
            printf("%s\n", tz);
        return 0;
    }
    if (strcmp(sub, "sync") == 0) {
        int autorun = 0, wait_s = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--auto") == 0)
                autorun = 1;
            else if (strcmp(argv[i], "--wait") == 0 && i + 1 < argc)
                wait_s = atoi(argv[++i]);
        }
        if (autorun && !ntp)
            return 0;
        for (int w = 0;; w++) {
            TmWifiStatus st;
            if (tm_wifi_running() && tm_wifi_status(&st) == 0 && strcmp(st.state, "COMPLETED") == 0 && st.ip[0])
                break;
            if (w >= wait_s)
                return 1;
            sleep(1);
        }
        return tm_clock_sync(30) == 0 ? 0 : 1;
    }
    fprintf(stderr, "usage: trimuxctl time tz|sync [--auto] [--wait N]\n");
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: trimuxctl power|leds|switch|net|scrape|update|app|time|sysinfo|device|scan|launch|boot|fat-grow|card-grow ...\n");
        return 1;
    }
    if (tm_paths_init(&P) != 0)
        return 1;
    if (strcmp(argv[1], "fat-grow") != 0)
        tm_paths_ensure(&P);
    open_log();
    const char *c = argv[1];
    if (strcmp(c, "power") == 0)
        return cmd_power(argc - 2, argv + 2);
    if (strcmp(c, "leds") == 0)
        return cmd_leds(argc - 2, argv + 2);
    if (strcmp(c, "switch") == 0)
        return cmd_switch();
    if (strcmp(c, "net") == 0)
        return cmd_net(argc - 2, argv + 2);
    if (strcmp(c, "scrape") == 0)
        return cmd_scrape(argc - 2, argv + 2);
    if (strcmp(c, "update") == 0)
        return cmd_update(argc - 2, argv + 2);
    if (strcmp(c, "app") == 0)
        return cmd_app();
    if (strcmp(c, "time") == 0)
        return cmd_time(argc - 2, argv + 2);
    if (strcmp(c, "sysinfo") == 0)
        return cmd_sysinfo();
    if (strcmp(c, "device") == 0) /* 0 only on a TrimUI Brick Pro firmware */
        return tm_sysinfo_is_brick_pro() ? 0 : 1;
    if (strcmp(c, "scan") == 0)
        return cmd_scan();
    if (strcmp(c, "launch") == 0)
        return cmd_launch();
    if (strcmp(c, "boot") == 0)
        return cmd_boot(argc - 2, argv + 2);
    if (strcmp(c, "fat-grow") == 0)
        return cmd_fatgrow(argc - 2, argv + 2);
    if (strcmp(c, "card-grow") == 0)
        return cmd_card_grow(argc - 2, argv + 2);
    fprintf(stderr, "unknown command %s\n", c);
    return 1;
}
