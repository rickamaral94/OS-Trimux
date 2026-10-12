/* Uninstall: hands the card back to the stock TrimUI interface.
 *
 * The stock firmware runs the interface of the card's trimui/ folder when it
 * exists (that is how TriMux starts) and its own one otherwise. It has no
 * emulators of its own: the stock menu reads them from Emus/<name>/config.json
 * on the card (or /mnt/UDISK/Emus), each with the Roms folder it lists
 * ("rompath"). So the check tells, before anything is removed:
 *   - how many stock emulators the card has;
 *   - which Roms folders with games no stock emulator reads.
 * Removing takes only TriMux's own folders: trimui/ first (the start hook),
 * then TriMux.old/, trimui.old/ and TriMux/. Games, saves, states, BIOS,
 * covers, apps, Emus/ and TriMuxData/ stay. Nothing is moved or renamed. */
#ifndef TRIMUX_UNINSTALL_H
#define TRIMUX_UNINSTALL_H

#include "paths.h"

#define TM_UNINSTALL_MAX_DIRS 48

typedef struct {
    int stock_emus;                                  /* stock emulators found */
    int game_dirs;                                   /* Roms folders with games */
    int nunread;                                     /* of those, read by no stock emulator */
    char unread[TM_UNINSTALL_MAX_DIRS][64];
    int nread;
    char read[TM_UNINSTALL_MAX_DIRS][64];            /* Roms folders the stock emulators read */
} TmUninstallCheck;

int tm_uninstall_check(const TmPaths *p, TmUninstallCheck *out);
/* 1 when a Roms folder holds a game-like file (licence/readme files and
 * hidden files do not count), up to depth sub-folder levels down. */
int tm_roms_dir_has_games(const char *dir, int depth);
/* Roms folder (first component after "Roms/") a stock rompath points at,
 * resolved from the emulator folder emu_rel ("Emus/FC"); 0 when found. */
int tm_uninstall_rompath_dir(const char *emu_rel, const char *rompath, char *out, size_t size);
/* Removes TriMux's folders from the card; 0 when all of them are gone. */
int tm_uninstall_run(const TmPaths *p);

#endif
