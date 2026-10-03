/* Platform (system) and emulator registry, loaded from share/systems.ini and
 * share/emulators.ini so new platforms or cores need no code change. */
#ifndef TRIMUX_CATALOG_H
#define TRIMUX_CATALOG_H

#include <stddef.h>

#define TM_MAX_FOLDERS 12
#define TM_MAX_EXTS 24
#define TM_MAX_BIOS 8
#define TM_MAX_SYS_EMUS 6

typedef struct {
    char id[16];
    char name[64];
    char short_name[16];
    char folders[TM_MAX_FOLDERS][64];
    int nfolders;
    char exts[TM_MAX_EXTS][8];
    int nexts;
    char bios[TM_MAX_BIOS][64];
    int nbios;
    int bios_required;
    char emulators[TM_MAX_SYS_EMUS][32];
    int nemus;
    unsigned color;   /* 0xRRGGBB badge color */
    int experimental; /* shown with a warning (e.g. ports) */
    int max_depth;    /* sub-folder levels scanned (1 = top level only) */
    char note_key[48];
    char thumbs[160]; /* libretro-thumbnails repositories, '|' separated */
    char popular[16]; /* share/popular/<name>.txt ranking (default: the id) */
} TmSystem;

typedef struct {
    char id[32];
    char name[64];
    char type[16];        /* "retroarch", or "script" (game is a shell script, e.g. ports) */
    char core[64];        /* file name inside the cores directory (retroarch only) */
    char config_name[64]; /* RetroArch library name (config/<name>/) */
    char profile[16];     /* recommended power profile id */
    int experimental;
    char note_key[48];
    char system_dir[64];  /* RetroArch system folder relative to the card, when not Bios/ (files the
                           * core needs that ship with TriMux, so online updates refresh them) */
} TmEmulator;

typedef struct {
    TmSystem *systems;
    size_t nsystems;
    TmEmulator *emus;
    size_t nemus;
} TmCatalog;

int tm_catalog_load(TmCatalog *cat, const char *systems_ini, const char *emulators_ini);
void tm_catalog_free(TmCatalog *cat);

const TmSystem *tm_catalog_system(const TmCatalog *cat, const char *id);
long tm_catalog_system_index(const TmCatalog *cat, const char *id);
const TmEmulator *tm_catalog_emulator(const TmCatalog *cat, const char *id);
int tm_system_has_ext(const TmSystem *sys, const char *filename);
int tm_system_supports_emu(const TmSystem *sys, const char *emu_id);

/* 1 if the emulator's core file exists in cores_dir. */
/* PortMaster's launcher (store install), relative to the card. */
#define TM_PORTMASTER_LAUNCH "Emus/tg5040/PORTS.pak/launch.sh"
int tm_portmaster_launcher(const char *cores_dir, char *out, size_t size);
/* 1 if a Ports script is a PortMaster port (or the PortMaster entry itself). */
int tm_port_is_portmaster(const char *script_path);
int tm_emulator_available(const TmEmulator *emu, const char *cores_dir);

/* Chooses the emulator for a game: per-game override, then platform
 * preference, then the first available emulator listed for the system.
 * Invalid or unavailable choices are skipped. Returns NULL if none. */
const TmEmulator *tm_catalog_resolve(const TmCatalog *cat, const TmSystem *sys,
                                     const char *game_override, const char *platform_pref,
                                     const char *cores_dir);

#endif
