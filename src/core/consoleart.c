#include "consoleart.h"

#include <math.h>
#include <string.h>
#include <strings.h>

/* Drawings are laid out on a 300x300 grid and scaled to the canvas. Shapes
 * are rounded rectangles and ellipses, drawn from their signed distance
 * (anti-aliased edges); a "solid" is a shape with a front face below it, lit
 * from above, which gives the three-quarter view. */

static unsigned char *g_px;
static int g_n;
static float g_k; /* pixels per grid unit */

typedef struct {
    int el; /* 0 rounded rectangle (a,b)-(c,d) radius r; 1 ellipse centre (a,b) radii (c,d) */
    float a, b, c, d, r;
} Sh;

static Sh RR(float x0, float y0, float x1, float y1, float r)
{
    Sh s = {0, x0, y0, x1, y1, r};
    return s;
}

static Sh EL(float cx, float cy, float rx, float ry)
{
    Sh s = {1, cx, cy, rx, ry, 0};
    return s;
}

static float sd(const Sh *s, float x, float y)
{
    if (s->el) {
        float dx = (x - s->a) / s->c, dy = (y - s->b) / s->d;
        return (sqrtf(dx * dx + dy * dy) - 1.0f) * fminf(s->c, s->d);
    }
    float hx = (s->c - s->a) / 2, hy = (s->d - s->b) / 2, r = fminf(s->r, fminf(hx, hy));
    float qx = fabsf(x - (s->a + s->c) / 2) - (hx - r), qy = fabsf(y - (s->b + s->d) / 2) - (hy - r);
    float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r;
}

static void box(const Sh *s, float *x0, float *y0, float *x1, float *y1)
{
    if (s->el) {
        *x0 = s->a - s->c, *y0 = s->b - s->d, *x1 = s->a + s->c, *y1 = s->b + s->d;
    } else {
        *x0 = s->a, *y0 = s->b, *x1 = s->c, *y1 = s->d;
    }
}

static void blend(int x, int y, float r, float g, float b, float a)
{
    if (a <= 0)
        return;
    unsigned char *o = g_px + ((size_t)y * (size_t)g_n + (size_t)x) * 4;
    float da = o[3] / 255.0f, oa = a + da * (1 - a);
    float c[3] = {r, g, b};
    for (int i = 0; i < 3; i++) {
        float v = (c[i] * a + o[i] * da * (1 - a)) / oa;
        o[i] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
    }
    o[3] = (unsigned char)(oa * 255 + 0.5f);
}

enum { FLAT, LIT, FRONT, GLASS };

/* Fills the union of n shapes. LIT: lighter at the top with a bright upper
 * rim; FRONT: the darker face under a solid; GLASS: a screen with a sheen. */
static void fill(const Sh *s, int n, unsigned rgb, int mode, float alpha)
{
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    for (int i = 0; i < n; i++) {
        float x0, y0, x1, y1;
        box(&s[i], &x0, &y0, &x1, &y1);
        bx0 = fminf(bx0, x0), by0 = fminf(by0, y0), bx1 = fmaxf(bx1, x1), by1 = fmaxf(by1, y1);
    }
    int px0 = (int)floorf(bx0 * g_k) - 1, py0 = (int)floorf(by0 * g_k) - 1;
    int px1 = (int)ceilf(bx1 * g_k) + 1, py1 = (int)ceilf(by1 * g_k) + 1;
    if (px0 < 0)
        px0 = 0;
    if (py0 < 0)
        py0 = 0;
    if (px1 > g_n)
        px1 = g_n;
    if (py1 > g_n)
        py1 = g_n;
    float cr = (float)((rgb >> 16) & 255), cg = (float)((rgb >> 8) & 255), cb = (float)(rgb & 255);
    float h = fmaxf(by1 - by0, 1), w = fmaxf(bx1 - bx0, 1);
    for (int py = py0; py < py1; py++)
        for (int px = px0; px < px1; px++) {
            float x = (px + 0.5f) / g_k, y = (py + 0.5f) / g_k, d = 1e9f;
            for (int i = 0; i < n; i++)
                d = fminf(d, sd(&s[i], x, y));
            float cov = 0.5f - d * g_k;
            if (cov <= 0)
                continue;
            if (cov > 1)
                cov = 1;
            float t = (y - by0) / h, f = 1, white = 0;
            if (mode == LIT) {
                f = 1.14f - 0.30f * t;
                float up = 1 - 2 * t, in = -d;
                if (up > 0 && in < 2.6f)
                    white = 0.34f * up * (1 - in / 2.6f);
            } else if (mode == FRONT) {
                f = 0.66f - 0.20f * t;
            } else if (mode == GLASS) {
                f = 1.05f - 0.50f * t;
                float band = ((x - bx0) + (y - by0) * 0.8f) / (w + h * 0.8f);
                if (band > 0.18f && band < 0.42f)
                    white = 0.10f;
                else if (band > 0.46f && band < 0.50f)
                    white = 0.07f;
            }
            float r = fminf(cr * f, 255), g = fminf(cg * f, 255), b = fminf(cb * f, 255);
            r += (255 - r) * white, g += (255 - g) * white, b += (255 - b) * white;
            blend(px, py, r, g, b, cov * alpha);
        }
}

static void flat(Sh s, unsigned rgb)
{
    fill(&s, 1, rgb, FLAT, 1);
}

static void glass(Sh s, unsigned rgb)
{
    fill(&s, 1, rgb, GLASS, 1);
}

/* The union of shapes as a solid with a front face depth units tall. */
static void solid_n(const Sh *s, int n, float depth, unsigned rgb)
{
    if (depth > 0) {
        Sh f[24];
        int m = 0;
        for (int i = 0; i < n && m < 22; i++) {
            if (s[i].el) {
                f[m++] = EL(s[i].a, s[i].b + depth, s[i].c, s[i].d);
                f[m++] = RR(s[i].a - s[i].c, s[i].b, s[i].a + s[i].c, s[i].b + depth, 0);
            } else {
                f[m++] = RR(s[i].a, s[i].b, s[i].c, s[i].d + depth, s[i].r);
            }
        }
        fill(f, m, rgb, FRONT, 1);
    }
    fill(s, n, rgb, LIT, 1);
}

static void solid(Sh s, float depth, unsigned rgb)
{
    solid_n(&s, 1, depth, rgb);
}

/* Soft floor shadow. */
static void shadow(float cx, float cy, float rx, float ry)
{
    int px0 = (int)((cx - rx) * g_k), px1 = (int)((cx + rx) * g_k) + 1;
    int py0 = (int)((cy - ry) * g_k), py1 = (int)((cy + ry) * g_k) + 1;
    for (int py = py0 < 0 ? 0 : py0; py < py1 && py < g_n; py++)
        for (int px = px0 < 0 ? 0 : px0; px < px1 && px < g_n; px++) {
            float dx = ((px + 0.5f) / g_k - cx) / rx, dy = ((py + 0.5f) / g_k - cy) / ry;
            float e = sqrtf(dx * dx + dy * dy);
            if (e >= 1)
                continue;
            float a = e < 0.4f ? 1 : 1 - (e - 0.4f) / 0.6f;
            blend(px, py, 0, 0, 0, 0.30f * a * a);
        }
}

static unsigned mul(unsigned rgb, float f)
{
    unsigned out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float v = (float)((rgb >> sh) & 255) * f;
        out |= (unsigned)(v > 255 ? 255 : v) << sh;
    }
    return out;
}

/* A recess in a surface: dark upper edge, light lower lip. */
static void well(float x0, float y0, float x1, float y1, float r, unsigned surface, unsigned inner)
{
    flat(RR(x0, y0 + 1.6f, x1, y1 + 1.6f, r), mul(surface, 1.22f));
    flat(RR(x0, y0, x1, y1, r), mul(inner, 0.5f));
    flat(RR(x0 + 0.6f, y0 + 2.6f, x1 - 0.6f, y1, r), inner);
}

/* A screen: bezel recess and glass. */
static void screen(float x0, float y0, float x1, float y1, float bez, unsigned surface, unsigned frame, unsigned tint)
{
    well(x0, y0, x1, y1, 6, surface, frame);
    glass(RR(x0 + bez, y0 + bez + 1, x1 - bez, y1 - bez + 1, 2), tint);
}

static void button(float cx, float cy, float r, unsigned rgb)
{
    flat(EL(cx, cy + r * 0.45f, r * 1.12f, r * 1.05f), 0x101010);
    solid(EL(cx, cy, r, r * 0.92f), r * 0.38f, rgb);
}

static void pill(float cx, float cy, float w, float h, unsigned rgb)
{
    solid(RR(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, h / 2), h * 0.3f, rgb);
}

static void dpad(float cx, float cy, float s, unsigned rgb)
{
    float w = s * 0.36f;
    Sh p[2] = {RR(cx - s / 2, cy - w / 2, cx + s / 2, cy + w / 2, 2.5f), RR(cx - w / 2, cy - s / 2, cx + w / 2, cy + s / 2, 2.5f)};
    Sh base[2] = {RR(cx - s / 2 - 1.5f, cy - w / 2 - 1, cx + s / 2 + 1.5f, cy + w / 2 + 3, 3.5f),
                  RR(cx - w / 2 - 1.5f, cy - s / 2 - 1, cx + w / 2 + 1.5f, cy + s / 2 + 3, 3.5f)};
    fill(base, 2, 0x0e0e10, FLAT, 0.8f);
    solid_n(p, 2, s * 0.10f, rgb);
    flat(EL(cx, cy, w * 0.26f, w * 0.26f), mul(rgb, 0.7f));
}

static void stick(float cx, float cy, float r, unsigned rgb)
{
    flat(EL(cx, cy + 1.5f, r * 1.18f, r * 1.12f), 0x121214);
    solid(EL(cx, cy, r, r * 0.92f), r * 0.35f, rgb);
    flat(EL(cx, cy - r * 0.05f, r * 0.62f, r * 0.56f), mul(rgb, 0.82f));
}

/* Ribs or grille slots. */
static void slots(float x0, float y0, int n, float dx, float w, float h, unsigned rgb)
{
    for (int i = 0; i < n; i++)
        flat(RR(x0 + i * dx, y0, x0 + i * dx + w, y0 + h, fminf(w, h) / 2), rgb);
}

/* A cartridge standing in a slot: slot recess, then the cartridge with a label. */
static void cartridge(float x0, float y0, float x1, float y1, float slot_y, unsigned surface, unsigned body,
                      unsigned label)
{
    well(x0 - 8, slot_y - 6, x1 + 8, slot_y + 10, 3, surface, 0x1a1a1c);
    solid(RR(x0, y0, x1, slot_y + 2, 4), 7, body);
    flat(RR(x0 + 8, y0 + 7, x1 - 8, y0 + (slot_y - y0) * 0.62f, 2.5f), label);
    flat(RR(x0 + 8, y0 + 7, x1 - 8, y0 + 12, 2), mul(label, 1.18f));
}

/* ------------------------------------------------------------ gamepads */

static void pad_rect(float cx, float cy, float w, unsigned body, unsigned face, unsigned b1, unsigned b2)
{
    float h = w * 0.42f, x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - h / 2, y1 = cy + h / 2;
    solid(RR(x0, y0, x1, y1, w * 0.05f), w * 0.06f, body);
    if (face != body)
        well(x0 + w * 0.04f, y0 + h * 0.12f, x1 - w * 0.04f, y1 - h * 0.12f, 3, body, face);
    dpad(x0 + w * 0.2f, cy + h * 0.02f, h * 0.56f, 0x1b1b1e);
    pill(cx - w * 0.07f, cy + h * 0.12f, w * 0.09f, h * 0.09f, 0x45464b);
    pill(cx + w * 0.06f, cy + h * 0.12f, w * 0.09f, h * 0.09f, 0x45464b);
    button(x1 - w * 0.25f, cy + h * 0.08f, h * 0.12f, b1);
    button(x1 - w * 0.11f, cy + h * 0.08f, h * 0.12f, b2);
}

/* Rounded ends (dogbone). */
static void pad_round(float cx, float cy, float w, unsigned body, const unsigned btn[4])
{
    float h = w * 0.38f, e = w / 2 - h / 2;
    Sh s[3] = {EL(cx - e, cy, h / 2, h / 2), EL(cx + e, cy, h / 2, h / 2), RR(cx - e, cy - h * 0.42f, cx + e, cy + h * 0.42f, 0)};
    solid_n(s, 3, w * 0.06f, body);
    dpad(cx - e, cy, h * 0.58f, 0x2a2a2e);
    pill(cx - w * 0.07f, cy + h * 0.08f, w * 0.08f, h * 0.08f, 0x6d6d73);
    pill(cx + w * 0.05f, cy + h * 0.08f, w * 0.08f, h * 0.08f, 0x6d6d73);
    float r = h * 0.11f, d = h * 0.2f;
    button(cx + e, cy - d, r, btn[0]);
    button(cx + e + d, cy, r, btn[1]);
    button(cx + e - d, cy, r, btn[2]);
    button(cx + e, cy + d, r, btn[3]);
}

/* Handles below (PlayStation-like). */
static void pad_handles(float cx, float cy, float w, unsigned body, const unsigned btn[4], int sticks)
{
    float h = w * 0.32f;
    Sh s[3] = {RR(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, h * 0.45f), EL(cx - w * 0.36f, cy + h * 0.5f, w * 0.16f, h * 0.75f),
               EL(cx + w * 0.36f, cy + h * 0.5f, w * 0.16f, h * 0.75f)};
    solid_n(s, 3, w * 0.06f, body);
    dpad(cx - w * 0.33f, cy - h * 0.02f, h * 0.62f, mul(body, 0.55f));
    float r = h * 0.11f, d = h * 0.21f, bx = cx + w * 0.33f;
    button(bx, cy - d, r, btn[0]);
    button(bx + d, cy, r, btn[1]);
    button(bx - d, cy, r, btn[2]);
    button(bx, cy + d, r, btn[3]);
    pill(cx - w * 0.08f, cy - h * 0.12f, w * 0.07f, h * 0.09f, 0x3c3d42);
    pill(cx + w * 0.08f, cy - h * 0.12f, w * 0.07f, h * 0.09f, 0x3c3d42);
    if (sticks) {
        stick(cx - w * 0.14f, cy + h * 0.38f, h * 0.17f, 0x2c2d31);
        stick(cx + w * 0.14f, cy + h * 0.38f, h * 0.17f, 0x2c2d31);
    }
}

/* ------------------------------------------------------------ home consoles */

static const unsigned k_snes_btn[4] = {0x5d4ba5, 0xb7b0e0, 0x5d4ba5, 0xb7b0e0};
static const unsigned k_play_btn[4] = {0x47a37f, 0xc75d5d, 0xc77fb6, 0x5d7dc7};
static const unsigned k_dc_btn[4] = {0xe0b52e, 0x3d86d6, 0x46b06a, 0xd8453c};

static void draw_nes(void)
{
    unsigned body = 0xcfccc6;
    shadow(150, 262, 135, 24);
    solid(RR(42, 72, 258, 168, 7), 34, body);
    slots(62, 82, 8, 22, 12, 22, 0xa9a6a0);
    well(58, 114, 242, 160, 4, body, 0xc3c0b9);
    flat(RR(42, 182, 258, 186, 0), 0x77777b);
    flat(RR(78, 179, 222, 199, 2), 0x2e2f33);
    flat(RR(52, 186, 64, 192, 1), 0xd8352f);
    pad_rect(186, 234, 150, 0xd6d4ce, 0x26272b, 0xc8302c, 0xc8302c);
}

static void draw_snes(void)
{
    unsigned body = 0xd5d3da;
    shadow(150, 262, 135, 24);
    solid(RR(40, 92, 260, 180, 34), 30, body);
    cartridge(108, 50, 192, 112, 112, body, 0x8f8d95, 0x6c5ce7);
    pill(75, 140, 34, 13, 0x6b5fa8);
    pill(225, 140, 34, 13, 0x8c84c6);
    flat(RR(70, 200, 230, 204, 2), mul(body, 0.5f));
    pad_round(172, 234, 168, 0xc9c7cf, k_snes_btn);
}

static void draw_md(int addon)
{
    unsigned body = 0x23252a;
    shadow(150, 262, 135, 24);
    if (addon == 2) { /* CD unit underneath */
        solid(RR(32, 140, 268, 188, 10), 22, 0x2a2c31);
        flat(RR(60, 196, 240, 201, 2), 0x111214);
    }
    float y0 = addon == 2 ? 92 : 100, y1 = addon == 2 ? 146 : 182;
    solid(RR(44, y0, 256, y1, 16), addon == 2 ? 14 : 26, body);
    solid(EL(128, (y0 + y1) / 2 + 2, 62, (y1 - y0) * 0.42f), 4, 0x2e3036);
    flat(EL(128, (y0 + y1) / 2 + 2, 48, (y1 - y0) * 0.31f), 0x26282c);
    if (addon == 1) { /* adapter on the slot */
        well(160, y0 + 8, 236, y0 + 34, 3, body, 0x151517);
        solid(RR(166, y0 - 46, 230, y0 + 26, 22), 10, 0x34363c);
        slots(176, y0 - 30, 4, 12, 6, 30, 0x8e3a3a);
    } else {
        cartridge(170, y0 - 40, 230, y0 + 22, y0 + 22, body, 0x34363b, 0x4a6fa5);
    }
    flat(EL(220, y1 - 14, 5, 5), 0xd8352f);
    float pcx = 172, pcy = 236;
    Sh pad[3] = {EL(pcx - 36, pcy, 42, 30), EL(pcx + 36, pcy, 42, 30), RR(pcx - 40, pcy - 27, pcx + 40, pcy + 14, 12)};
    solid_n(pad, 3, 9, 0x2a2c31);
    flat(EL(pcx - 44, pcy, 19, 17), 0x1a1b1e);
    dpad(pcx - 44, pcy, 28, 0x34363b);
    button(pcx + 22, pcy + 6, 6.5f, 0x4b4e55);
    button(pcx + 38, pcy, 6.5f, 0x4b4e55);
    button(pcx + 54, pcy - 6, 6.5f, 0x4b4e55);
    pill(pcx - 2, pcy - 12, 14, 6, 0x4b4e55);
}

static void draw_ms(void)
{
    unsigned body = 0x222327;
    shadow(150, 262, 135, 24);
    solid(RR(44, 96, 256, 180, 4), 30, body);
    flat(RR(44, 160, 256, 170, 0), 0xc0392b);
    slots(60, 108, 6, 10, 5, 34, 0x313237);
    cartridge(122, 66, 196, 116, 116, body, 0x2f3035, 0x1f6fb2);
    flat(RR(56, 186, 76, 192, 2), 0xc0392b);
    pad_rect(178, 236, 140, 0x1f2024, 0x1f2024, 0x55575d, 0x55575d);
}

static void draw_pce(void)
{
    unsigned body = 0xeeeeea;
    shadow(150, 262, 120, 22);
    solid(RR(86, 92, 214, 186, 10), 28, body);
    flat(RR(86, 96, 120, 186, 0), 0x4b4d52);
    well(130, 150, 204, 166, 3, body, 0x2b2c30);
    flat(RR(130, 104, 204, 108, 1), mul(body, 0.8f));
    flat(RR(130, 114, 204, 118, 1), mul(body, 0.8f));
    pad_rect(176, 236, 142, 0xe7e7e3, 0x3d3f45, 0x1f2024, 0x1f2024);
}

static void draw_n64(void)
{
    unsigned body = 0x3b3d42;
    shadow(150, 268, 135, 22);
    Sh c[2] = {RR(34, 110, 266, 186, 24), EL(150, 116, 74, 38)};
    solid_n(c, 2, 28, body);
    cartridge(116, 58, 184, 116, 116, mul(body, 1.15f), 0x56585e, 0x1f8a4c);
    pill(98, 150, 22, 10, 0x6d6f75);
    pill(202, 150, 22, 10, 0x6d6f75);
    float cx = 168, cy = 220;
    unsigned pad = 0x9fa1a6;
    Sh p[4] = {EL(cx, cy, 52, 26), RR(cx - 64, cy - 16, cx - 30, cy + 40, 17), RR(cx + 30, cy - 16, cx + 64, cy + 40, 17),
               RR(cx - 14, cy, cx + 14, cy + 44, 14)};
    solid_n(p, 4, 8, pad);
    dpad(cx - 46, cy - 2, 24, 0x2c2d31);
    stick(cx, cy + 18, 8, 0x8d8f95);
    button(cx + 34, cy + 6, 6, 0x2d6fd6);
    button(cx + 24, cy - 6, 6, 0x2f9e55);
    button(cx + 48, cy - 8, 3.8f, 0xf1c40f);
    button(cx + 56, cy - 1, 3.8f, 0xf1c40f);
    button(cx + 48, cy + 6, 3.8f, 0xf1c40f);
    button(cx + 40, cy - 1, 3.8f, 0xf1c40f);
    button(cx, cy - 8, 4.5f, 0xd8352f);
}

static void draw_neogeo(void)
{
    unsigned body = 0x1f2024;
    shadow(150, 266, 135, 22);
    solid(RR(34, 98, 266, 172, 8), 26, body);
    cartridge(92, 50, 208, 116, 116, body, 0x2a2b30, 0xd4ac0d);
    flat(RR(34, 188, 266, 192, 0), 0x3b3d42);
    solid(RR(64, 206, 244, 252, 10), 12, 0x1c1d21);
    flat(EL(100, 236, 13, 7), 0x0d0d0f);
    solid(RR(97, 214, 103, 236, 3), 0, 0x2b2c30);
    solid(EL(100, 212, 10, 9.5f), 3, 0xd8352f);
    button(150, 230, 7.5f, 0xd8352f);
    button(174, 224, 7.5f, 0xf1c40f);
    button(198, 220, 7.5f, 0x2f9e55);
    button(222, 220, 7.5f, 0x2d6fd6);
}

static void joystick(float cx, float cy, unsigned base, unsigned button_rgb)
{
    solid(RR(cx - 34, cy - 24, cx + 34, cy + 24, 7), 11, base);
    button(cx - 22, cy - 13, 6, button_rgb);
    flat(EL(cx + 4, cy + 2, 10, 6), 0x0d0d0f);
    solid(RR(cx + 1, cy - 34, cx + 7, cy + 2, 3), 0, 0x2b2c30);
    solid(EL(cx + 4, cy - 36, 8, 7.5f), 3, mul(base, 1.2f));
}

static void draw_atari(int seventy)
{
    unsigned body = seventy ? 0x202125 : 0x1c1c1e;
    shadow(150, 266, 135, 22);
    solid(RR(38, 100, 262, 184, seventy ? 12 : 5), 30, body);
    if (seventy) {
        Sh band = RR(38, 158, 262, 172, 0);
        fill(&band, 1, 0xb8bcc2, LIT, 1);
        flat(RR(38, 196, 262, 200, 0), 0x8a8e94);
    } else {
        Sh wood = RR(38, 150, 262, 184, 0);
        fill(&wood, 1, 0x7a4a24, LIT, 1);
        for (int i = 0; i < 5; i++)
            flat(RR(46 + i * 7, 156 + i * 5, 250 - i * 9, 157.5f + i * 5, 0.7f), 0x5f381a);
        flat(RR(38, 186, 262, 212, 0), 0x5c371a);
        for (int i = 0; i < 3; i++)
            flat(RR(50 + i * 11, 192 + i * 6, 250 - i * 13, 193.2f + i * 6, 0.6f), 0x47290f);
        slots(58, 110, 4, 16, 8, 22, 0x9a9ca1);
        slots(198, 110, 2, 18, 8, 22, 0x9a9ca1);
    }
    cartridge(120, 66, 180, 120, 120, body, 0x2b2c30, seventy ? 0x7f8c8d : 0xc0392b);
    joystick(214, 238, 0x1d1e21, 0xd8352f);
}

static void draw_ps(void)
{
    unsigned body = 0xb9bcc3;
    shadow(150, 262, 135, 24);
    solid(RR(36, 92, 264, 186, 10), 22, body);
    solid(EL(116, 136, 54, 36), 3, 0xc5c8ce);
    flat(EL(116, 136, 9, 6), mul(body, 0.7f));
    button(212, 118, 7, 0x9fa2a9);
    button(240, 118, 7, 0x9fa2a9);
    pill(226, 156, 28, 10, 0x9fa2a9);
    flat(RR(36, 196, 264, 199, 0), mul(body, 0.45f));
    pad_handles(168, 222, 156, 0xaeb1b8, k_play_btn, 1);
}

static void draw_dc(void)
{
    unsigned body = 0xededee;
    shadow(150, 262, 125, 24);
    solid(RR(56, 82, 244, 196, 22), 26, body);
    solid(EL(150, 132, 66, 40), 4, 0xf5f5f6);
    flat(EL(150, 132, 12, 8), 0xe67e22);
    flat(EL(150, 132, 6, 4), 0xededee);
    for (int i = 0; i < 4; i++)
        flat(RR(80 + i * 38, 204, 104 + i * 38, 214, 2), 0x55575d);
    float cx = 168, cy = 228;
    Sh p[3] = {RR(cx - 62, cy - 26, cx + 62, cy + 14, 22), EL(cx - 44, cy + 16, 22, 24), EL(cx + 44, cy + 16, 22, 24)};
    solid_n(p, 3, 9, 0xe8e8ea);
    well(cx - 18, cy - 22, cx + 18, cy + 4, 4, 0xe8e8ea, 0x2b2c30);
    stick(cx - 42, cy - 8, 8.5f, 0xb8bac0);
    dpad(cx - 40, cy + 18, 22, 0x4b4e55);
    button(cx + 42, cy - 12, 5.5f, k_dc_btn[0]);
    button(cx + 54, cy - 2, 5.5f, k_dc_btn[1]);
    button(cx + 30, cy - 2, 5.5f, k_dc_btn[2]);
    button(cx + 42, cy + 8, 5.5f, k_dc_btn[3]);
}

/* ------------------------------------------------------------ handhelds */

static void draw_gameboy(unsigned body, unsigned bezel, unsigned glass_rgb, unsigned btn)
{
    shadow(150, 270, 80, 16);
    solid(RR(92, 32, 208, 258, 12), 14, body);
    flat(RR(100, 40, 200, 42, 1), mul(body, 0.82f));
    screen(102, 50, 198, 140, 13, body, bezel, glass_rgb);
    flat(EL(112, 80, 2.4f, 2.4f), 0xd8352f);
    dpad(122, 180, 34, 0x1d1e21);
    button(186, 170, 10, btn);
    button(162, 186, 10, btn);
    pill(136, 224, 20, 7, mul(body, 0.6f));
    pill(162, 224, 20, 7, mul(body, 0.6f));
    for (int i = 0; i < 5; i++)
        flat(RR(176 + i * 6, 232 - i * 2, 179 + i * 6, 252 - i * 2, 1.5f), mul(body, 0.55f));
}

static void draw_wide(unsigned body, float x0, float x1, float sx0, float sx1, unsigned tint, int style)
{
    shadow(150, 236, (x1 - x0) / 2 + 6, 16);
    float y0 = 104, y1 = 206, cy = (y0 + y1) / 2;
    solid(RR(x0, y0, x1, y1, style == 3 ? 46 : 40), 16, body);
    screen(sx0, y0 + 10, sx1, y1 - 12, 11, body, 0x1c1d21, tint);
    float lx = (x0 + sx0) / 2, rx = (sx1 + x1) / 2;
    unsigned dark = 0x26272b, bt = style == 1 ? 0x2c3e8f : style == 2 ? mul(body, 0.55f) : 0x3a3c42;
    if (style == 4) { /* stick on the left (Neo Geo Pocket) */
        stick(lx, cy - 4, 14, 0x2c2d31);
    } else {
        dpad(lx, cy - 6, 34, dark);
    }
    if (style == 3) { /* four face buttons and a small stick (PSP) */
        float d = 11;
        button(rx, cy - 6 - d, 5.5f, 0x55575d);
        button(rx + d, cy - 6, 5.5f, 0x55575d);
        button(rx - d, cy - 6, 5.5f, 0x55575d);
        button(rx, cy - 6 + d, 5.5f, 0x55575d);
        stick(lx - 4, cy + 30, 6, 0x55575d);
        pill(150, y1 - 6, 14, 4, 0x55575d);
        pill(172, y1 - 6, 14, 4, 0x55575d);
    } else if (style == 5) { /* two pairs (Lynx) */
        button(rx - 8, cy - 14, 6.5f, 0x55575d);
        button(rx + 8, cy - 8, 6.5f, 0x55575d);
        button(rx - 8, cy + 10, 6.5f, 0x55575d);
        button(rx + 8, cy + 16, 6.5f, 0x55575d);
    } else {
        button(rx + 8, cy - 14, 8.5f, bt);
        button(rx - 10, cy, 8.5f, bt);
        pill(lx, cy + 34, 16, 6, mul(body, 0.6f));
        if (style == 2)
            button(rx + 2, cy + 26, 4.5f, 0x2d6fd6);
    }
}

static void draw_ws(void)
{
    unsigned body = 0xc9cdd3;
    shadow(150, 236, 128, 16);
    solid(RR(22, 106, 278, 204, 22), 16, body);
    screen(70, 116, 214, 192, 11, body, 0x2b2c30, 0x8fa39a);
    float lx = 46, cy = 155;
    button(lx, cy - 13, 5, 0x55575d);
    button(lx - 13, cy, 5, 0x55575d);
    button(lx + 13, cy, 5, 0x55575d);
    button(lx, cy + 13, 5, 0x55575d);
    button(244, cy - 2, 8, 0x55575d);
    button(262, cy - 16, 8, 0x55575d);
    pill(246, 186, 18, 6, mul(body, 0.6f));
}

/* ------------------------------------------------------------ others */

static void draw_arcade(unsigned accent)
{
    unsigned body = mul(accent, 0.75f);
    shadow(150, 270, 82, 14);
    solid(RR(86, 30, 214, 260, 8), 8, body);
    Sh mq = RR(96, 40, 204, 74, 4);
    fill(&mq, 1, 0xf7d154, LIT, 1);
    flat(RR(108, 52, 192, 62, 3), 0xe39b2b);
    screen(96, 84, 204, 162, 9, body, 0x111214, 0x23417a);
    solid(RR(74, 168, 226, 198, 6), 14, 0x2b2b30);
    flat(EL(110, 186, 9, 5), 0x0d0d0f);
    solid(RR(107, 166, 113, 186, 3), 0, 0x3a3a3f);
    solid(EL(110, 164, 9, 8.5f), 3, 0xd8352f);
    button(150, 182, 6.5f, 0xf1c40f);
    button(170, 178, 6.5f, 0x2f9e55);
    button(190, 182, 6.5f, 0x2d6fd6);
    well(126, 222, 174, 252, 3, body, 0x1a1a1d);
    flat(RR(138, 230, 144, 244, 1), 0xf39c12);
    flat(RR(156, 230, 162, 244, 1), 0xf39c12);
}

static void draw_pc(unsigned tint)
{
    unsigned beige = 0xd9d1be;
    shadow(150, 266, 118, 18);
    solid(RR(68, 40, 232, 168, 10), 20, beige);
    screen(82, 52, 218, 154, 11, beige, 0x26272b, tint);
    flat(RR(196, 178, 210, 182, 1), 0x2f9e55);
    solid(RR(126, 190, 174, 200, 3), 4, mul(beige, 0.85f));
    solid(RR(48, 210, 252, 250, 6), 10, beige);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 14 - (r == 2 ? 4 : 0); c++)
            flat(RR(58 + c * 13.4f + (r == 2 ? 26 : 0), 216 + r * 10.5f, 68 + c * 13.4f + (r == 2 ? 26 : 0),
                    223 + r * 10.5f, 1.5f),
                 0xb9b19e);
}

static void draw_gamepad(unsigned accent)
{
    float cx = 150, cy = 140;
    shadow(150, 236, 122, 18);
    Sh p[5] = {RR(cx - 70, cy - 40, cx + 70, cy + 22, 30), EL(cx - 66, cy + 4, 46, 42), EL(cx + 66, cy + 4, 46, 42),
               EL(cx - 76, cy + 46, 30, 38), EL(cx + 76, cy + 46, 30, 38)};
    solid_n(p, 5, 14, accent);
    dpad(cx - 72, cy - 6, 38, 0x26272b);
    float d = 15, bx = cx + 72, by = cy - 6;
    button(bx, by - d, 7.5f, 0xf1c40f);
    button(bx + d, by, 7.5f, 0xd8352f);
    button(bx - d, by, 7.5f, 0x2d6fd6);
    button(bx, by + d, 7.5f, 0x2f9e55);
    stick(cx - 34, cy + 34, 15, 0x2c2d31);
    stick(cx + 34, cy + 34, 15, 0x2c2d31);
    pill(cx - 16, cy - 12, 16, 7, mul(accent, 0.5f));
    pill(cx + 16, cy - 12, 16, 7, mul(accent, 0.5f));
}

void tm_console_art(unsigned char *rgba, int size, const char *id, unsigned accent)
{
    g_px = rgba, g_n = size, g_k = (float)size / 300.0f;
    if (!id)
        id = "";
    if (!strcasecmp(id, "FC"))
        draw_nes();
    else if (!strcasecmp(id, "SFC"))
        draw_snes();
    else if (!strcasecmp(id, "MD"))
        draw_md(0);
    else if (!strcasecmp(id, "32X"))
        draw_md(1);
    else if (!strcasecmp(id, "SEGACD"))
        draw_md(2);
    else if (!strcasecmp(id, "MS"))
        draw_ms();
    else if (!strcasecmp(id, "PCE"))
        draw_pce();
    else if (!strcasecmp(id, "N64"))
        draw_n64();
    else if (!strcasecmp(id, "NEOGEO"))
        draw_neogeo();
    else if (!strcasecmp(id, "A2600"))
        draw_atari(0);
    else if (!strcasecmp(id, "A7800"))
        draw_atari(1);
    else if (!strcasecmp(id, "PS"))
        draw_ps();
    else if (!strcasecmp(id, "DC"))
        draw_dc();
    else if (!strcasecmp(id, "GB"))
        draw_gameboy(0xc4c2bc, 0x5b5d6b, 0x8bac0f, 0x9b2257);
    else if (!strcasecmp(id, "GBC"))
        draw_gameboy(0x16a085, 0x2b2c30, 0x6fa8c9, 0x2b2c30);
    else if (!strcasecmp(id, "GBA"))
        draw_wide(0x5b3fa8, 22, 278, 86, 214, 0x6fa8c9, 0);
    else if (!strcasecmp(id, "GG"))
        draw_wide(0x2a2c31, 20, 280, 88, 212, 0x4f8fd0, 2);
    else if (!strcasecmp(id, "PSP"))
        draw_wide(0x26282d, 18, 282, 74, 226, 0x3d5fa8, 3);
    else if (!strcasecmp(id, "NGP"))
        draw_wide(0x2f3237, 26, 274, 84, 216, 0x9fb88a, 4);
    else if (!strcasecmp(id, "LYNX"))
        draw_wide(0x3d3f45, 12, 288, 92, 208, 0x5c8fbf, 5);
    else if (!strcasecmp(id, "WS"))
        draw_ws();
    else if (!strcasecmp(id, "ARCADE"))
        draw_arcade(0x3b3f8f);
    else if (!strcasecmp(id, "DOOM"))
        draw_pc(0x8b1a1a);
    else if (!strcasecmp(id, "QUAKE"))
        draw_pc(0x6d4c3d);
    else if (!strcasecmp(id, "CAVESTORY"))
        draw_pc(0x1565c0);
    else
        draw_gamepad(accent ? accent : 0x2e7d32);
}
