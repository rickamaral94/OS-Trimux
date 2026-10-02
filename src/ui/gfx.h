/* Drawing layer: SDL2 renderer + stb_truetype glyph cache. Only libSDL2 is
 * required at runtime (the firmware ships it in /usr/trimui/lib). */
#ifndef TRIMUX_GFX_H
#define TRIMUX_GFX_H

#include <SDL.h>
#include <stdint.h>

typedef struct {
    uint32_t bg, panel, panel2, text, dim, accent, accent_text, warn, ok, danger, sel;
} TmTheme;

enum { FONT_S = 0, FONT_M, FONT_L, FONT_XL, FONT_COUNT };
enum { ALIGN_LEFT = 0, ALIGN_CENTER, ALIGN_RIGHT };

int gfx_init(const char *font_path, const char *fallback_font, int want_w, int want_h);
void gfx_quit(void);
int gfx_w(void);
int gfx_h(void);
void gfx_set_theme(const char *name);
const TmTheme *gfx_theme(void);

void gfx_clear(void);
void gfx_present(void);
void gfx_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_rect_a(int x, int y, int w, int h, uint32_t rgb, uint8_t alpha);
void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t rgb);
void gfx_frame(int x, int y, int w, int h, int t, uint32_t rgb);
void gfx_star(int cx, int cy, int r, uint32_t rgb);
void gfx_battery(int x, int y, int h, int pct, int charging);

/* Text: UTF-8. Returns the drawn width. max_w > 0 ellipsizes. */
int gfx_text(int font, int x, int y, uint32_t rgb, int align, int max_w, const char *s);
int gfx_text_width(int font, const char *s);
int gfx_font_height(int font);
/* Wraps text inside a box; returns the height used. */
int gfx_text_wrap(int font, int x, int y, int w, int max_lines, uint32_t rgb, const char *s);
/* Badge with platform colour and short name. Returns width. */
int gfx_badge(int x, int y, int h, uint32_t rgb, const char *label);

/* Saves the current frame as BMP (host screenshots for documentation). */
int gfx_screenshot(const char *path);

#endif
