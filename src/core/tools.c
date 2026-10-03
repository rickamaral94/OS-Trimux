#define _GNU_SOURCE
#include "tools.h"
#include "log.h"
#include "popular.h"
#include "store.h"
#include "update.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------ statistics */

static void top_insert(TmStatGame *top, size_t *n, size_t max, const TmStatGame *g)
{
    size_t i = *n;
    while (i > 0 && top[i - 1].seconds < g->seconds)
        i--;
    if (i >= max)
        return;
    size_t last = *n < max ? *n : max - 1;
    memmove(&top[i + 1], &top[i], (last - i) * sizeof *top);
    top[i] = *g;
    if (*n < max)
        (*n)++;
}

static int cmp_system(const void *a, const void *b)
{
    const TmStatSystem *x = a, *y = b;
    return (y->seconds > x->seconds) - (y->seconds < x->seconds);
}

void tm_stats_compute(const TmIni *plays, const TmLibrary *lib, TmStats *out)
{
    memset(out, 0, sizeof *out);
    out->last.game = -1;
    for (size_t i = 0; i < lib->count; i++) {
        TmPlays pl;
        tm_plays_get(plays, lib->games[i].relpath, &pl);
        if (pl.times <= 0)
            continue;
        TmStatGame g = {(long)i, pl.times, pl.seconds, pl.last};
        out->seconds += pl.seconds;
        out->times += pl.times;
        out->games++;
        top_insert(out->top, &out->ntop, TM_STATS_TOP, &g);
        if (out->last.game < 0 || pl.last > out->last.last)
            out->last = g;
        size_t s = 0;
        while (s < out->nsystems && out->systems[s].system != lib->games[i].system)
            s++;
        if (s == out->nsystems) {
            if (s >= TM_STATS_SYSTEMS)
                continue;
            out->systems[s].system = lib->games[i].system;
            out->nsystems++;
        }
        out->systems[s].seconds += pl.seconds;
        out->systems[s].times += pl.times;
        out->systems[s].games++;
    }
    qsort(out->systems, out->nsystems, sizeof *out->systems, cmp_system);
}

void tm_format_duration(long s, char *out, size_t size)
{
    if (s < 60)
        snprintf(out, size, "%ld s", s < 0 ? 0 : s);
    else if (s < 3600)
        snprintf(out, size, "%ld min", s / 60);
    else if (s % 3600 < 60)
        snprintf(out, size, "%ld h", s / 3600);
    else
        snprintf(out, size, "%ld h %ld min", s / 3600, (s % 3600) / 60);
}

/* ------------------------------------------------------------ random */

static unsigned next_rand(unsigned *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return (*seed >> 8) & 0xFFFFFF;
}

long tm_random_game(const TmLibrary *lib, const TmIni *plays, int system, int unplayed, unsigned *seed)
{
    for (int pass = unplayed ? 0 : 1; pass < 2; pass++) {
        size_t n = 0;
        for (size_t i = 0; i < lib->count; i++) {
            if (system >= 0 && lib->games[i].system != system)
                continue;
            if (pass == 0) {
                TmPlays pl;
                tm_plays_get(plays, lib->games[i].relpath, &pl);
                if (pl.times > 0)
                    continue;
            }
            n++;
        }
        if (!n)
            continue;
        size_t want = next_rand(seed) % n;
        for (size_t i = 0; i < lib->count; i++) {
            if (system >= 0 && lib->games[i].system != system)
                continue;
            if (pass == 0) {
                TmPlays pl;
                tm_plays_get(plays, lib->games[i].relpath, &pl);
                if (pl.times > 0)
                    continue;
            }
            if (want-- == 0)
                return (long)i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------ protection */

int tm_path_protected(const char *relpath)
{
    static const char *const sys[] = {"TriMux", "TriMux.old", "TriMux.swap", ".trimux-new", ".trimux-store-new",
                                      "trimui", "trimui.old"};
    while (*relpath == '/')
        relpath++;
    size_t n = strcspn(relpath, "/");
    for (size_t i = 0; i < sizeof sys / sizeof *sys; i++)
        if (strlen(sys[i]) == n && strncasecmp(relpath, sys[i], n) == 0) /* FAT ignores case */
            return 1;
    return 0;
}

/* ------------------------------------------------------------ cleanup */

static const char *const k_clean_ids[TM_CLEAN_N] = {"computer", "logs", "temp"};

const char *tm_clean_id(int cat) { return cat >= 0 && cat < TM_CLEAN_N ? k_clean_ids[cat] : ""; }

int tm_clean_parse(const char *id)
{
    for (int i = 0; i < TM_CLEAN_N; i++)
        if (strcmp(id, k_clean_ids[i]) == 0)
            return i;
    return -1;
}

typedef struct {
    const TmPaths *p;
    TmCleanReport *rep;
    int remove;
} Walk;

static void note(Walk *w, int cat, const char *rel, uint64_t bytes)
{
    TmCleanCat *c = &w->rep->cat[cat];
    c->files++;
    c->bytes += bytes;
    if (c->nsample < TM_CLEAN_SAMPLES)
        tm_strlcpy(c->sample[c->nsample++], rel, sizeof c->sample[0]);
}

/* Counts (and, when removing, deletes) a whole tree. Never follows links. */
static void tree(Walk *w, int cat, const char *abs, const char *rel, int depth)
{
    struct stat st;
    if (lstat(abs, &st) != 0)
        return;
    if (!S_ISDIR(st.st_mode)) {
        if (!w->remove || unlink(abs) == 0)
            note(w, cat, rel, (uint64_t)st.st_size);
        return;
    }
    DIR *d = depth < 24 ? opendir(abs) : NULL;
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char a[TM_PATH_MAX], r[TM_PATH_MAX];
        if (tm_path_join(a, sizeof a, abs, e->d_name) == 0 && tm_path_join(r, sizeof r, rel, e->d_name) == 0)
            tree(w, cat, a, r, depth + 1);
    }
    if (d)
        closedir(d);
    if (w->remove)
        rmdir(abs);
}

static int computer_junk_file(const char *name)
{
    return (name[0] == '.' && name[1] == '_' && name[2]) || strcmp(name, ".DS_Store") == 0 ||
           strcasecmp(name, "Thumbs.db") == 0;
}

static int computer_junk_root_dir(const char *name)
{
    static const char *const dirs[] = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems"};
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++)
        if (strcmp(name, dirs[i]) == 0)
            return 1;
    return 0;
}

static void computer(Walk *w, const char *abs, const char *rel, int depth)
{
    DIR *d = opendir(abs);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *n = e->d_name;
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0)
            continue;
        char a[TM_PATH_MAX], r[TM_PATH_MAX];
        if (tm_path_join(a, sizeof a, abs, n) != 0 ||
            (rel[0] ? tm_path_join(r, sizeof r, rel, n) : tm_strlcpy(r, n, sizeof r)) != 0)
            continue;
        if (depth == 0 && (tm_path_protected(n) || strcmp(n, "System Volume Information") == 0))
            continue;
        struct stat st;
        if (lstat(a, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth == 0 && computer_junk_root_dir(n))
                tree(w, TM_CLEAN_COMPUTER, a, r, 1);
            else if (depth < 16)
                computer(w, a, r, depth + 1);
        } else if (S_ISREG(st.st_mode) && computer_junk_file(n)) {
            if (!w->remove || unlink(a) == 0)
                note(w, TM_CLEAN_COMPUTER, r, (uint64_t)st.st_size);
        }
    }
    closedir(d);
}

static void one(Walk *w, int cat, const char *abs, const char *rel)
{
    struct stat st;
    if (lstat(abs, &st) == 0)
        tree(w, cat, abs, rel, 0);
}

static void logs(Walk *w)
{
    const TmPaths *p = w->p;
    char a[TM_PATH_MAX], dir[TM_PATH_MAX];
    const char *rel_logs = "TriMuxData/logs";
    if (tm_path_join(a, sizeof a, p->logdir, "trimux.log.1") == 0)
        one(w, TM_CLEAN_LOGS, a, "TriMuxData/logs/trimux.log.1");
    if (tm_path_join(a, sizeof a, p->logdir, "retroarch/retroarch.log") == 0)
        one(w, TM_CLEAN_LOGS, a, "TriMuxData/logs/retroarch/retroarch.log");
    /* performance logs: the same files as Diagnóstico › Apagar registros */
    if (tm_path_join(dir, sizeof dir, p->logdir, "perf") != 0)
        return;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!tm_ends_with_ci(e->d_name, ".csv") && strcmp(e->d_name, "sessions.csv.1") != 0)
            continue;
        char r[TM_PATH_MAX];
        if (tm_path_join(a, sizeof a, dir, e->d_name) == 0 &&
            tm_snprintf(r, sizeof r, "%s/perf/%s", rel_logs, e->d_name) == 0)
            one(w, TM_CLEAN_LOGS, a, r);
    }
    if (d)
        closedir(d);
}

static void temp(Walk *w)
{
    const TmPaths *p = w->p;
    char a[TM_PATH_MAX];
    if (!tm_update_running(p) && tm_path_join(a, sizeof a, p->sd, ".trimux-new") == 0)
        one(w, TM_CLEAN_TEMP, a, ".trimux-new");
    if (!tm_store_running(p)) {
        if (tm_path_join(a, sizeof a, p->sd, ".trimux-store-new") == 0)
            one(w, TM_CLEAN_TEMP, a, ".trimux-store-new");
        if (tm_path_join(a, sizeof a, p->data, ".store-download.zip") == 0)
            one(w, TM_CLEAN_TEMP, a, "TriMuxData/.store-download.zip");
    }
}

static int run(const TmPaths *p, unsigned mask, TmCleanReport *rep, int remove)
{
    memset(rep, 0, sizeof *rep);
    Walk w = {p, rep, remove};
    if (mask & (1u << TM_CLEAN_COMPUTER))
        computer(&w, p->sd, "", 0);
    if (mask & (1u << TM_CLEAN_LOGS))
        logs(&w);
    if (mask & (1u << TM_CLEAN_TEMP))
        temp(&w);
    if (remove) {
        sync();
        for (int i = 0; i < TM_CLEAN_N; i++)
            if (mask & (1u << i))
                LOGI("clean: %s removed %d files (%llu bytes)", tm_clean_id(i), rep->cat[i].files,
                     (unsigned long long)rep->cat[i].bytes);
    }
    return 0;
}

int tm_clean_scan(const TmPaths *p, TmCleanReport *out) { return run(p, ~0u, out, 0); }

int tm_clean_run(const TmPaths *p, unsigned mask, TmCleanReport *done) { return run(p, mask, done, 1); }
