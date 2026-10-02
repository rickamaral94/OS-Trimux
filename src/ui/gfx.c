#define _GNU_SOURCE
#include "gfx.h"
#include "../core/image.h"
#include "../core/log.h"
#include "../core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/stb_truetype.h"
#pragma GCC diagnostic pop

#define ATLAS 1024
#define GLYPH_BUCKETS 512

typedef struct Glyph {
    uint32_t cp;
    int x, y, w, h, xoff, yoff, adv;
    struct Glyph *next;
} Glyph;

typedef struct {
    int px;
    float scale;
    int ascent, line_h;
    SDL_Texture *atlas;
    int pen_x, pen_y, row_h;
    Glyph *buckets[GLYPH_BUCKETS];
} Font;

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static int g_w, g_h;
static unsigned char *g_ttf;
static stbtt_fontinfo g_info;
static Font g_fonts[FONT_COUNT];
static TmTheme g_theme;

static const TmTheme k_dark = {0x0f1318, 0x18202a, 0x222c38, 0xeef2f5, 0x8d9aa8, 0x2e86de, 0xffffff,
                               0xf0a500, 0x27ae60, 0xe74c3c, 0x2e86de};
static const TmTheme k_light = {0xeef1f5, 0xffffff, 0xe1e6ec, 0x17202a, 0x5d6b7a, 0x1f6fb2, 0xffffff,
                                0xb9770e, 0x1e8449, 0xc0392b, 0x1f6fb2};
static const TmTheme k_contrast = {0x000000, 0x000000, 0x1a1a1a, 0xffffff, 0xd0d0d0, 0xffd400, 0x000000,
                                   0xffd400, 0x00ff66, 0xff4444, 0xffd400};

void gfx_set_theme(const char *name)
{
    if (name && strcmp(name, "light") == 0)
        g_theme = k_light;
    else if (name && strcmp(name, "contrast") == 0)
        g_theme = k_contrast;
    else
        g_theme = k_dark;
}

const TmTheme *gfx_theme(void) { return &g_theme; }
int gfx_w(void) { return g_w; }
int gfx_h(void) { return g_h; }

static void set_color(uint32_t rgb, uint8_t a)
{
    SDL_SetRenderDrawColor(g_ren, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, a);
}

static int font_init(Font *f, int px)
{
    memset(f, 0, sizeof *f);
    f->px = px;
    f->scale = stbtt_ScaleForPixelHeight(&g_info, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_info, &asc, &desc, &gap);
    f->ascent = (int)lroundf(asc * f->scale);
    f->line_h = (int)lroundf((asc - desc + gap) * f->scale);
    f->atlas = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ATLAS, ATLAS);
    if (!f->atlas)
        return -1;
    SDL_SetTextureBlendMode(f->atlas, SDL_BLENDMODE_BLEND);
    return 0;
}

static void font_reset(Font *f)
{
    for (int i = 0; i < GLYPH_BUCKETS; i++) {
        Glyph *g = f->buckets[i];
        while (g) {
            Glyph *n = g->next;
            free(g);
            g = n;
        }
        f->buckets[i] = NULL;
    }
    f->pen_x = f->pen_y = f->row_h = 0;
}

static Glyph *glyph_get(Font *f, uint32_t cp)
{
    unsigned b = cp % GLYPH_BUCKETS;
    for (Glyph *g = f->buckets[b]; g; g = g->next)
        if (g->cp == cp)
            return g;
    int adv, lsb, x0, y0, x1, y1;
    int idx = stbtt_FindGlyphIndex(&g_info, (int)cp);
    if (idx == 0 && cp != '?')
        return glyph_get(f, '?');
    stbtt_GetGlyphHMetrics(&g_info, idx, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(&g_info, idx, f->scale, f->scale, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    if (w > ATLAS / 4 || h > ATLAS / 4)
        w = h = 0;
    if (f->pen_x + w + 1 > ATLAS) {
        f->pen_x = 0;
        f->pen_y += f->row_h + 1;
        f->row_h = 0;
    }
    if (f->pen_y + h + 1 > ATLAS) {
        font_reset(f); /* atlas full: start over (rare) */
        b = cp % GLYPH_BUCKETS;
    }
    Glyph *g = calloc(1, sizeof *g);
    if (!g)
        return NULL;
    g->cp = cp;
    g->w = w;
    g->h = h;
    g->xoff = x0;
    g->yoff = y0;
    g->adv = (int)lroundf(adv * f->scale);
    g->x = f->pen_x;
    g->y = f->pen_y;
    if (w > 0 && h > 0) {
        unsigned char *mono = malloc((size_t)w * h);
        uint32_t *rgba = malloc((size_t)w * h * 4);
        if (mono && rgba) {
            stbtt_MakeGlyphBitmap(&g_info, mono, w, h, w, f->scale, f->scale, idx);
            for (int i = 0; i < w * h; i++)
                rgba[i] = ((uint32_t)mono[i] << 24) | 0x00ffffffu;
            SDL_Rect r = {g->x, g->y, w, h};
            SDL_UpdateTexture(f->atlas, &r, rgba, w * 4);
        }
        free(mono);
        free(rgba);
    }
    f->pen_x += w + 1;
    if (h > f->row_h)
        f->row_h = h;
    g->next = f->buckets[b];
    f->buckets[b] = g;
    return g;
}

static uint32_t utf8_next(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    uint32_t cp;
    int n;
    if (s[0] < 0x80) {
        cp = s[0];
        n = 1;
    } else if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x1f) << 6) | (s[1] & 0x3f);
        n = 2;
    } else if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x0f) << 12) | ((uint32_t)(s[1] & 0x3f) << 6) | (s[2] & 0x3f);
        n = 3;
    } else if ((s[0] & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3f) << 12) | ((uint32_t)(s[2] & 0x3f) << 6) |
             (s[3] & 0x3f);
        n = 4;
    } else {
        cp = '?';
        n = 1; /* invalid byte: show a replacement, keep going */
    }
    *ps += n;
    return cp;
}

int gfx_init(const char *font_path, const char *fallback_font, int want_w, int want_h)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0) {
        LOGE("SDL_Init: %s", SDL_GetError());
        return -1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    Uint32 flags = SDL_WINDOW_SHOWN;
    if (want_w <= 0)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    g_win = SDL_CreateWindow("TriMux", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             want_w > 0 ? want_w : 1024, want_h > 0 ? want_h : 768, flags);
    if (!g_win) {
        LOGE("SDL_CreateWindow: %s", SDL_GetError());
        return -1;
    }
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren)
        g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) {
        LOGE("SDL_CreateRenderer: %s", SDL_GetError());
        return -1;
    }
    SDL_GetRendererOutputSize(g_ren, &g_w, &g_h);
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    SDL_ShowCursor(SDL_DISABLE);
    size_t len = 0;
    g_ttf = (unsigned char *)tm_read_file(font_path, 32u << 20, &len);
    if (!g_ttf && fallback_font)
        g_ttf = (unsigned char *)tm_read_file(fallback_font, 32u << 20, &len);
    if (!g_ttf || !stbtt_InitFont(&g_info, g_ttf, stbtt_GetFontOffsetForIndex(g_ttf, 0))) {
        LOGE("font: cannot load %s", font_path);
        return -1;
    }
    float s = g_h / 768.0f;
    if (s < 0.5f)
        s = 0.5f;
    const int sizes[FONT_COUNT] = {22, 28, 36, 52};
    for (int i = 0; i < FONT_COUNT; i++)
        if (font_init(&g_fonts[i], (int)lroundf(sizes[i] * s)) != 0)
            return -1;
    SDL_RendererInfo ri;
    if (SDL_GetRendererInfo(g_ren, &ri) == 0)
        LOGI("ui: %dx%d renderer=%s video=%s", g_w, g_h, ri.name, SDL_GetCurrentVideoDriver());
    if (!g_theme.text)
        gfx_set_theme("dark");
    return 0;
}


/* ------------------------------------------------------------ images (covers) */

#define IMG_CACHE 6
static struct {
    char path[1024];
    SDL_Texture *tex; /* NULL: file missing or unreadable */
    int w, h;
    uint32_t checked; /* SDL ticks of the last lookup */
    uint32_t used;
} g_img[IMG_CACHE];

static void img_drop(int i)
{
    if (g_img[i].tex)
        SDL_DestroyTexture(g_img[i].tex);
    memset(&g_img[i], 0, sizeof g_img[i]);
}

int gfx_image(const char *path, int x, int y, int max_w, int max_h)
{
    if (!g_ren || !path || !path[0] || max_w <= 0 || max_h <= 0)
        return 0;
    uint32_t now = SDL_GetTicks();
    int slot = -1, lru = 0;
    for (int i = 0; i < IMG_CACHE; i++) {
        if (strcmp(g_img[i].path, path) == 0)
            slot = i;
        if (g_img[i].used < g_img[lru].used)
            lru = i;
    }
    if (slot >= 0 && !g_img[slot].tex && now - g_img[slot].checked > 5000) {
        img_drop(slot); /* look for a missing file again */
        slot = -1;
    }
    if (slot < 0) {
        slot = lru;
        img_drop(slot);
        snprintf(g_img[slot].path, sizeof g_img[slot].path, "%s", path);
        g_img[slot].checked = now;
        int w, h;
        unsigned char *px = tm_image_load_rgba(path, &w, &h);
        if (px) {
            SDL_Texture *t = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
            if (t && SDL_UpdateTexture(t, NULL, px, w * 4) == 0) {
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                g_img[slot].tex = t;
                g_img[slot].w = w;
                g_img[slot].h = h;
            } else if (t) {
                SDL_DestroyTexture(t);
            }
            tm_image_free(px);
        }
    }
    g_img[slot].used = now ? now : 1;
    if (!g_img[slot].tex)
        return 0;
    double s = (double)max_w / g_img[slot].w;
    if ((double)max_h / g_img[slot].h < s)
        s = (double)max_h / g_img[slot].h;
    if (s > 2.0)
        s = 2.0; /* small images are not blown up into a blur */
    SDL_Rect r = {0, y, (int)(g_img[slot].w * s), (int)(g_img[slot].h * s)};
    r.x = x + (max_w - r.w) / 2;
    SDL_RenderCopy(g_ren, g_img[slot].tex, NULL, &r);
    return r.h;
}

void gfx_quit(void)
{
    for (int i = 0; i < IMG_CACHE; i++)
        img_drop(i);
    for (int i = 0; i < FONT_COUNT; i++) {
        font_reset(&g_fonts[i]);
        if (g_fonts[i].atlas)
            SDL_DestroyTexture(g_fonts[i].atlas);
    }
    free(g_ttf);
    g_ttf = NULL;
    if (g_ren)
        SDL_DestroyRenderer(g_ren);
    if (g_win)
        SDL_DestroyWindow(g_win);
    g_ren = NULL;
    g_win = NULL;
    SDL_Quit();
}

void gfx_clear(void)
{
    set_color(g_theme.bg, 255);
    SDL_RenderClear(g_ren);
}

void gfx_present(void) { SDL_RenderPresent(g_ren); }

void gfx_rect(int x, int y, int w, int h, uint32_t rgb)
{
    SDL_Rect r = {x, y, w, h};
    set_color(rgb, 255);
    SDL_RenderFillRect(g_ren, &r);
}

void gfx_rect_a(int x, int y, int w, int h, uint32_t rgb, uint8_t a)
{
    SDL_Rect r = {x, y, w, h};
    set_color(rgb, a);
    SDL_RenderFillRect(g_ren, &r);
}

void gfx_frame(int x, int y, int w, int h, int t, uint32_t rgb)
{
    gfx_rect(x, y, w, t, rgb);
    gfx_rect(x, y + h - t, w, t, rgb);
    gfx_rect(x, y, t, h, rgb);
    gfx_rect(x + w - t, y, t, h, rgb);
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t rgb)
{
    if (r * 2 > h)
        r = h / 2;
    if (r * 2 > w)
        r = w / 2;
    set_color(rgb, 255);
    SDL_Rect mid = {x, y + r, w, h - 2 * r};
    SDL_RenderFillRect(g_ren, &mid);
    for (int i = 0; i < r; i++) {
        float dy = (float)(r - i) - 0.5f;
        int dx = (int)lroundf(sqrtf((float)(r * r) - dy * dy));
        SDL_Rect top = {x + r - dx, y + i, w - 2 * (r - dx), 1};
        SDL_Rect bot = {x + r - dx, y + h - 1 - i, w - 2 * (r - dx), 1};
        SDL_RenderFillRect(g_ren, &top);
        SDL_RenderFillRect(g_ren, &bot);
    }
}

void gfx_star(int cx, int cy, int r, uint32_t rgb)
{
    SDL_Vertex v[11];
    SDL_Color c = {(rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255};
    v[0].position.x = (float)cx;
    v[0].position.y = (float)cy;
    v[0].color = c;
    for (int i = 0; i < 10; i++) {
        float a = (float)M_PI / 5.0f * (float)i - (float)M_PI / 2.0f;
        float rr = (i % 2) ? r * 0.45f : (float)r;
        v[i + 1].position.x = cx + cosf(a) * rr;
        v[i + 1].position.y = cy + sinf(a) * rr;
        v[i + 1].color = c;
    }
    int idx[30];
    for (int i = 0; i < 10; i++) {
        idx[i * 3] = 0;
        idx[i * 3 + 1] = i + 1;
        idx[i * 3 + 2] = (i + 1) % 10 + 1;
    }
    SDL_RenderGeometry(g_ren, NULL, v, 11, idx, 30);
}

void gfx_battery(int x, int y, int h, int pct, int charging)
{
    int w = h * 2;
    const TmTheme *t = &g_theme;
    gfx_frame(x, y, w, h, 2, t->text);
    gfx_rect(x + w, y + h / 4, 3, h / 2, t->text);
    if (pct >= 0) {
        int fill = (w - 6) * (pct > 100 ? 100 : pct) / 100;
        uint32_t col = pct <= 15 ? t->danger : charging > 0 ? t->ok : t->text;
        gfx_rect(x + 3, y + 3, fill, h - 6, col);
    }
}

int gfx_font_height(int font) { return g_fonts[font].line_h; }

int gfx_text_width(int font, const char *s)
{
    Font *f = &g_fonts[font];
    int w = 0;
    while (s && *s) {
        Glyph *g = glyph_get(f, utf8_next(&s));
        if (g)
            w += g->adv;
    }
    return w;
}

static int draw_run(Font *f, int x, int y, uint32_t rgb, const char *s, const char *end)
{
    SDL_SetTextureColorMod(f->atlas, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
    int pen = x;
    while (*s && (!end || s < end)) {
        Glyph *g = glyph_get(f, utf8_next(&s));
        if (!g)
            continue;
        if (g->w > 0) {
            SDL_Rect src = {g->x, g->y, g->w, g->h};
            SDL_Rect dst = {pen + g->xoff, y + f->ascent + g->yoff, g->w, g->h};
            SDL_RenderCopy(g_ren, f->atlas, &src, &dst);
        }
        pen += g->adv;
    }
    return pen - x;
}

int gfx_text(int font, int x, int y, uint32_t rgb, int align, int max_w, const char *s)
{
    if (!s)
        return 0;
    Font *f = &g_fonts[font];
    int w = gfx_text_width(font, s);
    const char *end = NULL;
    int ell = 0;
    if (max_w > 0 && w > max_w) {
        int ew = gfx_text_width(font, "…"), acc = 0;
        const char *p = s;
        while (*p) {
            const char *q = p;
            Glyph *g = glyph_get(f, utf8_next(&q));
            int a = g ? g->adv : 0;
            if (acc + a + ew > max_w)
                break;
            acc += a;
            p = q;
        }
        end = p;
        w = acc + ew;
        ell = 1;
    }
    if (align == ALIGN_CENTER)
        x -= w / 2;
    else if (align == ALIGN_RIGHT)
        x -= w;
    int dw = draw_run(f, x, y, rgb, s, end);
    if (ell)
        draw_run(f, x + dw, y, rgb, "…", NULL);
    return w;
}

int gfx_text_wrap(int font, int x, int y, int w, int max_lines, uint32_t rgb, const char *s)
{
    Font *f = &g_fonts[font];
    int lines = 0;
    const char *p = s;
    while (p && *p && lines < max_lines) {
        while (*p == ' ')
            p++;
        const char *line_end = p, *last_space = NULL, *q = p;
        int acc = 0;
        while (*q && *q != '\n') {
            const char *r = q;
            Glyph *g = glyph_get(f, utf8_next(&r));
            int a = g ? g->adv : 0;
            if (acc + a > w && q > p)
                break;
            if (*q == ' ')
                last_space = q;
            acc += a;
            q = r;
        }
        if (*q && *q != '\n' && last_space)
            line_end = last_space;
        else
            line_end = q;
        if (lines == max_lines - 1 && *line_end && *line_end != '\n') {
            char buf[512];
            size_t n = strlen(p) < sizeof buf - 1 ? strlen(p) : sizeof buf - 1;
            memcpy(buf, p, n);
            buf[n] = '\0';
            gfx_text(font, x, y + lines * f->line_h, rgb, ALIGN_LEFT, w, buf);
        } else {
            draw_run(f, x, y + lines * f->line_h, rgb, p, line_end);
        }
        lines++;
        p = *line_end == '\n' ? line_end + 1 : line_end;
    }
    return lines * f->line_h;
}

int gfx_badge(int x, int y, int h, uint32_t rgb, const char *label)
{
    int pad = h / 3;
    int w = gfx_text_width(FONT_S, label) + pad * 2;
    if (w < h * 2)
        w = h * 2;
    gfx_round_rect(x, y, w, h, h / 4, rgb);
    gfx_text(FONT_S, x + w / 2, y + (h - gfx_font_height(FONT_S)) / 2, 0xffffff, ALIGN_CENTER, 0, label);
    return w;
}

int gfx_screenshot(const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, g_w, g_h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s)
        return -1;
    int rc = SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    if (rc == 0)
        rc = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return rc;
}
