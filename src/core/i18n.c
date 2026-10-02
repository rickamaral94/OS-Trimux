#define _GNU_SOURCE
#include "i18n.h"
#include "ini.h"
#include "util.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

static TmIni g_lang, g_fallback;
static char g_code[16] = TM_DEFAULT_LANG;
static int g_init;

static int code_is_valid(const char *code)
{
    size_t n = strlen(code);
    if (n < 2 || n >= 16)
        return 0;
    for (const char *p = code; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_'))
            return 0;
    return 1;
}

static int load_one(TmIni *ini, const char *dir, const char *code)
{
    char path[TM_PATH_MAX], name[32];
    if (!code_is_valid(code) || tm_snprintf(name, sizeof name, "%s.lang", code) != 0 ||
        tm_path_join(path, sizeof path, dir, name) != 0)
        return -1;
    if (!tm_file_exists(path))
        return -1;
    return tm_ini_load(ini, path);
}

int tm_i18n_load(const char *dir, const char *code)
{
    if (g_init)
        tm_i18n_free();
    tm_ini_init(&g_lang);
    tm_ini_init(&g_fallback);
    g_init = 1;
    int fb = load_one(&g_fallback, dir, TM_DEFAULT_LANG);
    if (code && strcmp(code, TM_DEFAULT_LANG) != 0 && load_one(&g_lang, dir, code) == 0) {
        tm_strlcpy(g_code, code, sizeof g_code);
        return 0;
    }
    tm_strlcpy(g_code, TM_DEFAULT_LANG, sizeof g_code);
    return fb;
}

const char *tm_tr(const char *key)
{
    if (!g_init)
        return key;
    const char *v = tm_ini_get(&g_lang, "", key, NULL);
    if (!v)
        v = tm_ini_get(&g_fallback, "", key, NULL);
    return v ? v : key;
}

const char *tm_i18n_current(void)
{
    return g_code;
}

size_t tm_i18n_available(const char *dir, char codes[][16], size_t max)
{
    DIR *d = opendir(dir);
    size_t n = 0;
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        size_t l = strlen(e->d_name);
        if (l > 5 && l < 21 && strcmp(e->d_name + l - 5, ".lang") == 0) {
            char code[16];
            memcpy(code, e->d_name, l - 5);
            code[l - 5] = '\0';
            if (code_is_valid(code))
                tm_strlcpy(codes[n++], code, 16);
        }
    }
    closedir(d);
    /* stable order: default first, then alphabetical */
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++) {
            int swap = strcmp(codes[j], TM_DEFAULT_LANG) == 0 ||
                       (strcmp(codes[i], TM_DEFAULT_LANG) != 0 && strcmp(codes[j], codes[i]) < 0);
            if (swap) {
                char t[16];
                memcpy(t, codes[i], 16);
                memcpy(codes[i], codes[j], 16);
                memcpy(codes[j], t, 16);
            }
        }
    return n;
}

int tm_i18n_language_name(const char *dir, const char *code, char *out, size_t size)
{
    TmIni ini;
    tm_ini_init(&ini);
    int rc = load_one(&ini, dir, code);
    tm_strlcpy(out, rc == 0 ? tm_ini_get(&ini, "", "lang.name", code) : code, size);
    tm_ini_free(&ini);
    return rc;
}

void tm_i18n_free(void)
{
    if (!g_init)
        return;
    tm_ini_free(&g_lang);
    tm_ini_free(&g_fallback);
    g_init = 0;
}
