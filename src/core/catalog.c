#define _GNU_SOURCE
#include "catalog.h"
#include "ini.h"
#include "log.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* Splits "a, b, c" into fixed-size slots. Returns the number stored. */
static int split_list(const char *src, char *out, size_t slot, int max)
{
    int n = 0;
    char buf[1024];
    tm_strlcpy(buf, src ? src : "", sizeof buf);
    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok && n < max; tok = strtok_r(NULL, ",", &save)) {
        char *t = tm_trim(tok);
        if (!*t)
            continue;
        if (tm_strlcpy(out + (size_t)n * slot, t, slot) == 0)
            n++;
    }
    return n;
}

static int id_is_valid(const char *id, size_t max)
{
    size_t n = strlen(id);
    if (n == 0 || n >= max)
        return 0;
    for (const char *p = id; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-'))
            return 0;
    return 1;
}

int tm_catalog_load(TmCatalog *cat, const char *systems_ini, const char *emulators_ini)
{
    memset(cat, 0, sizeof *cat);
    TmIni s, e;
    tm_ini_init(&s);
    tm_ini_init(&e);
    if (tm_ini_load(&s, systems_ini) != 0 || tm_ini_load(&e, emulators_ini) != 0) {
        tm_ini_free(&s);
        tm_ini_free(&e);
        return -1;
    }
    const char *names[128];
    size_t ns = tm_ini_sections(&s, names, TM_ARRAY_LEN(names));
    cat->systems = calloc(ns ? ns : 1, sizeof(TmSystem));
    for (size_t i = 0; cat->systems && i < ns; i++) {
        const char *id = names[i];
        if (!id_is_valid(id, sizeof cat->systems[0].id))
            continue;
        TmSystem *sys = &cat->systems[cat->nsystems];
        tm_strlcpy(sys->id, id, sizeof sys->id);
        tm_strlcpy(sys->name, tm_ini_get(&s, id, "name", id), sizeof sys->name);
        tm_strlcpy(sys->short_name, tm_ini_get(&s, id, "short", id), sizeof sys->short_name);
        sys->nfolders = split_list(tm_ini_get(&s, id, "folders", id), &sys->folders[0][0],
                                   sizeof sys->folders[0], TM_MAX_FOLDERS);
        sys->nexts = split_list(tm_ini_get(&s, id, "extensions", ""), &sys->exts[0][0],
                                sizeof sys->exts[0], TM_MAX_EXTS);
        sys->nbios = split_list(tm_ini_get(&s, id, "bios", ""), &sys->bios[0][0], sizeof sys->bios[0],
                                TM_MAX_BIOS);
        sys->nemus = split_list(tm_ini_get(&s, id, "emulators", ""), &sys->emulators[0][0],
                                sizeof sys->emulators[0], TM_MAX_SYS_EMUS);
        sys->bios_required = (int)tm_ini_get_long(&s, id, "bios_required", 0);
        sys->experimental = (int)tm_ini_get_long(&s, id, "experimental", 0);
        sys->color = (unsigned)strtoul(tm_ini_get(&s, id, "color", "607080"), NULL, 16) & 0xFFFFFFu;
        tm_strlcpy(sys->note_key, tm_ini_get(&s, id, "note", ""), sizeof sys->note_key);
        /* folder names become path components: reject unsafe ones */
        int k = 0;
        for (int f = 0; f < sys->nfolders; f++)
            if (tm_name_is_safe(sys->folders[f]))
                memmove(sys->folders[k++], sys->folders[f], sizeof sys->folders[0]);
        sys->nfolders = k;
        if (sys->nexts > 0)
            cat->nsystems++;
        else
            LOGW("catalog: system %s has no extensions, ignored", id);
    }

    ns = tm_ini_sections(&e, names, TM_ARRAY_LEN(names));
    cat->emus = calloc(ns ? ns : 1, sizeof(TmEmulator));
    for (size_t i = 0; cat->emus && i < ns; i++) {
        const char *id = names[i];
        if (!id_is_valid(id, sizeof cat->emus[0].id))
            continue;
        TmEmulator *em = &cat->emus[cat->nemus];
        tm_strlcpy(em->id, id, sizeof em->id);
        tm_strlcpy(em->name, tm_ini_get(&e, id, "name", id), sizeof em->name);
        tm_strlcpy(em->type, tm_ini_get(&e, id, "type", "retroarch"), sizeof em->type);
        tm_strlcpy(em->core, tm_ini_get(&e, id, "core", ""), sizeof em->core);
        tm_strlcpy(em->config_name, tm_ini_get(&e, id, "config_name", em->name), sizeof em->config_name);
        tm_strlcpy(em->profile, tm_ini_get(&e, id, "profile", "balanced"), sizeof em->profile);
        tm_strlcpy(em->note_key, tm_ini_get(&e, id, "note", ""), sizeof em->note_key);
        em->experimental = (int)tm_ini_get_long(&e, id, "experimental", 0);
        if (strcmp(em->type, "retroarch") != 0 || !tm_name_is_safe(em->core) ||
            !tm_name_is_safe(em->config_name)) {
            LOGW("catalog: emulator %s rejected (type/core/config_name)", id);
            continue;
        }
        cat->nemus++;
    }
    tm_ini_free(&s);
    tm_ini_free(&e);
    if (!cat->systems || !cat->emus) {
        tm_catalog_free(cat);
        return -1;
    }
    return 0;
}

void tm_catalog_free(TmCatalog *cat)
{
    free(cat->systems);
    free(cat->emus);
    memset(cat, 0, sizeof *cat);
}

long tm_catalog_system_index(const TmCatalog *cat, const char *id)
{
    for (size_t i = 0; id && i < cat->nsystems; i++)
        if (strcmp(cat->systems[i].id, id) == 0)
            return (long)i;
    return -1;
}

const TmSystem *tm_catalog_system(const TmCatalog *cat, const char *id)
{
    long i = tm_catalog_system_index(cat, id);
    return i < 0 ? NULL : &cat->systems[i];
}

const TmEmulator *tm_catalog_emulator(const TmCatalog *cat, const char *id)
{
    for (size_t i = 0; id && i < cat->nemus; i++)
        if (strcmp(cat->emus[i].id, id) == 0)
            return &cat->emus[i];
    return NULL;
}

int tm_system_has_ext(const TmSystem *sys, const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
        return 0;
    for (int i = 0; i < sys->nexts; i++)
        if (tm_strcasecmp_ascii(dot + 1, sys->exts[i]) == 0)
            return 1;
    return 0;
}

int tm_system_supports_emu(const TmSystem *sys, const char *emu_id)
{
    for (int i = 0; emu_id && i < sys->nemus; i++)
        if (strcmp(sys->emulators[i], emu_id) == 0)
            return 1;
    return 0;
}

int tm_emulator_available(const TmEmulator *emu, const char *cores_dir)
{
    char path[TM_PATH_MAX];
    if (!emu || tm_path_join(path, sizeof path, cores_dir, emu->core) != 0)
        return 0;
    return tm_file_exists(path);
}

static const TmEmulator *usable(const TmCatalog *cat, const TmSystem *sys, const char *id,
                                const char *cores_dir)
{
    if (!id || !*id || !tm_system_supports_emu(sys, id))
        return NULL;
    const TmEmulator *em = tm_catalog_emulator(cat, id);
    return (em && tm_emulator_available(em, cores_dir)) ? em : NULL;
}

const TmEmulator *tm_catalog_resolve(const TmCatalog *cat, const TmSystem *sys,
                                     const char *game_override, const char *platform_pref,
                                     const char *cores_dir)
{
    if (!sys)
        return NULL;
    const TmEmulator *em = usable(cat, sys, game_override, cores_dir);
    if (!em)
        em = usable(cat, sys, platform_pref, cores_dir);
    for (int i = 0; !em && i < sys->nemus; i++)
        em = usable(cat, sys, sys->emulators[i], cores_dir);
    return em;
}
