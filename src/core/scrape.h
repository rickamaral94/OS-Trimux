/* Game covers from libretro-thumbnails (thumbnails.libretro.com).
 *
 * Free, no account. Images are matched by name: the ROM file name (No-Intro
 * style, e.g. "Celeste Classic (World).gba") must match the thumbnail name.
 * Arcade zips (mslug.zip) are translated to their titles with a list built
 * from the FinalBurn Neo DAT. Covers are saved, shrunk, as
 * Imgs/<rom folder>/<rom name>.png, the folder the stock TrimUI UI reads too.
 * Downloads use the firmware's curl over HTTPS with the CA bundle shipped on
 * the card; nothing is downloaded unless the user asks (or turns on automatic
 * covers). */
#ifndef TRIMUX_SCRAPE_H
#define TRIMUX_SCRAPE_H

#include "catalog.h"
#include "library.h"
#include "paths.h"

#include <stddef.h>

#define TM_COVER_MAX_W 480
#define TM_COVER_MAX_H 480

typedef enum { TM_THUMB_BOXART = 0, TM_THUMB_SNAP, TM_THUMB_TITLE } TmThumbKind;

/* ---- pure helpers (unit tested) ---- */

/* libretro-thumbnails file name rule: the characters & * / : ` < > ? \ | and
 * the double quote become an underscore. */
void tm_scrape_sanitize(const char *in, char *out, size_t size);
int tm_scrape_url(const char *repo, TmThumbKind kind, const char *name, char *out, size_t size);
/* Names to try for a ROM base name, most likely first. */
size_t tm_scrape_candidates(const char *base, char out[][256], size_t max);
/* <sd>/Imgs/<folder under Roms>/<file name without extension>.png */
int tm_scrape_cover_path(const char *sd, const char *relpath, const char *system_id, char *out, size_t size);
TmThumbKind tm_thumb_kind_parse(const char *s);
const char *tm_thumb_kind_id(TmThumbKind k);

/* Arcade short name -> title, from share/arcade-names.tsv ("name\ttitle"). */
typedef struct {
    char *data;
    char **names; /* sorted pointers into data: "short\0title\0" */
    size_t count;
} TmArcadeNames;
int tm_arcade_load(TmArcadeNames *a, const char *path);
const char *tm_arcade_title(const TmArcadeNames *a, const char *short_name);
void tm_arcade_free(TmArcadeNames *a);

/* ---- status shared with the menu (/tmp/trimux/scrape.status) ---- */

typedef struct {
    char state[16]; /* running, done, stopped, nowifi, network, error, interrupted */
    int done, total, found, missing;
} TmScrapeStatus;

int tm_scrape_status_read(const TmPaths *p, TmScrapeStatus *st);
int tm_scrape_running(const TmPaths *p);
/* Asks a running scraper to stop after the current image. */
void tm_scrape_request_stop(const TmPaths *p);

/* ---- the scraper itself (trimuxctl scrape) ---- */

typedef struct {
    TmThumbKind kind;
    int retry_missing; /* also try games that were not found before */
    int wait_wifi_s;   /* wait this long for a Wi-Fi connection */
    int autorun;       /* started by itself (boot, after a rescan), not by the user */
} TmScrapeOptions;

/* While downloading, TriMuxData/state/scrape_active exists on the card. If
 * an automatic run finds it, the previous run never finished (the device
 * restarted or lost power): it does not start again by itself until the next
 * boot, so a crash while downloading can never become a restart loop. The
 * state is then "interrupted"; a manual download always runs. */

/* Returns 0 when it ran to the end (even if some covers were not found). */
int tm_scrape_run(const TmPaths *p, const TmCatalog *cat, const TmLibrary *lib, const TmScrapeOptions *o);

#endif
