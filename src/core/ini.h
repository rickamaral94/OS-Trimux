/* Minimal INI store: [section] key = value, '#' or ';' comments.
 * Keys before any section belong to section "". Order is preserved so files
 * stay readable after a save. Malformed lines are skipped, never fatal. */
#ifndef TRIMUX_INI_H
#define TRIMUX_INI_H

#include <stddef.h>

typedef struct {
    char *section;
    char *key;
    char *value;
} TmIniEntry;

typedef struct {
    TmIniEntry *items;
    size_t count, cap;
} TmIni;

void tm_ini_init(TmIni *ini);
void tm_ini_free(TmIni *ini);
/* Parses text (not NUL-required beyond len). Returns number of skipped lines. */
int tm_ini_parse(TmIni *ini, const char *text, size_t len);
/* Loads a file. Missing file = empty store, returns 0. Returns -1 on I/O error. */
int tm_ini_load(TmIni *ini, const char *path);
/* Atomic save (tmp + fsync + rename). */
int tm_ini_save(const TmIni *ini, const char *path);

const char *tm_ini_get(const TmIni *ini, const char *section, const char *key, const char *def);
long tm_ini_get_long(const TmIni *ini, const char *section, const char *key, long def);
int tm_ini_set(TmIni *ini, const char *section, const char *key, const char *value);
int tm_ini_set_long(TmIni *ini, const char *section, const char *key, long value);
int tm_ini_remove(TmIni *ini, const char *section, const char *key);

/* Iterates entries of one section: returns index of next match >= start, or -1. */
long tm_ini_next_in_section(const TmIni *ini, const char *section, size_t start);
/* Lists distinct section names in order; caller must not free the strings. */
size_t tm_ini_sections(const TmIni *ini, const char **out, size_t max);

#endif
