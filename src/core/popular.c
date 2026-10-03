#define _GNU_SOURCE
#include "popular.h"
#include "library.h"
#include "log.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tm_popular_key(const char *title, char *out, size_t size)
{
    char f[512];
    tm_fold_key(title, f, sizeof f);
    char *s = f;
    if (strncmp(s, "the ", 4) == 0)
        s += 4;
    /* No-Intro writes "Legend of Zelda, The - ..." */
    for (char *t; (t = strstr(s, ", the")) != NULL && (t[5] == '\0' || t[5] == ' ' || t[5] == ':');)
        memmove(t, t + 5, strlen(t + 5) + 1);
    size_t o = 0;
    for (; *s && o + 4 < size; s++) {
        if (*s == '&') {
            memcpy(out + o, "and", 3);
            o += 3;
        } else if (isalnum((unsigned char)*s)) {
            out[o++] = *s;
        }
    }
    out[o] = '\0';
}

static int cmp_entry(const void *a, const void *b)
{
    return strcmp(((const TmPopEntry *)a)->key, ((const TmPopEntry *)b)->key);
}

static void list_load(TmPopList *l, const char *path)
{
    size_t len = 0;
    char *text = tm_read_file(path, 1u << 20, &len);
    if (!text)
        return;
    size_t cap = 0;
    int rank = 0;
    for (char *save = NULL, *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *t = tm_trim(line);
        if (!*t || *t == '#')
            continue;
        rank++;
        for (char *s2 = NULL, *alt = strtok_r(t, "|", &s2); alt; alt = strtok_r(NULL, "|", &s2)) {
            char key[256];
            tm_popular_key(alt, key, sizeof key);
            if (!key[0])
                continue;
            if (l->n == cap) {
                size_t nc = cap ? cap * 2 : 64;
                TmPopEntry *ne = realloc(l->e, nc * sizeof *ne);
                if (!ne)
                    break;
                l->e = ne;
                cap = nc;
            }
            l->e[l->n].key = strdup(key);
            l->e[l->n].rank = rank;
            if (l->e[l->n].key)
                l->n++;
        }
    }
    free(text);
    if (l->n)
        qsort(l->e, l->n, sizeof *l->e, cmp_entry);
    /* the same key twice (alias of two entries): keep the better rank */
    size_t k = 0;
    for (size_t i = 0; i < l->n; i++) {
        if (k && strcmp(l->e[k - 1].key, l->e[i].key) == 0) {
            if (l->e[i].rank < l->e[k - 1].rank)
                l->e[k - 1].rank = l->e[i].rank;
            free(l->e[i].key);
            continue;
        }
        l->e[k++] = l->e[i];
    }
    l->n = k;
}

int tm_popular_load(TmPopular *p, const TmCatalog *cat, const char *share_dir)
{
    memset(p, 0, sizeof *p);
    p->lists = calloc(cat->nsystems ? cat->nsystems : 1, sizeof *p->lists);
    if (!p->lists)
        return -1;
    p->nlists = cat->nsystems;
    for (size_t i = 0; i < cat->nsystems; i++) {
        const TmSystem *sys = &cat->systems[i];
        if (!sys->popular[0])
            continue;
        /* systems sharing a list (GB and GBC) read it once each: small files */
        char rel[64], path[TM_PATH_MAX];
        if (tm_snprintf(rel, sizeof rel, "popular/%s.txt", sys->popular) == 0 &&
            tm_path_join(path, sizeof path, share_dir, rel) == 0)
            list_load(&p->lists[i], path);
    }
    return 0;
}

void tm_popular_free(TmPopular *p)
{
    for (size_t i = 0; p->lists && i < p->nlists; i++) {
        for (size_t j = 0; j < p->lists[i].n; j++)
            free(p->lists[i].e[j].key);
        free(p->lists[i].e);
    }
    free(p->lists);
    memset(p, 0, sizeof *p);
}

int tm_popular_rank(const TmPopular *p, int system, const char *relpath)
{
    if (system < 0 || (size_t)system >= p->nlists || !p->lists[system].n || !relpath)
        return 0;
    const char *file = strrchr(relpath, '/');
    char name[256], key[256];
    tm_clean_name(file ? file + 1 : relpath, 1, name, sizeof name);
    char *disc = strstr(name, " (Disc"); /* clean names keep disc markers */
    if (disc)
        *disc = '\0';
    tm_popular_key(name, key, sizeof key);
    TmPopEntry probe = {key, 0};
    const TmPopEntry *e = bsearch(&probe, p->lists[system].e, p->lists[system].n, sizeof probe, cmp_entry);
    return e ? e->rank : 0;
}

size_t tm_popular_count(const TmPopular *p, int system)
{
    if (system < 0 || (size_t)system >= p->nlists)
        return 0;
    /* ranks are 1..max; aliases share a rank */
    int max = 0;
    for (size_t i = 0; i < p->lists[system].n; i++)
        if (p->lists[system].e[i].rank > max)
            max = p->lists[system].e[i].rank;
    return (size_t)max;
}

/* INI keys cannot hold '=' (file names can): %-encode it and '%' */
static void plays_key(const char *relpath, char *out, size_t size)
{
    size_t o = 0;
    for (const char *s = relpath; *s && o + 4 < size; s++) {
        if (*s == '=' || *s == '%') {
            snprintf(out + o, size - o, "%%%02X", (unsigned char)*s);
            o += 3;
        } else {
            out[o++] = *s;
        }
    }
    out[o] = '\0';
}

void tm_plays_get(const TmIni *plays, const char *relpath, TmPlays *out)
{
    memset(out, 0, sizeof *out);
    char key[TM_PATH_MAX * 3];
    plays_key(relpath, key, sizeof key);
    const char *v = tm_ini_get(plays, "plays", key, NULL);
    if (v && sscanf(v, "%ld %ld %ld", &out->times, &out->seconds, &out->last) < 1)
        memset(out, 0, sizeof *out);
}

int tm_plays_add(const char *plays_path, const char *relpath, long seconds, long now, long min_seconds)
{
    if (seconds < min_seconds)
        return 0;
    TmIni ini;
    tm_ini_init(&ini);
    if (tm_ini_load(&ini, plays_path) != 0) {
        tm_ini_free(&ini);
        return -1;
    }
    TmPlays p;
    tm_plays_get(&ini, relpath, &p);
    char v[64];
    snprintf(v, sizeof v, "%ld %ld %ld", p.times + 1, p.seconds + seconds, now);
    char key[TM_PATH_MAX * 3];
    plays_key(relpath, key, sizeof key);
    int rc = tm_ini_set(&ini, "plays", key, v);
    if (rc == 0)
        rc = tm_ini_save(&ini, plays_path);
    tm_ini_free(&ini);
    return rc;
}
