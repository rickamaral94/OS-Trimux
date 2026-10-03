/* TriMux's own tools (Aplicativos › Ferramentas): play statistics, a random
 * game, card cleanup and the protected folders shared by the file manager
 * and the browser file server. */
#ifndef TRIMUX_TOOLS_H
#define TRIMUX_TOOLS_H

#include "catalog.h"
#include "ini.h"
#include "library.h"
#include "paths.h"

#include <stddef.h>
#include <stdint.h>

/* ---- play statistics (from TriMuxData/state/plays.ini) ---- */

#define TM_STATS_TOP 10
#define TM_STATS_SYSTEMS 64

typedef struct {
    long game; /* library index */
    long times, seconds, last;
} TmStatGame;

typedef struct {
    int system;
    long seconds, times;
    int games;
} TmStatSystem;

typedef struct {
    long seconds, times;
    int games;                    /* games played at least once */
    TmStatGame top[TM_STATS_TOP]; /* most time first */
    size_t ntop;
    TmStatGame last;              /* most recently played (game -1: none) */
    TmStatSystem systems[TM_STATS_SYSTEMS]; /* most time first */
    size_t nsystems;
} TmStats;

/* Only games still in the library are counted. */
void tm_stats_compute(const TmIni *plays, const TmLibrary *lib, TmStats *out);
/* "3 h 20 min", "12 min", "45 s" */
void tm_format_duration(long seconds, char *out, size_t size);

/* ---- random game ---- */

/* A random library game of system (-1: any). unplayed: only games never
 * played, falling back to any game when all were played. -1 if none. */
long tm_random_game(const TmLibrary *lib, const TmIni *plays, int system, int unplayed, unsigned *seed);

/* ---- protected folders ---- */

/* 1 if relpath (relative to the card root) is TriMux itself, its update or
 * rollback copies, or the official system's folder: never written or deleted
 * by the file tools. */
int tm_path_protected(const char *relpath);

/* ---- card cleanup ---- */

enum {
    TM_CLEAN_COMPUTER = 0, /* ._*, .DS_Store, Thumbs.db, .Trashes, .Spotlight-V100... */
    TM_CLEAN_LOGS,         /* old TriMux and RetroArch logs, performance logs */
    TM_CLEAN_TEMP,         /* leftovers of interrupted updates and app installs */
    TM_CLEAN_N
};

#define TM_CLEAN_SAMPLES 4

typedef struct {
    int files;
    uint64_t bytes;
    char sample[TM_CLEAN_SAMPLES][160]; /* first relpaths found */
    int nsample;
} TmCleanCat;

typedef struct {
    TmCleanCat cat[TM_CLEAN_N];
} TmCleanReport;

const char *tm_clean_id(int cat);
int tm_clean_parse(const char *id);
/* Finds what each category would remove (nothing is changed). */
int tm_clean_scan(const TmPaths *p, TmCleanReport *out);
/* Removes the categories in mask (bit = 1 << TM_CLEAN_*); done lists what
 * was removed. Games, saves, BIOS, covers and settings are never touched. */
int tm_clean_run(const TmPaths *p, unsigned mask, TmCleanReport *done);

#endif
