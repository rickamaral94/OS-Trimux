/* Cover images: decode (PNG/JPEG), shrink to fit, write PNG. Wraps the
 * public-domain stb_image libraries (src/core/third_party). */
#ifndef TRIMUX_IMAGE_H
#define TRIMUX_IMAGE_H

/* RGBA pixels (4 bytes each). Free with tm_image_free. Images larger than
 * 4096x4096 are refused. */
unsigned char *tm_image_load_rgba(const char *path, int *w, int *h);
void tm_image_free(void *pixels);
/* Shrinks src (never enlarges) to fit max_w x max_h, keeping the aspect ratio,
 * and writes it as PNG to dst atomically. Returns 0 on success. */
int tm_image_fit_png(const char *src, const char *dst, int max_w, int max_h);

#endif
