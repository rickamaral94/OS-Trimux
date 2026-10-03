#define _GNU_SOURCE
#include "launch.h"
#include "ini.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

int tm_launch_file(const TmPaths *p, char *out, size_t size)
{
    return tm_path_join(out, size, p->tmp, "launch.ini");
}

int tm_launch_write(const TmPaths *p, const char *system, const char *rom_rel, const char *emu)
{
    char path[TM_PATH_MAX];
    TmIni ini;
    tm_ini_init(&ini);
    int rc = tm_launch_file(p, path, sizeof path);
    rc |= tm_ini_set(&ini, "launch", "system", system);
    rc |= tm_ini_set(&ini, "launch", "rom", rom_rel);
    rc |= tm_ini_set(&ini, "launch", "emulator", emu);
    if (rc == 0)
        rc = tm_ini_save(&ini, path);
    tm_ini_free(&ini);
    return rc;
}

#define ERR(k)                                                                                          \
    do {                                                                                                \
        tm_strlcpy(err, k, errsz);                                                                      \
        tm_ini_free(&ini);                                                                              \
        return -1;                                                                                      \
    } while (0)

int tm_launch_read(const TmPaths *p, const TmCatalog *cat, TmLaunch *l, char *err, size_t errsz)
{
    char path[TM_PATH_MAX];
    TmIni ini;
    tm_ini_init(&ini);
    memset(l, 0, sizeof *l);
    if (tm_launch_file(p, path, sizeof path) != 0 || tm_ini_load(&ini, path) != 0)
        ERR("launch.err.request");
    l->system = tm_catalog_system(cat, tm_ini_get(&ini, "launch", "system", ""));
    if (!l->system)
        ERR("launch.err.system");
    l->emu = tm_catalog_emulator(cat, tm_ini_get(&ini, "launch", "emulator", ""));
    if (!l->emu || !tm_system_supports_emu(l->system, l->emu->id))
        ERR("launch.err.emulator");
    if (strcmp(l->emu->type, "retroarch") == 0 &&
        (tm_path_join(l->core_abs, sizeof l->core_abs, p->cores, l->emu->core) != 0 || !tm_file_exists(l->core_abs)))
        ERR("launch.err.core_missing");
    const char *rel = tm_ini_get(&ini, "launch", "rom", "");
    if (tm_strlcpy(l->rom_rel, rel, sizeof l->rom_rel) != 0 || !*rel || rel[0] == '/')
        ERR("launch.err.rom");
    if (tm_path_join(l->rom_abs, sizeof l->rom_abs, p->sd, rel) != 0 || !tm_path_is_safe_under(p->sd, l->rom_abs))
        ERR("launch.err.rom");
    if (!tm_file_exists(l->rom_abs))
        ERR("launch.err.rom_missing");
    if (!tm_system_has_ext(l->system, l->rom_abs))
        ERR("launch.err.rom");
    tm_ini_free(&ini);
    return 0;
}

static int cfg_line(char *buf, size_t size, size_t *len, const char *key, const char *val)
{
    if (strchr(val, '"') || strchr(val, '\n'))
        return -1;
    int n = snprintf(buf + *len, size - *len, "%s = \"%s\"\n", key, val);
    if (n < 0 || (size_t)n >= size - *len)
        return -1;
    *len += (size_t)n;
    return 0;
}

int tm_ra_language(const char *code)
{
    /* values of enum retro_language (libretro.h) */
    static const struct {
        const char *code;
        int ra;
    } map[] = {{"en_US", 0}, {"ja_JP", 1}, {"fr_FR", 2}, {"es_ES", 3}, {"de_DE", 4}, {"it_IT", 5},
               {"nl_NL", 6}, {"pt_BR", 7}, {"pt_PT", 8}, {"ru_RU", 9}, {"ko_KR", 10}};
    for (size_t i = 0; code && i < TM_ARRAY_LEN(map); i++)
        if (strcmp(code, map[i].code) == 0)
            return map[i].ra;
    return 0;
}

int tm_launch_write_ra_append(const TmPaths *p, const TmLaunch *l, const char *extra, char *out_path, size_t size)
{
    char saves[TM_PATH_MAX], states[TM_PATH_MAX], bios[TM_PATH_MAX], shots[TM_PATH_MAX], rel[TM_PATH_MAX];
    char buf[4096];
    size_t len = 0;
    int rc = 0;
    rc |= tm_snprintf(rel, sizeof rel, "Saves/%s", l->system->id);
    rc |= tm_path_join(saves, sizeof saves, p->sd, rel);
    rc |= tm_snprintf(rel, sizeof rel, "States/%s", l->system->id);
    rc |= tm_path_join(states, sizeof states, p->sd, rel);
    rc |= tm_path_join(bios, sizeof bios, p->sd, "Bios");
    rc |= tm_path_join(shots, sizeof shots, p->sd, "Screenshots");
    if (rc)
        return -1;
    /* folders are created only if missing; existing saves are untouched */
    tm_mkdir_p(saves);
    tm_mkdir_p(states);
    tm_mkdir_p(shots);
    rc |= cfg_line(buf, sizeof buf, &len, "savefile_directory", saves);
    rc |= cfg_line(buf, sizeof buf, &len, "savestate_directory", states);
    rc |= cfg_line(buf, sizeof buf, &len, "system_directory", bios);
    rc |= cfg_line(buf, sizeof buf, &len, "screenshot_directory", shots);
    rc |= cfg_line(buf, sizeof buf, &len, "libretro_directory", p->cores);
    if (extra && *extra) {
        int n = snprintf(buf + len, sizeof buf - len, "%s\n", extra);
        if (n < 0 || (size_t)n >= sizeof buf - len)
            return -1;
        len += (size_t)n;
    }
    if (rc || tm_path_join(out_path, size, p->tmp, "ra-append.cfg") != 0)
        return -1;
    return tm_atomic_write(out_path, buf, len);
}

void tm_thermal_init(TmThermalGuard *g, long hot_mc, long cool_mc, int samples)
{
    memset(g, 0, sizeof *g);
    g->hot_mc = hot_mc;
    g->cool_mc = cool_mc < hot_mc ? cool_mc : hot_mc - 5000;
    g->samples = samples > 0 ? samples : 1;
}

int tm_thermal_step(TmThermalGuard *g, long t)
{
    if (t < 0) {
        g->cool_count = 0;
        return TM_THERMAL_NONE;
    }
    if (!g->throttled) {
        g->hot_count = t >= g->hot_mc ? g->hot_count + 1 : 0;
        if (g->hot_count >= g->samples) {
            g->throttled = 1;
            g->hot_count = g->cool_count = 0;
            return TM_THERMAL_THROTTLE;
        }
    } else {
        g->cool_count = t <= g->cool_mc ? g->cool_count + 1 : 0;
        if (g->cool_count >= g->samples) {
            g->throttled = 0;
            g->hot_count = g->cool_count = 0;
            return TM_THERMAL_RESTORE;
        }
    }
    return TM_THERMAL_NONE;
}
