#define _GNU_SOURCE
#include "paths.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tm_paths_init(TmPaths *p)
{
    memset(p, 0, sizeof *p);
    const char *sd = getenv("TRIMUX_SDCARD");
    const char *tmp = getenv("TRIMUX_TMP");
    int rc = 0;
    rc |= tm_strlcpy(p->sd, sd && *sd ? sd : "/mnt/SDCARD", sizeof p->sd);
    rc |= tm_strlcpy(p->tmp, tmp && *tmp ? tmp : "/tmp/trimux", sizeof p->tmp);
    rc |= tm_path_join(p->sys, sizeof p->sys, p->sd, "TriMux");
    rc |= tm_path_join(p->data, sizeof p->data, p->sd, "TriMuxData");
    rc |= tm_path_join(p->share, sizeof p->share, p->sys, "share");
    rc |= tm_path_join(p->retroarch, sizeof p->retroarch, p->sys, "retroarch");
    rc |= tm_path_join(p->cores, sizeof p->cores, p->retroarch, "cores");
    rc |= tm_path_join(p->config, sizeof p->config, p->data, "config/trimux.ini");
    rc |= tm_path_join(p->overrides, sizeof p->overrides, p->data, "config/overrides.ini");
    rc |= tm_path_join(p->favorites, sizeof p->favorites, p->data, "config/favorites.txt");
    rc |= tm_path_join(p->recents, sizeof p->recents, p->data, "config/recent.txt");
    rc |= tm_path_join(p->library, sizeof p->library, p->data, "cache/library.tsv");
    rc |= tm_path_join(p->logdir, sizeof p->logdir, p->data, "logs");
    rc |= tm_path_join(p->state, sizeof p->state, p->data, "state");
    rc |= tm_path_join(p->ra_home, sizeof p->ra_home, p->data, "retroarch");
    return rc ? -1 : 0;
}

int tm_paths_ensure(const TmPaths *p)
{
    const char *subs[] = {"config", "cache", "logs", "state", "retroarch"};
    int rc = 0;
    for (size_t i = 0; i < TM_ARRAY_LEN(subs); i++) {
        char d[TM_PATH_MAX];
        if (tm_path_join(d, sizeof d, p->data, subs[i]) != 0 || tm_mkdir_p(d) != 0)
            rc = -1;
    }
    if (tm_mkdir_p(p->tmp) != 0)
        rc = -1;
    return rc;
}

static void def(TmIni *ini, const char *sec, const char *key, const char *val)
{
    if (!tm_ini_get(ini, sec, key, NULL))
        tm_ini_set(ini, sec, key, val);
}

int tm_settings_load(TmIni *ini, const TmPaths *p)
{
    tm_ini_init(ini);
    int created = !tm_file_exists(p->config);
    if (!created && tm_ini_load(ini, p->config) != 0) {
        LOGW("settings: unreadable %s, using defaults (file kept)", p->config);
        tm_ini_free(ini);
        tm_ini_init(ini);
    }
    long ver = tm_ini_get_long(ini, "meta", "version", created ? TM_SETTINGS_VERSION : 0);
    if (!created && ver < TM_SETTINGS_VERSION) {
        char bak[TM_PATH_MAX];
        if (tm_snprintf(bak, sizeof bak, "%s.bak-v%ld", p->config, ver) == 0 && tm_copy_file(p->config, bak) == 0)
            LOGI("settings: migrating v%ld -> v%d (backup %s)", ver, TM_SETTINGS_VERSION, bak);
    }
    tm_ini_set_long(ini, "meta", "version", TM_SETTINGS_VERSION);
    def(ini, "general", "language", "pt_BR");
    def(ini, "general", "theme", "dark");
    def(ini, "general", "wizard_done", "0");
    def(ini, "general", "clean_names", "1");
    def(ini, "general", "show_empty", "0");
    def(ini, "general", "idle_poweroff_min", "0");
    def(ini, "input", "swap_ab", "0");
    def(ini, "power", "profile", "auto");
    def(ini, "power", "thermal_guard", "1");
    return created;
}

int tm_settings_save(const TmIni *ini, const TmPaths *p)
{
    return tm_ini_save(ini, p->config);
}
