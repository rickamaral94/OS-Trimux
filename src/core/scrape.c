/* Game covers from libretro-thumbnails. */
#define _GNU_SOURCE
#include "scrape.h"
#include "image.h"
#include "log.h"
#include "net.h"
#include "util.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef TRIMUX_VERSION
#define TRIMUX_VERSION "dev"
#endif
#define CURL "/usr/bin/curl"
#define BASE_URL "https://thumbnails.libretro.com"
#define MAX_DOWNLOAD (8L * 1024 * 1024)

/* ------------------------------------------------------------ pure helpers */

void tm_scrape_sanitize(const char *in, char *out, size_t size)
{
    size_t o = 0;
    for (; in && *in && o + 1 < size; in++)
        out[o++] = strchr("&*/:`<>?\\|\"", *in) ? '_' : *in;
    if (size)
        out[o] = '\0';
}

static int encode(const char *in, char *out, size_t size, size_t *o)
{
    static const char hx[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        int plain = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                    strchr("-_.~", *p);
        if (*o + (plain ? 1 : 3) >= size)
            return -1;
        if (plain) {
            out[(*o)++] = (char)*p;
        } else {
            out[(*o)++] = '%';
            out[(*o)++] = hx[*p >> 4];
            out[(*o)++] = hx[*p & 15];
        }
    }
    out[*o] = '\0';
    return 0;
}

static const char *kind_dir(TmThumbKind k)
{
    return k == TM_THUMB_SNAP ? "Named_Snaps" : k == TM_THUMB_TITLE ? "Named_Titles" : "Named_Boxarts";
}

int tm_scrape_url(const char *repo, TmThumbKind kind, const char *name, char *out, size_t size)
{
    char clean[256];
    size_t o = 0;
    if (!repo || !*repo || !name || !*name || size < sizeof BASE_URL + 2)
        return -1;
    tm_scrape_sanitize(name, clean, sizeof clean);
    o = (size_t)snprintf(out, size, "%s/", BASE_URL);
    if (encode(repo, out, size, &o) != 0)
        return -1;
    if (o + strlen(kind_dir(kind)) + 3 >= size)
        return -1;
    o += (size_t)snprintf(out + o, size - o, "/%s/", kind_dir(kind));
    if (encode(clean, out, size, &o) != 0 || o + 5 >= size)
        return -1;
    memcpy(out + o, ".png", 5);
    return 0;
}

static void strip_tag(const char *in, const char *tag_prefix, char *out, size_t size)
{
    /* removes " (Disc 1)" style tags */
    tm_strlcpy(out, in, size);
    char *p = strstr(out, tag_prefix);
    if (!p)
        return;
    char *end = strchr(p + 1, ')');
    if (!end)
        return;
    memmove(p, end + 1, strlen(end + 1) + 1);
}

size_t tm_scrape_candidates(const char *base, char out[][256], size_t max)
{
    size_t n = 0;
    if (!base || !*base || !max)
        return 0;
    tm_strlcpy(out[n++], base, 256);
    char nodisc[256];
    strip_tag(base, " (Disc ", nodisc, sizeof nodisc);
    if (n < max && strcmp(nodisc, base) != 0)
        tm_strlcpy(out[n++], nodisc, 256);
    if (!strchr(base, '(')) {
        /* plain names ("Celeste"): try the usual No-Intro regions */
        static const char *const regions[] = {" (USA)", " (World)", " (Europe)", " (Japan)"};
        for (size_t i = 0; i < TM_ARRAY_LEN(regions) && n < max; i++)
            if (tm_snprintf(out[n], 256, "%s%s", base, regions[i]) == 0)
                n++;
    }
    return n;
}

int tm_scrape_cover_path(const char *sd, const char *relpath, const char *system_id, char *out, size_t size)
{
    char folder[128], name[256];
    const char *file = strrchr(relpath, '/');
    file = file ? file + 1 : relpath;
    tm_strlcpy(name, file, sizeof name);
    char *dot = strrchr(name, '.');
    if (dot && dot != name)
        *dot = '\0';
    /* the folder the game was found in (Roms/NES/... -> NES), as the stock UI does */
    tm_strlcpy(folder, system_id, sizeof folder);
    if (strncmp(relpath, "Roms/", 5) == 0) {
        const char *s = relpath + 5, *e = strchr(s, '/');
        if (e && (size_t)(e - s) < sizeof folder) {
            memcpy(folder, s, (size_t)(e - s));
            folder[e - s] = '\0';
        }
    }
    if (!tm_name_is_safe(folder) || !tm_name_is_safe(name))
        return -1;
    return tm_snprintf(out, size, "%s/Imgs/%s/%s.png", sd, folder, name);
}

TmThumbKind tm_thumb_kind_parse(const char *s)
{
    if (s && strcmp(s, "snap") == 0)
        return TM_THUMB_SNAP;
    if (s && strcmp(s, "title") == 0)
        return TM_THUMB_TITLE;
    return TM_THUMB_BOXART;
}

const char *tm_thumb_kind_id(TmThumbKind k) { return k == TM_THUMB_SNAP ? "snap" : k == TM_THUMB_TITLE ? "title" : "boxart"; }

static int cmp_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int tm_arcade_load(TmArcadeNames *a, const char *path)
{
    memset(a, 0, sizeof *a);
    size_t len = 0;
    a->data = tm_read_file(path, 4u << 20, &len);
    if (!a->data)
        return -1;
    size_t lines = 1;
    for (size_t i = 0; i < len; i++)
        lines += a->data[i] == '\n';
    a->names = malloc(lines * sizeof *a->names);
    if (!a->names) {
        tm_arcade_free(a);
        return -1;
    }
    char *save = NULL;
    for (char *l = strtok_r(a->data, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(l, '\t');
        if (!tab || tab == l || !tab[1])
            continue;
        *tab = '\0';
        char *cr = strchr(tab + 1, '\r');
        if (cr)
            *cr = '\0';
        a->names[a->count++] = l;
    }
    qsort(a->names, a->count, sizeof *a->names, cmp_names);
    return 0;
}

const char *tm_arcade_title(const TmArcadeNames *a, const char *short_name)
{
    if (!a || !a->count || !short_name)
        return NULL;
    char *const *hit = bsearch(&short_name, a->names, a->count, sizeof *a->names, cmp_names);
    return hit ? *hit + strlen(*hit) + 1 : NULL;
}

void tm_arcade_free(TmArcadeNames *a)
{
    free(a->names);
    free(a->data);
    memset(a, 0, sizeof *a);
}

/* ------------------------------------------------------------ status */

static int status_path(const TmPaths *p, const char *name, char *out, size_t size)
{
    return tm_path_join(out, size, p->tmp, name);
}

static void status_write(const TmPaths *p, const TmScrapeStatus *st)
{
    char path[TM_PATH_MAX], buf[256];
    int n = snprintf(buf, sizeof buf, "state=%s\ndone=%d\ntotal=%d\nfound=%d\nmissing=%d\n", st->state, st->done,
                     st->total, st->found, st->missing);
    if (n > 0 && status_path(p, "scrape.status", path, sizeof path) == 0)
        tm_atomic_write(path, buf, (size_t)n);
}

int tm_scrape_status_read(const TmPaths *p, TmScrapeStatus *st)
{
    memset(st, 0, sizeof *st);
    char path[TM_PATH_MAX];
    if (status_path(p, "scrape.status", path, sizeof path) != 0)
        return -1;
    char *txt = tm_read_file(path, 4096, NULL);
    if (!txt)
        return -1;
    char *save = NULL;
    for (char *l = strtok_r(txt, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(l, '=');
        if (!eq)
            continue;
        *eq++ = '\0';
        if (strcmp(l, "state") == 0)
            tm_strlcpy(st->state, eq, sizeof st->state);
        else if (strcmp(l, "done") == 0)
            st->done = atoi(eq);
        else if (strcmp(l, "total") == 0)
            st->total = atoi(eq);
        else if (strcmp(l, "found") == 0)
            st->found = atoi(eq);
        else if (strcmp(l, "missing") == 0)
            st->missing = atoi(eq);
    }
    free(txt);
    return 0;
}

int tm_scrape_running(const TmPaths *p)
{
    char path[TM_PATH_MAX];
    long pid = 0;
    if (status_path(p, "scrape.pid", path, sizeof path) != 0 || tm_read_long(path, &pid) != 0 || pid <= 1)
        return 0;
    return kill((pid_t)pid, 0) == 0;
}

void tm_scrape_request_stop(const TmPaths *p)
{
    char path[TM_PATH_MAX];
    if (status_path(p, "scrape.stop", path, sizeof path) == 0)
        tm_atomic_write(path, "1\n", 2);
}

/* ------------------------------------------------------------ not-found list */

typedef struct {
    char **items;
    size_t count, cap;
} StrSet;

static int set_has(const StrSet *s, const char *v)
{
    return s->count && bsearch(&v, s->items, s->count, sizeof *s->items, cmp_names) != NULL;
}

static void set_load(StrSet *s, const char *path)
{
    memset(s, 0, sizeof *s);
    char *txt = tm_read_file(path, 4u << 20, NULL);
    if (!txt)
        return;
    char *save = NULL;
    for (char *l = strtok_r(txt, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        if (s->count == s->cap) {
            size_t nc = s->cap ? s->cap * 2 : 256;
            char **ni = realloc(s->items, nc * sizeof *ni);
            if (!ni)
                break;
            s->items = ni;
            s->cap = nc;
        }
        s->items[s->count++] = strdup(l);
    }
    free(txt);
    qsort(s->items, s->count, sizeof *s->items, cmp_names);
}

static void set_free(StrSet *s)
{
    for (size_t i = 0; i < s->count; i++)
        free(s->items[i]);
    free(s->items);
    memset(s, 0, sizeof *s);
}

/* ------------------------------------------------------------ runner */

/* curl exit codes: 22 = HTTP error (e.g. 404, not found). */
static int download(const TmPaths *p, const char *url, const char *dst)
{
    char curl[TM_PATH_MAX], ca[TM_PATH_MAX], ua[64];
    if (tm_fw_path(curl, sizeof curl, CURL) != 0 || tm_path_join(ca, sizeof ca, p->share, "cacert.pem") != 0)
        return -1;
    snprintf(ua, sizeof ua, "TriMux/%s", TRIMUX_VERSION);
    char maxsize[24];
    snprintf(maxsize, sizeof maxsize, "%ld", MAX_DOWNLOAD);
    /* -f: HTTP errors fail; TLS is always verified against the bundled CA list */
    char *argv[] = {curl, "-fsSL", "--proto", "=https", "--max-time", "30", "--connect-timeout", "10",
                    "--max-filesize", maxsize, "--cacert", ca, "-A", ua, "-o", (char *)dst, (char *)url, NULL};
    return tm_run(argv, NULL, 0, 40000);
}

static int is_image(const char *path)
{
    unsigned char sig[8] = {0};
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(sig, 1, sizeof sig, f);
    fclose(f);
    return n >= 4 && ((sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G') ||
                      (sig[0] == 0xFF && sig[1] == 0xD8));
}

static int wifi_ready(void)
{
    TmWifiStatus st;
    return tm_wifi_running() && tm_wifi_status(&st) == 0 && strcmp(st.state, "COMPLETED") == 0 && st.ip[0];
}

static int stop_requested(const TmPaths *p)
{
    char path[TM_PATH_MAX];
    return status_path(p, "scrape.stop", path, sizeof path) == 0 && tm_file_exists(path);
}

/* Covers download in the background; while a game runs, it waits. */
static void wait_while_in_game(const TmPaths *p)
{
    char marker[TM_PATH_MAX];
    if (tm_path_join(marker, sizeof marker, p->state, "in_game") != 0)
        return;
    while (tm_file_exists(marker) && !stop_requested(p))
        sleep(5);
}

int tm_scrape_run(const TmPaths *p, const TmCatalog *cat, const TmLibrary *lib, const TmScrapeOptions *o)
{
    TmScrapeStatus st;
    memset(&st, 0, sizeof st);
    char pidfile[TM_PATH_MAX], stopfile[TM_PATH_MAX], missfile[TM_PATH_MAX], tmp[TM_PATH_MAX], arc[TM_PATH_MAX];
    if (tm_scrape_running(p)) {
        LOGI("scrape: already running");
        return 1;
    }
    tm_mkdir_p(p->tmp);
    status_path(p, "scrape.pid", pidfile, sizeof pidfile);
    status_path(p, "scrape.stop", stopfile, sizeof stopfile);
    status_path(p, "scrape.download", tmp, sizeof tmp);
    unlink(stopfile);
    char pidbuf[32];
    snprintf(pidbuf, sizeof pidbuf, "%d\n", (int)getpid());
    tm_atomic_write(pidfile, pidbuf, strlen(pidbuf));
    setpriority(PRIO_PROCESS, 0, 10); /* never compete with the menu */

    for (int waited = 0; !wifi_ready(); waited++) {
        if (waited >= o->wait_wifi_s || stop_requested(p)) {
            tm_strlcpy(st.state, "nowifi", sizeof st.state);
            status_write(p, &st);
            unlink(pidfile);
            LOGI("scrape: no Wi-Fi connection, nothing downloaded");
            return 2;
        }
        sleep(1);
    }

    char cachedir[TM_PATH_MAX];
    tm_path_join(cachedir, sizeof cachedir, p->data, "cache");
    tm_mkdir_p(cachedir);
    tm_path_join(missfile, sizeof missfile, cachedir, "covers-missing.txt");
    StrSet miss;
    set_load(&miss, missfile);
    if (o->retry_missing) {
        set_free(&miss);
        unlink(missfile);
    }
    TmArcadeNames arcade;
    tm_path_join(arc, sizeof arc, p->share, "arcade-names.tsv");
    int have_arcade = tm_arcade_load(&arcade, arc) == 0;

    /* what is missing */
    char cover[TM_PATH_MAX];
    for (size_t i = 0; i < lib->count; i++) {
        const TmGame *g = &lib->games[i];
        const TmSystem *sys = &cat->systems[g->system];
        if (sys->thumbs[0] && tm_scrape_cover_path(p->sd, g->relpath, sys->id, cover, sizeof cover) == 0 &&
            !tm_file_exists(cover) && !set_has(&miss, g->relpath))
            st.total++;
    }
    tm_strlcpy(st.state, "running", sizeof st.state);
    status_write(p, &st);
    LOGI("scrape: %d games without %s images", st.total, tm_thumb_kind_id(o->kind));

    FILE *missf = fopen(missfile, "a");
    int net_errors = 0;
    for (size_t i = 0; i < lib->count && strcmp(st.state, "running") == 0; i++) {
        const TmGame *g = &lib->games[i];
        const TmSystem *sys = &cat->systems[g->system];
        if (!sys->thumbs[0] || tm_scrape_cover_path(p->sd, g->relpath, sys->id, cover, sizeof cover) != 0 ||
            tm_file_exists(cover) || set_has(&miss, g->relpath))
            continue;
        wait_while_in_game(p);
        if (stop_requested(p)) {
            tm_strlcpy(st.state, "stopped", sizeof st.state);
            break;
        }
        char base[256];
        const char *file = strrchr(g->relpath, '/');
        tm_strlcpy(base, file ? file + 1 : g->relpath, sizeof base);
        char *dot = strrchr(base, '.');
        if (dot && dot != base)
            *dot = '\0';
        const char *title = have_arcade ? tm_arcade_title(&arcade, base) : NULL;
        char cands[8][256];
        size_t nc = title ? (tm_strlcpy(cands[0], title, 256), 1) : tm_scrape_candidates(base, cands, 8);
        int got = 0, net_fail = 0;
        char repos[160];
        tm_strlcpy(repos, sys->thumbs, sizeof repos);
        char *save = NULL;
        for (char *repo = strtok_r(repos, "|", &save); repo && !got && !net_fail; repo = strtok_r(NULL, "|", &save)) {
            repo = tm_trim(repo);
            for (size_t c = 0; c < nc && !got; c++) {
                char url[1024];
                if (tm_scrape_url(repo, o->kind, cands[c], url, sizeof url) != 0)
                    continue;
                unlink(tmp);
                int rc = download(p, url, tmp);
                if (rc == 0 && is_image(tmp)) {
                    char dir[TM_PATH_MAX];
                    tm_strlcpy(dir, cover, sizeof dir);
                    char *slash = strrchr(dir, '/');
                    if (slash)
                        *slash = '\0';
                    if (tm_mkdir_p(dir) == 0 && tm_image_fit_png(tmp, cover, TM_COVER_MAX_W, TM_COVER_MAX_H) == 0)
                        got = 1;
                } else if (rc != 22 && rc != 0) {
                    net_fail = 1; /* DNS, timeout, TLS...: not "image missing" */
                    break;
                }
            }
        }
        unlink(tmp);
        st.done++;
        if (got) {
            st.found++;
            net_errors = 0;
            LOGD("scrape: cover for %s", g->relpath);
        } else if (net_fail) {
            st.done--; /* will be tried again next time */
            if (++net_errors >= 3) {
                tm_strlcpy(st.state, "network", sizeof st.state);
                LOGW("scrape: repeated network errors, stopping");
            }
        } else {
            st.missing++;
            if (missf)
                fprintf(missf, "%s\n", g->relpath);
        }
        status_write(p, &st);
    }
    if (strcmp(st.state, "running") == 0)
        tm_strlcpy(st.state, "done", sizeof st.state);
    status_write(p, &st);
    if (missf)
        fclose(missf);
    if (have_arcade)
        tm_arcade_free(&arcade);
    set_free(&miss);
    unlink(pidfile);
    unlink(stopfile);
    LOGI("scrape: %s, %d found, %d not found of %d", st.state, st.found, st.missing, st.total);
    return strcmp(st.state, "done") == 0 ? 0 : 3;
}
