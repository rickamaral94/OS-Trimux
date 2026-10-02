/* Card layout. Everything is relative to the SD card root so the same code
 * runs on the device and in tests (TRIMUX_SDCARD / TRIMUX_TMP override). */
#ifndef TRIMUX_PATHS_H
#define TRIMUX_PATHS_H

#include "ini.h"
#include "util.h"

typedef struct {
    char sd[TM_PATH_MAX];        /* /mnt/SDCARD */
    char sys[TM_PATH_MAX];       /* TriMux       (replaced on update) */
    char data[TM_PATH_MAX];      /* TriMuxData   (user data, never replaced) */
    char share[TM_PATH_MAX];     /* TriMux/share */
    char cores[TM_PATH_MAX];     /* TriMux/retroarch/cores */
    char retroarch[TM_PATH_MAX]; /* TriMux/retroarch */
    char config[TM_PATH_MAX];    /* TriMuxData/config/trimux.ini */
    char overrides[TM_PATH_MAX]; /* TriMuxData/config/overrides.ini */
    char favorites[TM_PATH_MAX];
    char recents[TM_PATH_MAX];
    char library[TM_PATH_MAX];
    char logdir[TM_PATH_MAX];
    char state[TM_PATH_MAX];
    char ra_home[TM_PATH_MAX];   /* TriMuxData/retroarch */
    char tmp[TM_PATH_MAX];       /* /tmp/trimux (RAM) */
} TmPaths;

int tm_paths_init(TmPaths *p);
/* Creates the user-data folders (never touches games/saves). */
int tm_paths_ensure(const TmPaths *p);

#define TM_SETTINGS_VERSION 1

/* Loads settings, filling defaults. Migrates old versions after taking a
 * backup copy (trimux.ini.bak-v<N>). Returns 1 if the file was created. */
int tm_settings_load(TmIni *ini, const TmPaths *p);
int tm_settings_save(const TmIni *ini, const TmPaths *p);

#endif
