/* Performance log for game sessions (opt-in, setting [diag] perf).
 *
 * While a game runs, the launcher already reads temperature and CPU limits
 * every 10 s; with the log on, it also appends one line per sample to
 * TriMuxData/logs/perf/<date>_<system>.csv and, at the end, one summary line
 * to TriMuxData/logs/perf/sessions.csv. All values are integers (MHz, °C, %)
 * and the separator is ';', so the files open directly in a spreadsheet in
 * any locale. Nothing is written while the log is off. */
#ifndef TRIMUX_PERF_H
#define TRIMUX_PERF_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TM_PERF_KEEP_SESSIONS 30 /* per-session sample files kept */

typedef struct {
    FILE *f;
    char dir[1024];  /* .../logs/perf */
    char file[1100]; /* this session's samples */
    char started[20]; /* YYYY-MM-DD HH:MM */
    char system[32], emulator[48], game[128], profile[24];
    uint64_t t0_ms;
    long temp_start, temp_max, temp_end; /* m°C, -1 unknown */
    int bat_start, bat_end;              /* %, -1 unknown */
    int charged;                         /* charger seen during the session */
    long long khz_sum;
    long khz_max;
    int nfreq, samples, throttles;
} TmPerf;

typedef struct {
    char started[20];
    char system[32], emulator[48], game[128], profile[24];
    long duration_s;
    long avg_mhz, max_mhz;
    int temp_start_c, temp_max_c, temp_end_c; /* -1 unknown */
    int bat_start, bat_end;                   /* -1 unknown */
    int charged, throttles, exit_code;
} TmPerfSummary;

/* logdir: TriMuxData/logs. Returns 0 and an open session, or -1. */
int tm_perf_open(TmPerf *p, const char *logdir, const char *system, const char *emulator, const char *game,
                 const char *profile);
/* One sample. Any value may be -1 (unknown). profile: the limit in force. */
void tm_perf_sample(TmPerf *p, uint64_t now_ms, long cur_khz, long max_khz, long temp_mc, int bat, int charging,
                    const char *profile);
void tm_perf_throttled(TmPerf *p);
/* Writes the summary line, closes the file and prunes old sample files. */
void tm_perf_close(TmPerf *p, uint64_t now_ms, int exit_code);

/* Pure: summary line <-> struct. */
int tm_perf_format_summary(const TmPerfSummary *s, char *out, size_t size);
int tm_perf_parse_summary(const char *line, TmPerfSummary *s);
/* Newest first, at most max sessions from logdir/perf/sessions.csv. */
size_t tm_perf_recent(const char *logdir, TmPerfSummary *out, size_t max);
/* Number of sessions in the summary file. */
size_t tm_perf_recent_count(const char *logdir);
/* Battery drain in %/hour, or -1 when not meaningful (charging, < 5 min). */
int tm_perf_drain_per_hour(const TmPerfSummary *s);
/* Deletes the performance files and the RetroArch log (never anything else). */
int tm_perf_clear(const char *logdir);

#endif
