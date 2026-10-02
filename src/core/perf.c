/* Performance log for game sessions. */
#define _GNU_SOURCE
#include "perf.h"
#include "log.h"
#include "util.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SESSIONS "sessions.csv"
#define SESSIONS_MAX_BYTES (256 * 1024)
#define SESSIONS_HEADER                                                                                    \
    "inicio;plataforma;emulador;jogo;perfil;duracao_s;cpu_media_mhz;cpu_max_mhz;temp_inicio_c;temp_max_c;" \
    "temp_fim_c;bateria_inicio;bateria_fim;carregando;protecao_termica;saida\n"
#define SAMPLES_HEADER "tempo_s;cpu_mhz;limite_mhz;temp_c;bateria;carregando;perfil\n"

/* Field text without separators or line breaks. */
static void clean(char *dst, size_t size, const char *src)
{
    size_t o = 0;
    for (; src && *src && o + 1 < size; src++)
        dst[o++] = (*src == ';' || *src == '\n' || *src == '\r') ? ',' : *src;
    dst[o] = '\0';
}

static int to_c(long mc) { return mc < 0 ? -1 : (int)((mc + 500) / 1000); }

int tm_perf_open(TmPerf *p, const char *logdir, const char *system, const char *emulator, const char *game,
                 const char *profile)
{
    memset(p, 0, sizeof *p);
    p->temp_start = p->temp_max = p->temp_end = -1;
    p->bat_start = p->bat_end = -1;
    if (tm_path_join(p->dir, sizeof p->dir, logdir, "perf") != 0 || tm_mkdir_p(p->dir) != 0)
        return -1;
    clean(p->system, sizeof p->system, system);
    clean(p->emulator, sizeof p->emulator, emulator);
    clean(p->profile, sizeof p->profile, profile);
    /* game: file name only, never the folder layout of the card */
    const char *base = game ? strrchr(game, '/') : NULL;
    clean(p->game, sizeof p->game, base ? base + 1 : game);
    time_t now = time(NULL);
    struct tm tmv;
    char stamp[32] = "00000000-000000";
    if (localtime_r(&now, &tmv)) {
        strftime(p->started, sizeof p->started, "%Y-%m-%d %H:%M", &tmv);
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tmv);
    }
    char name[96];
    snprintf(name, sizeof name, "%s_%s.csv", stamp, tm_name_is_safe(p->system) ? p->system : "jogo");
    if (tm_path_join(p->file, sizeof p->file, p->dir, name) != 0)
        return -1;
    p->f = fopen(p->file, "w");
    if (!p->f)
        return -1;
    setvbuf(p->f, NULL, _IOLBF, 0); /* a crash keeps every line written so far */
    fputs(SAMPLES_HEADER, p->f);
    return 0;
}

void tm_perf_sample(TmPerf *p, uint64_t now_ms, long cur_khz, long max_khz, long temp_mc, int bat, int charging,
                    const char *profile)
{
    if (!p->f)
        return;
    if (!p->samples)
        p->t0_ms = now_ms;
    p->samples++;
    if (cur_khz > 0) {
        p->khz_sum += cur_khz;
        p->nfreq++;
        if (cur_khz > p->khz_max)
            p->khz_max = cur_khz;
    }
    if (temp_mc >= 0) {
        if (p->temp_start < 0)
            p->temp_start = temp_mc;
        if (temp_mc > p->temp_max)
            p->temp_max = temp_mc;
        p->temp_end = temp_mc;
    }
    if (bat >= 0) {
        if (p->bat_start < 0)
            p->bat_start = bat;
        p->bat_end = bat;
    }
    if (charging == 1)
        p->charged = 1;
    char prof[24];
    clean(prof, sizeof prof, profile ? profile : p->profile);
    fprintf(p->f, "%llu;%ld;%ld;%d;%d;%d;%s\n", (unsigned long long)((now_ms - p->t0_ms) / 1000),
            cur_khz > 0 ? cur_khz / 1000 : -1, max_khz > 0 ? max_khz / 1000 : -1, to_c(temp_mc), bat,
            charging, prof);
}

void tm_perf_throttled(TmPerf *p) { p->throttles++; }

static int cmp_name_desc(const void *a, const void *b) { return strcmp(*(char *const *)b, *(char *const *)a); }

/* Keeps the newest TM_PERF_KEEP_SESSIONS sample files. Only names this
 * module creates (YYYYMMDD-HHMMSS_*.csv) are ever deleted. */
static void prune(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    char *names[512];
    size_t n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < TM_ARRAY_LEN(names)) {
        const char *s = e->d_name;
        if (strlen(s) > 20 && s[8] == '-' && s[15] == '_' && tm_ends_with_ci(s, ".csv") && s[0] >= '0' && s[0] <= '9')
            names[n++] = strdup(s);
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp_name_desc);
    for (size_t i = 0; i < n; i++) {
        if (i >= TM_PERF_KEEP_SESSIONS) {
            char p[TM_PATH_MAX];
            if (tm_path_join(p, sizeof p, dir, names[i]) == 0)
                unlink(p);
        }
        free(names[i]);
    }
}

void tm_perf_close(TmPerf *p, uint64_t now_ms, int exit_code)
{
    if (!p->f)
        return;
    fclose(p->f);
    p->f = NULL;
    TmPerfSummary s;
    memset(&s, 0, sizeof s);
    tm_strlcpy(s.started, p->started, sizeof s.started);
    tm_strlcpy(s.system, p->system, sizeof s.system);
    tm_strlcpy(s.emulator, p->emulator, sizeof s.emulator);
    tm_strlcpy(s.game, p->game, sizeof s.game);
    tm_strlcpy(s.profile, p->profile, sizeof s.profile);
    s.duration_s = p->samples ? (long)((now_ms - p->t0_ms) / 1000) : 0;
    s.avg_mhz = p->nfreq ? (long)(p->khz_sum / p->nfreq / 1000) : -1;
    s.max_mhz = p->khz_max > 0 ? p->khz_max / 1000 : -1;
    s.temp_start_c = to_c(p->temp_start);
    s.temp_max_c = to_c(p->temp_max);
    s.temp_end_c = to_c(p->temp_end);
    s.bat_start = p->bat_start;
    s.bat_end = p->bat_end;
    s.charged = p->charged;
    s.throttles = p->throttles;
    s.exit_code = exit_code;
    char line[512], path[TM_PATH_MAX];
    if (tm_perf_format_summary(&s, line, sizeof line) != 0 || tm_path_join(path, sizeof path, p->dir, SESSIONS) != 0)
        return;
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > SESSIONS_MAX_BYTES) {
        char old[TM_PATH_MAX];
        if (tm_snprintf(old, sizeof old, "%s.1", path) == 0)
            rename(path, old);
    }
    int fresh = !tm_file_exists(path);
    FILE *f = fopen(path, "a");
    if (f) {
        if (fresh)
            fputs(SESSIONS_HEADER, f);
        fputs(line, f);
        fclose(f);
    }
    prune(p->dir);
    LOGI("perf: session %s %s %lds, cpu avg %ld MHz, temp max %d C, battery %d->%d", s.system, s.emulator,
         s.duration_s, s.avg_mhz, s.temp_max_c, s.bat_start, s.bat_end);
}

int tm_perf_format_summary(const TmPerfSummary *s, char *out, size_t size)
{
    char sys[32], emu[48], game[128], prof[24];
    clean(sys, sizeof sys, s->system);
    clean(emu, sizeof emu, s->emulator);
    clean(game, sizeof game, s->game);
    clean(prof, sizeof prof, s->profile);
    return tm_snprintf(out, size, "%s;%s;%s;%s;%s;%ld;%ld;%ld;%d;%d;%d;%d;%d;%d;%d;%d\n", s->started, sys, emu,
                       game, prof, s->duration_s, s->avg_mhz, s->max_mhz, s->temp_start_c, s->temp_max_c,
                       s->temp_end_c, s->bat_start, s->bat_end, s->charged, s->throttles, s->exit_code);
}

int tm_perf_parse_summary(const char *line, TmPerfSummary *s)
{
    char buf[512];
    char *f[16];
    int n = 0;
    memset(s, 0, sizeof *s);
    if (!line || tm_strlcpy(buf, line, sizeof buf) != 0)
        return -1;
    char *p = buf;
    tm_trim(p);
    while (n < 16) {
        f[n++] = p;
        char *sep = strchr(p, ';');
        if (!sep)
            break;
        *sep = '\0';
        p = sep + 1;
    }
    if (n != 16 || f[0][0] < '0' || f[0][0] > '9')
        return -1; /* header or damaged line */
    tm_strlcpy(s->started, f[0], sizeof s->started);
    tm_strlcpy(s->system, f[1], sizeof s->system);
    tm_strlcpy(s->emulator, f[2], sizeof s->emulator);
    tm_strlcpy(s->game, f[3], sizeof s->game);
    tm_strlcpy(s->profile, f[4], sizeof s->profile);
    s->duration_s = atol(f[5]);
    s->avg_mhz = atol(f[6]);
    s->max_mhz = atol(f[7]);
    s->temp_start_c = atoi(f[8]);
    s->temp_max_c = atoi(f[9]);
    s->temp_end_c = atoi(f[10]);
    s->bat_start = atoi(f[11]);
    s->bat_end = atoi(f[12]);
    s->charged = atoi(f[13]);
    s->throttles = atoi(f[14]);
    s->exit_code = atoi(f[15]);
    return 0;
}

size_t tm_perf_recent(const char *logdir, TmPerfSummary *out, size_t max)
{
    char path[TM_PATH_MAX], rel[64];
    snprintf(rel, sizeof rel, "perf/%s", SESSIONS);
    if (!max || tm_path_join(path, sizeof path, logdir, rel) != 0)
        return 0;
    size_t len = 0;
    char *txt = tm_read_file(path, SESSIONS_MAX_BYTES * 2, &len);
    if (!txt)
        return 0;
    /* split into lines, then read them newest (last) first */
    size_t nl = 1;
    for (size_t i = 0; i < len; i++)
        nl += txt[i] == '\n';
    char **lines = malloc(nl * sizeof *lines);
    size_t n = 0, cnt = 0;
    if (lines) {
        char *save = NULL;
        for (char *l = strtok_r(txt, "\n", &save); l && cnt < nl; l = strtok_r(NULL, "\n", &save))
            lines[cnt++] = l;
        for (size_t i = cnt; i > 0 && n < max; i--)
            if (tm_perf_parse_summary(lines[i - 1], &out[n]) == 0)
                n++;
        free(lines);
    }
    free(txt);
    return n;
}

size_t tm_perf_recent_count(const char *logdir)
{
    char path[TM_PATH_MAX], rel[64];
    snprintf(rel, sizeof rel, "perf/%s", SESSIONS);
    if (tm_path_join(path, sizeof path, logdir, rel) != 0)
        return 0;
    char *txt = tm_read_file(path, SESSIONS_MAX_BYTES * 2, NULL);
    size_t n = 0;
    TmPerfSummary s;
    char *save = NULL;
    for (char *l = txt ? strtok_r(txt, "\n", &save) : NULL; l; l = strtok_r(NULL, "\n", &save))
        n += tm_perf_parse_summary(l, &s) == 0;
    free(txt);
    return n;
}

int tm_perf_drain_per_hour(const TmPerfSummary *s)
{
    if (s->charged || s->duration_s < 300 || s->bat_start < 0 || s->bat_end < 0 || s->bat_end > s->bat_start)
        return -1;
    return (int)(((long)(s->bat_start - s->bat_end) * 3600 + s->duration_s / 2) / s->duration_s);
}

int tm_perf_clear(const char *logdir)
{
    char dir[TM_PATH_MAX];
    int removed = 0;
    if (tm_path_join(dir, sizeof dir, logdir, "perf") == 0) {
        DIR *d = opendir(dir);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            const char *s = e->d_name;
            if (tm_ends_with_ci(s, ".csv") || strcmp(s, SESSIONS ".1") == 0) {
                char p[TM_PATH_MAX];
                if (tm_path_join(p, sizeof p, dir, s) == 0 && unlink(p) == 0)
                    removed++;
            }
        }
        if (d)
            closedir(d);
    }
    char ra[TM_PATH_MAX];
    if (tm_path_join(ra, sizeof ra, logdir, "retroarch/retroarch.log") == 0 && unlink(ra) == 0)
        removed++;
    LOGI("perf: cleared %d log files", removed);
    return removed;
}
