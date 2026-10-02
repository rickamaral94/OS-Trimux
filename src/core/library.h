/* Game library: finds games in known folders, keeps a small on-card index so
 * the menu starts without rescanning, and never touches the game files. */
#ifndef TRIMUX_LIBRARY_H
#define TRIMUX_LIBRARY_H

#include "catalog.h"
#include <stddef.h>
#include <time.h>

#define TM_LIB_MAX_GAMES 30000
#define TM_LIB_MAX_DIRS 256

typedef struct {
    int system;    /* index into the catalog */
    char *relpath; /* path relative to the SD card root, e.g. "Roms/GBA/x.gba" */
    char *name;    /* display name */
    char *key;     /* folded name for search/sort */
} TmGame;

typedef struct {
    char relpath[256];
    long mtime;
} TmLibDir;

typedef struct {
    TmGame *games;
    size_t count, cap;
    TmLibDir dirs[TM_LIB_MAX_DIRS]; /* scanned top-level system folders */
    size_t ndirs;
    int truncated; /* hit TM_LIB_MAX_GAMES */
} TmLibrary;

void tm_library_init(TmLibrary *lib);
void tm_library_free(TmLibrary *lib);

/* Scans every known folder of every system under sd_root. */
int tm_library_scan(TmLibrary *lib, const TmCatalog *cat, const char *sd_root, int clean_names);
int tm_library_save(const TmLibrary *lib, const TmCatalog *cat, const char *path);
/* Loads an index. Returns 0 ok, -1 missing/corrupt. */
int tm_library_load(TmLibrary *lib, const TmCatalog *cat, const char *path);
/* 1 if a scanned folder changed or a new known folder appeared. */
int tm_library_is_stale(const TmLibrary *lib, const TmCatalog *cat, const char *sd_root);

/* Folder candidates of a system that exist on the card ("Roms/GBA" ...). */
size_t tm_library_system_dirs(const TmSystem *sys, const char *sd_root, char out[][256], size_t max);
/* Creates Roms/<first folder> for every system. Never deletes. Returns created count. */
int tm_library_create_default_dirs(const TmCatalog *cat, const char *sd_root);

size_t tm_library_count_system(const TmLibrary *lib, int system);
long tm_library_find(const TmLibrary *lib, const char *relpath);

/* Display name from a file name: strips extension and trailing (...)/[...] tags,
 * except disc markers. */
void tm_clean_name(const char *filename, int clean, char *out, size_t size);
/* Case/accent-insensitive substring match against the game's key. */
int tm_game_matches(const TmGame *g, const char *folded_query);

void tm_library_sort(TmLibrary *lib);

#endif
