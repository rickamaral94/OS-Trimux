/* Cover images on top of stb_image / stb_image_resize2 / stb_image_write. */
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
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#include "third_party/stb_image_resize2.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "third_party/stb_image_write.h"
#pragma GCC diagnostic pop

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
        if (!out || !stbir_resize_uint8_srgb(px, w, h, 0, out, nw, nh, 0, STBIR_RGBA)) {
            free(out);
            tm_image_free(px);
            return -1;
        }
    }
    char tmp[TM_PATH_MAX];
    int rc = -1;
    if (tm_snprintf(tmp, sizeof tmp, "%s.tmp", dst) == 0 && stbi_write_png(tmp, nw, nh, 4, out, nw * 4)) {
        rc = rename(tmp, dst) == 0 ? 0 : -1;
        if (rc)
            unlink(tmp);
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
