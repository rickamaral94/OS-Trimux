/* Game launch handoff between the menu and the launcher.
 *
 * The menu never builds shell commands. It writes a tiny INI file in RAM
 * (/tmp/trimux/launch.ini) with the system id, the ROM path relative to the
 * card and the emulator id; the launcher re-validates everything before
 * executing RetroArch directly with execv (no shell). */
#ifndef TRIMUX_LAUNCH_H
#define TRIMUX_LAUNCH_H

#include "catalog.h"
#include "paths.h"

typedef struct {
    const TmSystem *system;
    const TmEmulator *emu;
    char rom_rel[TM_PATH_MAX];
    char rom_abs[TM_PATH_MAX];
    char core_abs[TM_PATH_MAX];
} TmLaunch;

int tm_launch_file(const TmPaths *p, char *out, size_t size);
int tm_launch_write(const TmPaths *p, const char *system, const char *rom_rel, const char *emu);
/* Reads and validates. err receives an i18n key on failure. */
int tm_launch_read(const TmPaths *p, const TmCatalog *cat, TmLaunch *l, char *err, size_t errsz);
/* Writes the per-launch RetroArch config fragment (directories, plus the
 * optional extra "key = value" lines) into tmp. */
int tm_launch_write_ra_append(const TmPaths *p, const TmLaunch *l, const char *extra, char *out_path, size_t size);
/* RetroArch user_language value for a TriMux language code (0 = English). */
int tm_ra_language(const char *code);

/* Thermal guard state machine (pure, unit tested). */
typedef struct {
    long hot_mc, cool_mc; /* thresholds in milli-degrees C */
    int samples;          /* consecutive samples needed */
    int hot_count, cool_count;
    int throttled;
} TmThermalGuard;

enum { TM_THERMAL_NONE = 0, TM_THERMAL_THROTTLE = 1, TM_THERMAL_RESTORE = 2 };

void tm_thermal_init(TmThermalGuard *g, long hot_mc, long cool_mc, int samples);
/* temp_mc < 0 (sensor error) never triggers a restore. */
int tm_thermal_step(TmThermalGuard *g, long temp_mc);

#endif
