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
/* Font for characters the menu font lacks (CJK names); up to two, the first
 * one that loads is used. Opened only when such a character is drawn. */
void gfx_add_fallback_font(const char *path);
int gfx_w(void);
int gfx_h(void);
void gfx_set_theme(const char *name);
const TmTheme *gfx_theme(void);

void gfx_clear(void);
void gfx_present(void);
void gfx_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_rect_a(int x, int y, int w, int h, uint32_t rgb, uint8_t alpha);
void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t rgb);
void gfx_round_rect_a(int x, int y, int w, int h, int r, uint32_t rgb, uint8_t alpha);
/* Colour pct% of the way from a to b. */
uint32_t gfx_mix(uint32_t a, uint32_t b, int pct);
/* Vertical gradient (colour and alpha from top to bottom). */
void gfx_gradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom, uint8_t a_top, uint8_t a_bottom);
/* Soft shadow around a rounded box (draw it before the box). */
void gfx_shadow(int x, int y, int w, int h, int r, int spread);
/* Wi-Fi signal: 4 bars, the first `bars` lit (0-4). Width = h * 1.3. */
void gfx_wifi(int x, int y, int h, int bars, uint32_t on, uint32_t off);
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

/* Draws a PNG/JPEG scaled to fit max_w x max_h, horizontally centred in the
 * box. Returns the drawn height, or 0 when there is no (valid) image. Small
 * cache; a missing file is looked up again after a few seconds, so covers
 * downloaded in the background show up. */
int gfx_image(const char *path, int x, int y, int max_w, int max_h);
/* Same, reporting the drawn size. Returns 1 drawn, 0 no image, -1 not loaded
 * yet (only a couple of new images are decoded per frame; see
 * gfx_image_pending). */
int gfx_image_box(const char *path, int x, int y, int max_w, int max_h, int *out_w, int *out_h);
/* 1 when an image was put off this frame: draw again soon. */
int gfx_image_pending(void);
/* New images decoded per frame (0 = no limit, for screenshots and tests). */
void gfx_set_load_budget(int n);

/* Saves the current frame as BMP (host screenshots for documentation). */
int gfx_screenshot(const char *path);

#endif
