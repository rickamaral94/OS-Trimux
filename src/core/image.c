/* Cover images: decode with stb_image, shrink, write with stb_image_write. */
#define _GNU_SOURCE
#include "image.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_MAX_DIMENSIONS 4096
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "third_party/stb_image_write.h"
#pragma GCC diagnostic pop

/* Area-average downscale (each output pixel is the mean of the source pixels
 * it covers). Enough for covers and simple to verify. Only shrinks. */
static void shrink(const unsigned char *src, int w, int h, unsigned char *dst, int nw, int nh)
{
    for (int oy = 0; oy < nh; oy++) {
        int y0 = (int)((long long)oy * h / nh), y1 = (int)((long long)(oy + 1) * h / nh);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (int ox = 0; ox < nw; ox++) {
            int x0 = (int)((long long)ox * w / nw), x1 = (int)((long long)(ox + 1) * w / nw);
            if (x1 <= x0)
                x1 = x0 + 1;
            unsigned long sum[4] = {0, 0, 0, 0};
            for (int y = y0; y < y1; y++) {
                const unsigned char *p = src + ((size_t)y * w + x0) * 4;
                for (int x = x0; x < x1; x++, p += 4)
                    sum[0] += p[0], sum[1] += p[1], sum[2] += p[2], sum[3] += p[3];
            }
            unsigned long n = (unsigned long)(y1 - y0) * (unsigned long)(x1 - x0);
            unsigned char *q = dst + ((size_t)oy * nw + ox) * 4;
            for (int c = 0; c < 4; c++)
                q[c] = (unsigned char)((sum[c] + n / 2) / n);
        }
    }
}

unsigned char *tm_image_load_rgba(const char *path, int *w, int *h)
{
    int n = 0;
    *w = *h = 0;
    return stbi_load(path, w, h, &n, 4);
}

void tm_image_free(void *pixels) { stbi_image_free(pixels); }

int tm_image_fit_png(const char *src, const char *dst, int max_w, int max_h)
{
    int w, h;
    unsigned char *px = tm_image_load_rgba(src, &w, &h);
    if (!px || w <= 0 || h <= 0 || max_w <= 0 || max_h <= 0) {
        tm_image_free(px);
        return -1;
    }
    int nw = w, nh = h;
    if (w > max_w || h > max_h) {
        double sx = (double)max_w / w, sy = (double)max_h / h, s = sx < sy ? sx : sy;
        nw = (int)(w * s + 0.5);
        nh = (int)(h * s + 0.5);
        if (nw < 1)
            nw = 1;
        if (nh < 1)
            nh = 1;
    }
    unsigned char *out = px;
    if (nw != w || nh != h) {
        out = malloc((size_t)nw * nh * 4);
        if (!out) {
            tm_image_free(px);
            return -1;
        }
        shrink(px, w, h, out, nw, nh);
    }
    char tmp[TM_PATH_MAX];
    int rc = -1;
    /* written next to the final name, forced onto the card, then renamed:
     * a power cut or the firmware's boot-time disk check (fsck.fat -p on
     * the card) never finds a half-written cover or a dangling entry */
    if (tm_snprintf(tmp, sizeof tmp, "%s.tmp", dst) == 0 && stbi_write_png(tmp, nw, nh, 4, out, nw * 4) &&
        tm_fsync_path(tmp) == 0) {
        rc = rename(tmp, dst) == 0 ? 0 : -1;
        if (rc)
            unlink(tmp);
        else
            tm_fsync_parent(dst);
    }
    if (out != px)
        free(out);
    tm_image_free(px);
    return rc;
}

/* For unit tests: writes RGBA pixels as PNG. */
int tm_test_write_png(const char *path, int w, int h, const unsigned char *rgba);
int tm_test_write_png(const char *path, int w, int h, const unsigned char *rgba)
{
    return stbi_write_png(path, w, h, 4, rgba, w * 4) ? 0 : -1;
}
