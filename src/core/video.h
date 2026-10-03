/* Image settings per platform, chosen in the menu (Configurações ›
 * Emuladores › <plataforma> › Imagem) and kept in trimux.ini under
 * [video.<SYSTEM>]. At launch they become RetroArch lines in the RAM append
 * config, a shader preset (--set-shader) and a few core options.
 *
 * Keys (all optional; a missing key changes nothing, so RetroArch keeps
 * whatever the user set there):
 *   aspect  = original | integer | full
 *   filter  = sharp | smooth | pixel | lcd | crt
 *   res     = index into the emulator's internal resolutions (0 = native)
 *   colors  = 0 | 1   colours as on the original handheld's screen
 *   ghost   = 0 | 1   LCD ghosting (needed for transparency in some games)
 *   hdpacks = 0 | 1   HD texture packs (Bios/HdPacks/<rom name>/hires.txt) */
#ifndef TRIMUX_VIDEO_H
#define TRIMUX_VIDEO_H

#include "ini.h"

#include <stddef.h>

#define TM_VIDEO_MAX_RES 4

typedef struct {
    const char *value;     /* core option value */
    const char *label_key; /* i18n key */
} TmVideoChoice;

/* What an emulator offers beyond RetroArch's own scaling and shaders. */
typedef struct {
    const char *emu;
    const char *res_key; /* internal resolution option, NULL if none */
    TmVideoChoice res[TM_VIDEO_MAX_RES];
    int nres;
    const char *color_key, *color_on, *color_off;
    int color_def; /* the core's own default (1 = on) */
    const char *ghost_key, *ghost_on, *ghost_off;
    const char *hd_key; /* HD texture packs: "enabled" / "disabled" */
} TmVideoCaps;

typedef struct {
    const char *id;        /* value stored in trimux.ini */
    const char *label_key; /* i18n key */
} TmVideoOption;

extern const TmVideoOption tm_video_aspects[];
extern const size_t tm_video_naspects;
extern const TmVideoOption tm_video_filters[];
extern const size_t tm_video_nfilters;

/* NULL when the emulator has nothing extra (2D cores without options). */
const TmVideoCaps *tm_video_caps(const char *emu_id);
/* Index of the stored value in the list, or def when unset/unknown. */
int tm_video_index(const TmVideoOption *opts, size_t n, const char *id, int def);
/* Shader preset file (relative to the shaders folder) for a filter, or NULL. */
const char *tm_video_shader(const char *filter);

/* RetroArch lines for [video.<system>] ("key = \"value\"\n"...), appended to
 * buf (NUL-terminated; buf must start as a valid string). shader receives the
 * absolute preset path, or "" when no shader is wanted or it is missing. */
int tm_video_ra_lines(const TmIni *settings, const char *system, const char *shaders_dir, char *buf, size_t size,
                      char *shader, size_t shader_size);
/* Puts the chosen core options into RetroArch's per-core options file (only
 * the keys set in TriMux; every other line is kept). 0 also when there was
 * nothing to do. */
int tm_video_core_options(const TmIni *settings, const char *system, const TmVideoCaps *caps, const char *opt_path);
/* Number of HD packs in <bios>/HdPacks (folders with a hires.txt). */
int tm_video_count_hdpacks(const char *bios_dir);

#endif
