#define _GNU_SOURCE
#include "library.h"
#include "log.h"
#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LIB_MAGIC "#TRIMUX-LIB 1"
#define SCAN_MAX_DEPTH 3
#define M3U_MAX_HIDE 64

/* Parent folders searched for system folders. On FAT these names are
 * case-insensitive; duplicates are removed by inode. */
static const char *const k_parents[] = {"Roms", "roms", "ROMS"};

void tm_library_init(TmLibrary *lib)
{
    memset(lib, 0, sizeof *lib);
}

void tm_library_free(TmLibrary *lib)
{
    for (size_t i = 0; i < lib->count; i++) {
        free(lib->games[i].relpath);
        free(lib->games[i].name);
        free(lib->games[i].key);
    }
    free(lib->games);
    tm_library_init(lib);
}

static int is_disc_tag(const char *tag)
{
    char low[64];
    tm_fold_key(tag, low, sizeof low);
    return strstr(low, "disc") || strstr(low, "disco") || strstr(low, "disk") || strstr(low, "cd ");
}

void tm_clean_name(const char *filename, int clean, char *out, size_t size)
{
    char buf[512];
    const char *base = strrchr(filename, '/');
    tm_strlcpy(buf, base ? base + 1 : filename, sizeof buf);
    char *dot = strrchr(buf, '.');
    if (dot && dot != buf)
        *dot = '\0';
    if (clean) {
        char keep[512] = "";
        for (;;) {
            char *s = tm_trim(buf);
            if (s != buf)
                memmove(buf, s, strlen(s) + 1);
            size_t n = strlen(buf);
            if (n == 0)
                break;
            char close = buf[n - 1], open = close == ')' ? '(' : close == ']' ? '[' : 0;
            if (!open)
                break;
            char *start = strrchr(buf, open);
            if (!start || start == buf)
                break;
            if (open == '(' && is_disc_tag(start)) {
                char tmp[512];
                snprintf(tmp, sizeof tmp, " %s%s", start, keep);
                tm_strlcpy(keep, tmp, sizeof keep);
            }
            *start = '\0';
        }
        if (!*tm_trim(buf)) {
            /* everything was tags: fall back to the raw name */
            tm_strlcpy(buf, base ? base + 1 : filename, sizeof buf);
            dot = strrchr(buf, '.');
            if (dot && dot != buf)
                *dot = '\0';
        } else if (*keep) {
            char joined[512];
            snprintf(joined, sizeof joined, "%s%s", buf, keep);
            tm_strlcpy(buf, joined, sizeof buf);
        }
    }
    tm_strlcpy(out, tm_trim(buf), size);
}

static int add_game(TmLibrary *lib, int system, const char *relpath, const char *name)
{
    if (lib->count >= TM_LIB_MAX_GAMES) {
        lib->truncated = 1;
        return -1;
    }
    if (lib->count == lib->cap) {
        size_t ncap = lib->cap ? lib->cap * 2 : 256;
        TmGame *n = realloc(lib->games, ncap * sizeof *n);
        if (!n)
            return -1;
        lib->games = n;
        lib->cap = ncap;
    }
    char key[512];
    tm_fold_key(name, key, sizeof key);
    TmGame *g = &lib->games[lib->count];
    g->system = system;
    g->relpath = strdup(relpath);
    g->name = strdup(name);
    g->key = strdup(key);
    if (!g->relpath || !g->name || !g->key) {
        free(g->relpath);
        free(g->name);
        free(g->key);
        return -1;
    }
    lib->count++;
    return 0;
}

/* Collects file names listed inside .m3u playlists of a directory so the
 * individual discs are hidden behind the playlist entry. */
static size_t collect_m3u_hidden(const char *abs_dir, char hidden[][256], size_t max)
{
    DIR *d = opendir(abs_dir);
    size_t n = 0;
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.' || !tm_ends_with_ci(e->d_name, ".m3u"))
            continue;
        char p[TM_PATH_MAX];
        if (tm_path_join(p, sizeof p, abs_dir, e->d_name) != 0)
            continue;
        char *txt = tm_read_file(p, 16384, NULL);
        if (!txt)
            continue;
        char *save = NULL;
        for (char *line = strtok_r(txt, "\r\n", &save); line && n < max; line = strtok_r(NULL, "\r\n", &save)) {
            char *t = tm_trim(line);
            if (!*t || *t == '#' || strchr(t, '/'))
                continue;
            tm_strlcpy(hidden[n++], t, 256);
        }
        free(txt);
    }
    closedir(d);
    return n;
}

static int skip_dir_name(const char *name)
{
    static const char *const skip[] = {"Imgs", "Images", "media", "Saves", "States", "Thumbs",
                                       "boxart", "snap", "manuals", "System Volume Information"};
    if (name[0] == '.' || name[0] == '_')
        return 1;
    for (size_t i = 0; i < TM_ARRAY_LEN(skip); i++)
        if (tm_strcasecmp_ascii(name, skip[i]) == 0)
            return 1;
    return 0;
}

static void scan_dir(TmLibrary *lib, const TmSystem *sys, int sys_index, const char *sd_root,
                     const char *rel_dir, int depth, int clean)
{
    char abs_dir[TM_PATH_MAX];
    if (tm_path_join(abs_dir, sizeof abs_dir, sd_root, rel_dir) != 0)
        return;
    DIR *d = opendir(abs_dir);
    if (!d)
        return;
    static char hidden[M3U_MAX_HIDE][256];
    size_t nhidden = collect_m3u_hidden(abs_dir, hidden, M3U_MAX_HIDE);
    struct dirent *e;
    while ((e = readdir(d)) && !lib->truncated) {
        const char *nm = e->d_name;
        if (nm[0] == '.' || !tm_name_is_safe(nm))
            continue;
        char rel[TM_PATH_MAX], abs[TM_PATH_MAX];
        if (tm_path_join(rel, sizeof rel, rel_dir, nm) != 0 || tm_path_join(abs, sizeof abs, sd_root, rel) != 0)
            continue;
        struct stat st;
        if (stat(abs, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < sys->max_depth && depth < SCAN_MAX_DEPTH && !skip_dir_name(nm)) {
                scan_dir(lib, sys, sys_index, sd_root, rel, depth + 1, clean);
                /* the recursive call reused the static buffer */
                nhidden = collect_m3u_hidden(abs_dir, hidden, M3U_MAX_HIDE);
            }
            continue;
        }
        if (!S_ISREG(st.st_mode) || !tm_system_has_ext(sys, nm))
            continue;
        int is_hidden = 0;
        for (size_t h = 0; h < nhidden && !is_hidden; h++)
            is_hidden = tm_strcasecmp_ascii(hidden[h], nm) == 0;
        if (is_hidden)
            continue;
        char name[256];
        tm_clean_name(nm, clean, name, sizeof name);
        add_game(lib, sys_index, rel, name);
    }
    closedir(d);
}

typedef struct {
    dev_t dev;
    ino_t ino;
} DirId;

/* Finds system folders by listing each parent once and matching names
 * case-insensitively, deduplicating by inode. */
static size_t find_system_dirs(const TmSystem *sys, const char *sd_root, char out[][256], size_t max,
                               DirId *seen, size_t *nseen, size_t seen_max)
{
    size_t n = 0;
    for (size_t p = 0; p < TM_ARRAY_LEN(k_parents) && n < max; p++) {
        char parent[TM_PATH_MAX];
        if (tm_path_join(parent, sizeof parent, sd_root, k_parents[p]) != 0)
            continue;
        DIR *d = opendir(parent);
        if (!d)
            continue;
        struct dirent *e;
        while ((e = readdir(d)) && n < max) {
            if (e->d_name[0] == '.')
                continue;
            int match = 0;
            for (int f = 0; f < sys->nfolders && !match; f++) {
                char tagged[72];
                /* exact name, or a "Long Name (TAG)" folder as used by MinUI/NextUI cards */
                snprintf(tagged, sizeof tagged, "(%s)", sys->folders[f]);
                match = tm_strcasecmp_ascii(e->d_name, sys->folders[f]) == 0 ||
                        (strlen(e->d_name) > strlen(tagged) && tm_ends_with_ci(e->d_name, tagged));
            }
            if (!match)
                continue;
            char rel[TM_PATH_MAX], abs[TM_PATH_MAX];
            struct stat st;
            if (tm_path_join(rel, sizeof rel, k_parents[p], e->d_name) != 0 ||
                tm_path_join(abs, sizeof abs, sd_root, rel) != 0 || stat(abs, &st) != 0 ||
                !S_ISDIR(st.st_mode) || strlen(rel) >= 256)
                continue;
            int dup = 0;
            for (size_t s = 0; s < *nseen && !dup; s++)
                dup = seen[s].dev == st.st_dev && seen[s].ino == st.st_ino;
            if (dup)
                continue;
            if (*nseen < seen_max)
                seen[(*nseen)++] = (DirId){st.st_dev, st.st_ino};
            tm_strlcpy(out[n++], rel, 256);
        }
        closedir(d);
    }
    return n;
}

size_t tm_library_system_dirs(const TmSystem *sys, const char *sd_root, char out[][256], size_t max)
{
    DirId seen[64];
    size_t nseen = 0;
    return find_system_dirs(sys, sd_root, out, max, seen, &nseen, TM_ARRAY_LEN(seen));
}

static int cmp_game(const void *a, const void *b)
{
    const TmGame *x = a, *y = b;
    int c = strcmp(x->key, y->key);
    if (c)
        return c;
    if (x->system != y->system)
        return x->system - y->system;
    return strcmp(x->relpath, y->relpath);
}

void tm_library_sort(TmLibrary *lib)
{
    if (lib->count > 1)
        qsort(lib->games, lib->count, sizeof(TmGame), cmp_game);
}

int tm_library_scan(TmLibrary *lib, const TmCatalog *cat, const char *sd_root, int clean_names)
{
    tm_library_free(lib);
    static DirId seen[TM_LIB_MAX_DIRS];
    size_t nseen = 0;
    for (size_t s = 0; s < cat->nsystems; s++) {
        char dirs[16][256];
        size_t nd = find_system_dirs(&cat->systems[s], sd_root, dirs, 16, seen, &nseen, TM_LIB_MAX_DIRS);
        for (size_t i = 0; i < nd; i++) {
            if (lib->ndirs < TM_LIB_MAX_DIRS) {
                char abs[TM_PATH_MAX];
                struct stat st;
                TmLibDir *ld = &lib->dirs[lib->ndirs++];
                tm_strlcpy(ld->relpath, dirs[i], sizeof ld->relpath);
                ld->mtime = (tm_path_join(abs, sizeof abs, sd_root, dirs[i]) == 0 && stat(abs, &st) == 0)
                                ? (long)st.st_mtime
                                : 0;
            }
            scan_dir(lib, &cat->systems[s], (int)s, sd_root, dirs[i], 1, clean_names);
        }
    }
    tm_library_sort(lib);
    LOGI("library: %zu games in %zu folders%s", lib->count, lib->ndirs, lib->truncated ? " (truncated)" : "");
    return 0;
}

static int field_ok(const char *s)
{
    return s && !strchr(s, '\t') && !strchr(s, '\n') && !strchr(s, '\r');
}

int tm_library_save(const TmLibrary *lib, const TmCatalog *cat, const char *path)
{
    size_t cap = 64 * 1024, len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return -1;
    len += (size_t)snprintf(buf, cap, "%s\n", LIB_MAGIC);
    for (size_t i = 0; i <= lib->ndirs + lib->count; i++) {
        char line[1024];
        int n;
        if (i == lib->ndirs + lib->count)
            break;
        if (i < lib->ndirs) {
            n = snprintf(line, sizeof line, "#dir\t%s\t%ld\n", lib->dirs[i].relpath, lib->dirs[i].mtime);
        } else {
            const TmGame *g = &lib->games[i - lib->ndirs];
            if (!field_ok(g->relpath) || !field_ok(g->name))
                continue;
            n = snprintf(line, sizeof line, "%s\t%s\t%s\n", cat->systems[g->system].id, g->relpath, g->name);
        }
        if (n < 0 || (size_t)n >= sizeof line)
            continue;
        if (len + (size_t)n + 1 > cap) {
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

int tm_library_load(TmLibrary *lib, const TmCatalog *cat, const char *path)
{
    tm_library_free(lib);
    size_t len = 0;
    char *data = tm_read_file(path, 32u << 20, &len);
    if (!data)
        return -1;
    if (strncmp(data, LIB_MAGIC "\n", strlen(LIB_MAGIC) + 1) != 0) {
        free(data);
        return -1;
    }
    char *save = NULL;
    for (char *line = strtok_r(data + strlen(LIB_MAGIC) + 1, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *f1 = line, *f2 = strchr(f1, '\t');
        if (!f2)
            continue;
        *f2++ = '\0';
        char *f3 = strchr(f2, '\t');
        if (!f3)
            continue;
        *f3++ = '\0';
        if (strcmp(f1, "#dir") == 0) {
            if (lib->ndirs < TM_LIB_MAX_DIRS) {
                tm_strlcpy(lib->dirs[lib->ndirs].relpath, f2, sizeof lib->dirs[0].relpath);
                lib->dirs[lib->ndirs++].mtime = strtol(f3, NULL, 10);
            }
            continue;
        }
        long s = tm_catalog_system_index(cat, f1);
        if (s < 0 || !*f2 || strstr(f2, "..") == f2)
            continue;
        add_game(lib, (int)s, f2, f3);
    }
    free(data);
    tm_library_sort(lib);
    return 0;
}

int tm_library_is_stale(const TmLibrary *lib, const TmCatalog *cat, const char *sd_root)
{
    for (size_t i = 0; i < lib->ndirs; i++) {
        char abs[TM_PATH_MAX];
        struct stat st;
        if (tm_path_join(abs, sizeof abs, sd_root, lib->dirs[i].relpath) != 0 || stat(abs, &st) != 0)
            return 1; /* folder removed or card swapped */
        if ((long)st.st_mtime != lib->dirs[i].mtime)
            return 1;
    }
    size_t known = 0;
    for (size_t s = 0; s < cat->nsystems; s++) {
        char dirs[16][256];
        known += tm_library_system_dirs(&cat->systems[s], sd_root, dirs, 16);
    }
    return known != lib->ndirs;
}

int tm_library_create_default_dirs(const TmCatalog *cat, const char *sd_root)
{
    int created = 0;
    for (size_t s = 0; s < cat->nsystems; s++) {
        const TmSystem *sys = &cat->systems[s];
        char dirs[4][256];
        if (sys->nfolders == 0 || tm_library_system_dirs(sys, sd_root, dirs, 4) > 0)
            continue;
        char rel[TM_PATH_MAX], abs[TM_PATH_MAX];
        if (tm_path_join(rel, sizeof rel, "Roms", sys->folders[0]) != 0 ||
            tm_path_join(abs, sizeof abs, sd_root, rel) != 0)
            continue;
        if (tm_mkdir_p(abs) == 0)
            created++;
    }
    return created;
}

size_t tm_library_count_system(const TmLibrary *lib, int system)
{
    size_t n = 0;
    for (size_t i = 0; i < lib->count; i++)
        n += lib->games[i].system == system;
    return n;
}

long tm_library_find(const TmLibrary *lib, const char *relpath)
{
    for (size_t i = 0; relpath && i < lib->count; i++)
        if (strcmp(lib->games[i].relpath, relpath) == 0)
            return (long)i;
    return -1;
}

int tm_game_matches(const TmGame *g, const char *folded_query)
{
    if (!folded_query || !*folded_query)
        return 1;
    return strstr(g->key, folded_query) != NULL;
}
