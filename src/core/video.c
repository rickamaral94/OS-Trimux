#define _GNU_SOURCE
#include "video.h"
#include "log.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const TmVideoOption tm_video_aspects[] = {
    {"original", "video.aspect.original"},
    {"integer", "video.aspect.integer"},
    {"full", "video.aspect.full"},
};
const size_t tm_video_naspects = TM_ARRAY_LEN(tm_video_aspects);

const TmVideoOption tm_video_filters[] = {
    {"sharp", "video.filter.sharp"}, {"smooth", "video.filter.smooth"}, {"pixel", "video.filter.pixel"},
    {"lcd", "video.filter.lcd"},     {"crt", "video.filter.crt"},
};
const size_t tm_video_nfilters = TM_ARRAY_LEN(tm_video_filters);

/* Option names and values checked against each core's
 * libretro_core_options.h at the commits in sources/sources.lock. */
static const TmVideoCaps k_caps[] = {
    {.emu = "pcsx_rearmed",
     .res_key = "pcsx_rearmed_neon_enhancement_enable",
     .res = {{"disabled", "video.res.native"}, {"enabled", "video.res.2x"}},
     .nres = 2},
    {.emu = "prboom",
     .res_key = "prboom-resolution",
     .res = {{"320x200", "video.res.native"}, {"640x400", "video.res.2x"}, {"960x600", "video.res.3x"}},
     .nres = 3},
    {.emu = "tyrquake",
     .res_key = "tyrquake_resolution",
     .res = {{"320x240", "video.res.native"}, {"640x480", "video.res.2x"}, {"1024x768", "video.res.screen"}},
     .nres = 3},
    {.emu = "mupen64plus_next",
     .res_key = "mupen64plus-EnableNativeResFactor",
     .res = {{"0", "video.res.n64default"}, {"1", "video.res.native_light"}, {"2", "video.res.2x"}},
     .nres = 3},
    {.emu = "flycast",
     .res_key = "reicast_internal_resolution",
     .res = {{"640x480", "video.res.native"}, {"960x720", "video.res.1_5x"}, {"1280x960", "video.res.2x"}},
     .nres = 3},
    {.emu = "gambatte",
     .color_key = "gambatte_gbc_color_correction", .color_on = "GBC only", .color_off = "disabled", .color_def = 1,
     .ghost_key = "gambatte_mix_frames", .ghost_on = "lcd_ghosting_fast", .ghost_off = "disabled"},
    {.emu = "mgba",
     .color_key = "mgba_color_correction", .color_on = "Auto", .color_off = "OFF",
     .ghost_key = "mgba_interframe_blending", .ghost_on = "mix_smart", .ghost_off = "OFF"},
    {.emu = "gpsp",
     .color_key = "gpsp_color_correction", .color_on = "enabled", .color_off = "disabled",
     .ghost_key = "gpsp_frame_mixing", .ghost_on = "enabled", .ghost_off = "disabled"},
    {.emu = "genesis_plus_gx",
     .ghost_key = "genesis_plus_gx_lcd_filter", .ghost_on = "enabled", .ghost_off = "disabled"},
    {.emu = "handy", .ghost_key = "handy_lcd_ghosting", .ghost_on = "2frames", .ghost_off = "disabled"},
    {.emu = "fceumm", .hd_key = "fceumm_hdpacks"},
};

const TmVideoCaps *tm_video_caps(const char *emu_id)
{
    for (size_t i = 0; emu_id && i < TM_ARRAY_LEN(k_caps); i++)
        if (strcmp(k_caps[i].emu, emu_id) == 0)
            return &k_caps[i];
    return NULL;
}

int tm_video_index(const TmVideoOption *opts, size_t n, const char *id, int def)
{
    for (size_t i = 0; id && i < n; i++)
        if (strcmp(opts[i].id, id) == 0)
            return (int)i;
    return def;
}

const char *tm_video_shader(const char *filter)
{
    /* light GLSL presets from libretro/glsl-shaders, copied to
     * TriMux/retroarch/shaders by scripts/assemble_sdcard.sh */
    if (!filter)
        return NULL;
    if (strcmp(filter, "pixel") == 0)
        return "sharp-bilinear-simple.glslp";
    if (strcmp(filter, "lcd") == 0)
        return "zfast-lcd.glslp";
    if (strcmp(filter, "crt") == 0)
        return "zfast-crt.glslp";
    return NULL;
}

static int add_line(char *buf, size_t size, const char *key, const char *val)
{
    size_t len = strlen(buf);
    int n = snprintf(buf + len, size - len, "%s%s = \"%s\"", len ? "\n" : "", key, val);
    return n < 0 || (size_t)n >= size - len ? -1 : 0;
}

int tm_video_ra_lines(const TmIni *settings, const char *system, const char *shaders_dir, char *buf, size_t size,
                      char *shader, size_t shader_size)
{
    char sec[48];
    int rc = 0;
    shader[0] = '\0';
    if (tm_snprintf(sec, sizeof sec, "video.%s", system) != 0)
        return -1;
    const char *aspect = tm_ini_get(settings, sec, "aspect", NULL);
    if (aspect && tm_video_index(tm_video_aspects, tm_video_naspects, aspect, -1) >= 0) {
        /* RetroArch enum aspect_ratio: 22 = core provided, 24 = full */
        rc |= add_line(buf, size, "aspect_ratio_index", strcmp(aspect, "full") == 0 ? "24" : "22");
        rc |= add_line(buf, size, "video_scale_integer", strcmp(aspect, "integer") == 0 ? "true" : "false");
    }
    const char *filter = tm_ini_get(settings, sec, "filter", NULL);
    if (filter && tm_video_index(tm_video_filters, tm_video_nfilters, filter, -1) >= 0) {
        const char *preset = tm_video_shader(filter);
        char path[TM_PATH_MAX];
        if (preset && tm_path_join(path, sizeof path, shaders_dir, preset) == 0 && tm_file_exists(path) &&
            !strchr(path, '"')) {
            tm_strlcpy(shader, path, shader_size);
            rc |= add_line(buf, size, "video_shader_enable", "true");
            rc |= add_line(buf, size, "video_smooth", "false");
        } else {
            if (preset)
                LOGW("video: shader %s missing, using the plain picture", preset);
            rc |= add_line(buf, size, "video_shader_enable", "false");
            rc |= add_line(buf, size, "video_smooth", strcmp(filter, "smooth") == 0 ? "true" : "false");
        }
    }
    return rc;
}

/* Sets key = "value" in a RetroArch options file held in buf. */
static int opt_set(char **text, size_t *len, const char *key, const char *val)
{
    size_t klen = strlen(key);
    char line[256];
    int n = snprintf(line, sizeof line, "%s = \"%s\"\n", key, val);
    if (n < 0 || (size_t)n >= sizeof line)
        return -1;
    for (char *p = *text; p && *p;) {
        char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) + 1 : strlen(p);
        const char *q = p;
        while (*q == ' ' || *q == '\t')
            q++;
        if (strncmp(q, key, klen) == 0 && (q[klen] == ' ' || q[klen] == '=' || q[klen] == '\t')) {
            size_t off = (size_t)(p - *text), rest = *len - off - ll;
            char *nt = malloc(*len - ll + (size_t)n + 1);
            if (!nt)
                return -1;
            memcpy(nt, *text, off);
            memcpy(nt + off, line, (size_t)n);
            memcpy(nt + off + n, *text + off + ll, rest);
            *len = off + (size_t)n + rest;
            nt[*len] = '\0';
            free(*text);
            *text = nt;
            return 0;
        }
        p += ll;
    }
    int need_nl = *len > 0 && (*text)[*len - 1] != '\n';
    char *nt = realloc(*text, *len + (size_t)need_nl + (size_t)n + 1);
    if (!nt)
        return -1;
    if (need_nl)
        nt[(*len)++] = '\n';
    memcpy(nt + *len, line, (size_t)n + 1);
    *len += (size_t)n;
    *text = nt;
    return 0;
}

int tm_video_core_options(const TmIni *settings, const char *system, const TmVideoCaps *caps, const char *opt_path)
{
    if (!caps)
        return 0;
    char sec[48];
    if (tm_snprintf(sec, sizeof sec, "video.%s", system) != 0)
        return -1;
    const char *keys[4], *vals[4];
    int n = 0;
    long res = tm_ini_get_long(settings, sec, "res", -1);
    if (caps->res_key && res >= 0 && res < caps->nres) {
        keys[n] = caps->res_key;
        vals[n++] = caps->res[res].value;
    }
    long colors = tm_ini_get_long(settings, sec, "colors", -1);
    if (caps->color_key && colors >= 0) {
        keys[n] = caps->color_key;
        vals[n++] = colors ? caps->color_on : caps->color_off;
    }
    long ghost = tm_ini_get_long(settings, sec, "ghost", -1);
    if (caps->ghost_key && ghost >= 0) {
        keys[n] = caps->ghost_key;
        vals[n++] = ghost ? caps->ghost_on : caps->ghost_off;
    }
    long hd = tm_ini_get_long(settings, sec, "hdpacks", -1);
    if (caps->hd_key && hd >= 0) {
        keys[n] = caps->hd_key;
        vals[n++] = hd ? "enabled" : "disabled";
    }
    if (n == 0)
        return 0;
    size_t len = 0;
    char *text = tm_read_file(opt_path, 1u << 20, &len);
    if (!text) {
        text = calloc(1, 1);
        len = 0;
        if (!text)
            return -1;
    }
    int rc = 0;
    for (int i = 0; i < n && rc == 0; i++)
        rc = opt_set(&text, &len, keys[i], vals[i]);
    if (rc == 0) {
        char dir[TM_PATH_MAX];
        tm_strlcpy(dir, opt_path, sizeof dir);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            tm_mkdir_p(dir);
        }
        rc = tm_atomic_write(opt_path, text, len);
    }
    free(text);
    return rc;
}

int tm_video_count_hdpacks(const char *bios_dir)
{
    char root[TM_PATH_MAX];
    if (tm_path_join(root, sizeof root, bios_dir, "HdPacks") != 0)
        return 0;
    DIR *d = opendir(root);
    if (!d)
        return 0;
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char sub[TM_PATH_MAX], hires[TM_PATH_MAX];
        if (tm_path_join(sub, sizeof sub, root, e->d_name) == 0 &&
            tm_path_join(hires, sizeof hires, sub, "hires.txt") == 0 && tm_file_exists(hires))
            count++;
    }
    closedir(d);
    return count;
}
