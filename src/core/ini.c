#define _GNU_SOURCE
#include "ini.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INI_MAX_LINE 1024
#define INI_MAX_FILE (2u << 20)

void tm_ini_init(TmIni *ini)
{
    memset(ini, 0, sizeof *ini);
}

void tm_ini_free(TmIni *ini)
{
    for (size_t i = 0; i < ini->count; i++) {
        free(ini->items[i].section);
        free(ini->items[i].key);
        free(ini->items[i].value);
    }
    free(ini->items);
    tm_ini_init(ini);
}

static long find(const TmIni *ini, const char *section, const char *key)
{
    for (size_t i = 0; i < ini->count; i++)
        if (strcmp(ini->items[i].section, section) == 0 && strcmp(ini->items[i].key, key) == 0)
            return (long)i;
    return -1;
}

static int append(TmIni *ini, const char *section, const char *key, const char *value)
{
    if (ini->count == ini->cap) {
        size_t ncap = ini->cap ? ini->cap * 2 : 32;
        TmIniEntry *n = realloc(ini->items, ncap * sizeof *n);
        if (!n)
            return -1;
        ini->items = n;
        ini->cap = ncap;
    }
    TmIniEntry *e = &ini->items[ini->count];
    e->section = strdup(section);
    e->key = strdup(key);
    e->value = strdup(value);
    if (!e->section || !e->key || !e->value) {
        free(e->section);
        free(e->key);
        free(e->value);
        return -1;
    }
    ini->count++;
    return 0;
}

int tm_ini_set(TmIni *ini, const char *section, const char *key, const char *value)
{
    if (!section || !key || !*key || !value)
        return -1;
    if (strchr(key, '\n') || strchr(value, '\n') || strchr(section, '\n') || strchr(key, '='))
        return -1;
    long i = find(ini, section, key);
    if (i < 0) {
        /* insert after the last entry of the same section to keep grouping */
        long last = -1;
        for (size_t j = 0; j < ini->count; j++)
            if (strcmp(ini->items[j].section, section) == 0)
                last = (long)j;
        if (append(ini, section, key, value) != 0)
            return -1;
        if (last >= 0 && (size_t)last + 1 < ini->count - 1) {
            TmIniEntry moved = ini->items[ini->count - 1];
            memmove(&ini->items[last + 2], &ini->items[last + 1],
                    (ini->count - 1 - (size_t)last - 1) * sizeof(TmIniEntry));
            ini->items[last + 1] = moved;
        }
        return 0;
    }
    char *nv = strdup(value);
    if (!nv)
        return -1;
    free(ini->items[i].value);
    ini->items[i].value = nv;
    return 0;
}

int tm_ini_set_long(TmIni *ini, const char *section, const char *key, long value)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%ld", value);
    return tm_ini_set(ini, section, key, buf);
}

int tm_ini_remove(TmIni *ini, const char *section, const char *key)
{
    long i = find(ini, section, key);
    if (i < 0)
        return -1;
    free(ini->items[i].section);
    free(ini->items[i].key);
    free(ini->items[i].value);
    memmove(&ini->items[i], &ini->items[i + 1], (ini->count - (size_t)i - 1) * sizeof(TmIniEntry));
    ini->count--;
    return 0;
}

const char *tm_ini_get(const TmIni *ini, const char *section, const char *key, const char *def)
{
    long i = find(ini, section, key);
    return i < 0 ? def : ini->items[i].value;
}

long tm_ini_get_long(const TmIni *ini, const char *section, const char *key, long def)
{
    const char *v = tm_ini_get(ini, section, key, NULL);
    if (!v || !*v)
        return def;
    char *end;
    errno = 0;
    long r = strtol(v, &end, 10);
    if (errno || *end)
        return def;
    return r;
}

int tm_ini_parse(TmIni *ini, const char *text, size_t len)
{
    char section[128] = "";
    int skipped = 0;
    size_t pos = 0;
    /* skip UTF-8 BOM written by some Windows editors */
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        pos = 3;
    while (pos < len) {
        size_t end = pos;
        while (end < len && text[end] != '\n')
            end++;
        size_t n = end - pos;
        char line[INI_MAX_LINE];
        if (n >= sizeof line) {
            skipped++;
            pos = end + 1;
            continue;
        }
        memcpy(line, text + pos, n);
        line[n] = '\0';
        pos = end + 1;
        char *s = tm_trim(line);
        if (!*s || *s == '#' || *s == ';')
            continue;
        if (*s == '[') {
            char *close = strchr(s, ']');
            if (!close) {
                skipped++;
                continue;
            }
            *close = '\0';
            tm_strlcpy(section, tm_trim(s + 1), sizeof section);
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            skipped++;
            continue;
        }
        *eq = '\0';
        char *k = tm_trim(s), *v = tm_trim(eq + 1);
        size_t vl = strlen(v);
        if (vl >= 2 && v[0] == '"' && v[vl - 1] == '"') {
            v[vl - 1] = '\0';
            v++;
        }
        if (!*k || tm_ini_set(ini, section, k, v) != 0)
            skipped++;
    }
    return skipped;
}

int tm_ini_load(TmIni *ini, const char *path)
{
    size_t len = 0;
    errno = 0;
    char *data = tm_read_file(path, INI_MAX_FILE, &len);
    if (!data)
        return errno == ENOENT ? 0 : -1;
    tm_ini_parse(ini, data, len);
    free(data);
    return 0;
}

int tm_ini_save(const TmIni *ini, const char *path)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return -1;
    const char *cur = NULL;
    for (size_t i = 0; i <= ini->count; i++) {
        char line[INI_MAX_LINE * 2];
        int n = 0;
        if (i == ini->count)
            break;
        const TmIniEntry *e = &ini->items[i];
        if (!cur || strcmp(cur, e->section) != 0) {
            if (*e->section)
                n = snprintf(line, sizeof line, "%s[%s]\n", cur ? "\n" : "", e->section);
            cur = e->section;
        }
        n += snprintf(line + n, sizeof line - (size_t)n, "%s = %s\n", e->key, e->value);
        if (n < 0 || (size_t)n >= sizeof line)
            continue;
        if (len + (size_t)n >= cap) {
            while (len + (size_t)n >= cap)
                cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                return -1;
            }
            buf = nb;
        }
        memcpy(buf + len, line, (size_t)n);
        len += (size_t)n;
    }
    int rc = tm_atomic_write(path, buf, len);
    free(buf);
    return rc;
}

long tm_ini_next_in_section(const TmIni *ini, const char *section, size_t start)
{
    for (size_t i = start; i < ini->count; i++)
        if (strcmp(ini->items[i].section, section) == 0)
            return (long)i;
    return -1;
}

size_t tm_ini_sections(const TmIni *ini, const char **out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < ini->count && n < max; i++) {
        int seen = 0;
        for (size_t j = 0; j < n; j++)
            if (strcmp(out[j], ini->items[i].section) == 0) {
                seen = 1;
                break;
            }
        if (!seen)
            out[n++] = ini->items[i].section;
    }
    return n;
}
