/* Sistema oficial: makes the card usable by the stock TrimUI interface, so it
 * keeps working when TriMux is removed (or is tested before that).
 *
 * The stock firmware has no emulators of its own: its menu lists the folders
 * of Emus/ on the card (and /mnt/UDISK/Emus), each a config.json with the Roms
 * folder it reads ("rompath") and a launch.sh it runs with the game. A card
 * written with the TriMux image has none, and the stock card's own Emus
 * folder is not published by TrimUI. So for every Roms folder with games that
 * no stock emulator reads, TriMux can write one in that format, run by a copy
 * of its own RetroArch and cores (open source, licences included) kept in
 * Emus/_TriMux (no config.json there, so the stock menu does not list it).
 *
 * Rules: an existing emulator folder TriMux did not write is never changed;
 * TriMux's own ones carry a .trimux-stock marker and are refreshed. Saves go
 * to the same Saves/<platform> folders TriMux uses. Nothing in Roms, Saves,
 * Bios or Imgs is moved, renamed or deleted. */
#ifndef TRIMUX_STOCKPREP_H
#define TRIMUX_STOCKPREP_H

#include "catalog.h"
#include "ini.h"
#include "paths.h"

#define TM_STOCK_MAX 48
#define TM_STOCK_RUNTIME "Emus/_TriMux"
#define TM_STOCK_MARKER ".trimux-stock"
/* in the marker: emulators written with older files (icons) are refreshed */
#define TM_STOCK_FILES "files=2"

typedef enum {
    TM_STOCK_ORIGINAL = 0, /* read by an emulator TriMux did not write (the stock card's) */
    TM_STOCK_TRIMUX,       /* read by an emulator TriMux wrote, complete */
    TM_STOCK_MISSING,      /* no emulator yet; TriMux can write one */
    TM_STOCK_UNSUPPORTED,  /* no emulator and TriMux has none for it (unknown folder, ports) */
} TmStockState;

typedef struct {
    char dir[64];      /* Roms/<dir> */
    char sys_id[16];
    char sys_name[64];
    char emu_id[32];
    char emu_dir[80];  /* Emus/<name> that reads it (or will) */
    TmStockState state;
} TmStockItem;

typedef struct {
    TmStockItem items[TM_STOCK_MAX];
    int n;
    int original_emus;           /* emulators on the card/internal memory that TriMux did not write */
    int trimux_emus;
    unsigned long long need_bytes; /* still to copy for the missing/outdated ones */
} TmStockPlan;

int tm_stock_plan(const TmPaths *p, const TmCatalog *cat, const TmIni *settings, TmStockPlan *out);
/* Writes the missing emulators (and refreshes TriMux's own). 0 on success;
 * err gets an i18n key on failure ("stock.err.space", ...). */
int tm_stock_prepare(const TmPaths *p, const TmCatalog *cat, const TmIni *settings, char *err, size_t errsz);
/* Counts per state, for summaries. */
void tm_stock_counts(const TmStockPlan *plan, int counts[4]);
/* Writes a 300x300 PNG icon: a drawing of the platform's console on a
 * transparent background (consoleart.h); the stock menu shows the name. */
int tm_stock_icon(const char *path, const char *sys_id, unsigned rgb);

#endif
