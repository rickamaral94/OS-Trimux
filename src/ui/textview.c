/* Text viewer: shows a log or any text file of the card full screen, with
 * long lines wrapped and the end of the file (the newest lines) first.
 * Only the last TV_MAX_BYTES are read; nothing is ever written. */
#define _GNU_SOURCE
#include "app.h"
#include "../core/log.h"
#include "../core/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define S(v) ((v) * gfx_h() / 768)
#define TV_MAX_BYTES (128 * 1024)
#define TV_SNIFF 4096

typedef struct {
    int off, len;
} TvLine;

static struct {
    char *text;
    TvLine *lines;
    int n, cap, top;
    int wrap_w; /* width the lines were wrapped for */
    int truncated;
    int prev_screen;
    char title[96];
    char path[TM_PATH_MAX];
} T;

int textview_is_text(const char *abs)
{
    FILE *f = fopen(abs, "rb");
    if (!f)
        return 0;
    unsigned char buf[TV_SNIFF];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    int bad = 0;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == 0)
            return 0;
        if (buf[i] < 32 && buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\t' && buf[i] != 27)
            bad++;
    }
    return (size_t)bad * 20 <= n; /* a few stray control bytes are fine */
}

/* Valid UTF-8 sequence length at s (0 = invalid). */
static int utf8_len(const unsigned char *s, size_t left)
{
    int n = s[0] < 0x80 ? 1 : (s[0] & 0xE0) == 0xC0 ? 2 : (s[0] & 0xF0) == 0xE0 ? 3 : (s[0] & 0xF8) == 0xF0 ? 4 : 0;
    if (!n || (size_t)n > left)
        return 0;
    for (int i = 1; i < n; i++)
        if ((s[i] & 0xC0) != 0x80)
            return 0;
    return n;
}

/* Tabs become a space, other control bytes and broken UTF-8 a '?', so the
 * font always gets valid text. */
static void sanitize(char *s, size_t len)
{
    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') {
            i++;
            continue;
        }
        if (c == '\t' || c == '\r') {
            s[i++] = ' ';
            continue;
        }
        if (c < 32 || c == 127) {
            s[i++] = '?';
            continue;
        }
        int n = utf8_len((const unsigned char *)s + i, len - i);
        if (!n) {
            s[i++] = '?';
            continue;
        }
        i += (size_t)n;
    }
}

static void push(int off, int len)
{
    if (T.n == T.cap) {
        int cap = T.cap ? T.cap * 2 : 1024;
        TvLine *nl = realloc(T.lines, (size_t)cap * sizeof *nl);
        if (!nl)
            return;
        T.lines = nl;
        T.cap = cap;
    }
    T.lines[T.n].off = off;
    T.lines[T.n].len = len;
    T.n++;
}

/* Splits the text into screen lines no wider than w pixels: at a space when
 * there is one, otherwise in the middle of the word. */
static void wrap(int w)
{
    T.n = 0;
    T.wrap_w = w;
    const char *s = T.text;
    size_t len = strlen(s);
    size_t i = 0;
    while (i <= len) {
        size_t end = i;
        while (end < len && s[end] != '\n')
            end++;
        if (end == i)
            push((int)i, 0);
        size_t p = i;
        while (p < end) {
            int width = 0;
            size_t q = p, last_space = 0;
            while (q < end) {
                int cl = utf8_len((const unsigned char *)s + q, end - q);
                if (!cl)
                    cl = 1;
                char ch[5] = {0};
                memcpy(ch, s + q, (size_t)cl);
                int cw = gfx_text_width(FONT_S, ch);
                if (width + cw > w && q > p)
                    break;
                width += cw;
                if (s[q] == ' ')
                    last_space = q;
                q += (size_t)cl;
            }
            if (q < end && last_space > p)
                q = last_space + 1; /* break after the last space that fits */
            push((int)p, (int)(q - p));
            p = q;
        }
        i = end + 1;
    }
}

static int body_rows(void)
{
    int lh = gfx_font_height(FONT_S) + S(4);
    int top = S(64) + S(16), bottom = gfx_h() - S(52) - S(14);
    return lh > 0 ? (bottom - top - S(28)) / lh : 1;
}

static int text_width(void)
{
    return gfx_w() - S(28) * 2 - S(52); /* margins and the scroll bar */
}

static void clamp_top(void)
{
    int rows = body_rows();
    if (T.top > T.n - rows)
        T.top = T.n - rows;
    if (T.top < 0)
        T.top = 0;
}

int textview_open(const char *abs, const char *title)
{
    struct stat st;
    if (stat(abs, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    FILE *f = fopen(abs, "rb");
    if (!f)
        return -1;
    long size = (long)st.st_size;
    long from = size > TV_MAX_BYTES ? size - TV_MAX_BYTES : 0;
    char *buf = malloc((size_t)(size - from) + 1);
    if (!buf || fseek(f, from, SEEK_SET) != 0) {
        free(buf);
        fclose(f);
        return -1;
    }
    size_t n = fread(buf, 1, (size_t)(size - from), f);
    fclose(f);
    buf[n] = '\0';
    char *start = buf;
    if (from > 0) { /* start at a whole line */
        char *nl = memchr(buf, '\n', n);
        if (nl)
            start = nl + 1;
    }
    size_t len = n - (size_t)(start - buf);
    memmove(buf, start, len + 1);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    sanitize(buf, len);
    free(T.text);
    T.text = buf;
    T.truncated = from > 0;
    tm_strlcpy(T.title, title && *title ? title : abs, sizeof T.title);
    tm_strlcpy(T.path, abs, sizeof T.path);
    T.n = 0;
    T.wrap_w = 0;
    T.top = 1 << 30; /* the end of the file: the newest lines */
    LOGI("ui: viewing %s (%zu bytes%s)", abs, len, T.truncated ? ", end only" : "");
    if (A.screen != SCR_TEXT)
        T.prev_screen = A.screen;
    A.screen = SCR_TEXT;
    A.dirty = 1;
    return 0;
}

void textview_draw(void)
{
    const TmTheme *t = gfx_theme();
    if (T.wrap_w != text_width())
        wrap(text_width());
    clamp_top();
    app_header(T.title);
    int x = S(28), top = S(64) + S(16), w = gfx_w() - 2 * x, h = gfx_h() - S(52) - S(14) - top;
    app_panel(x, top, w, h, 0);
    int lh = gfx_font_height(FONT_S) + S(4), rows = body_rows();
    int y = top + S(14);
    if (T.n == 0 || (T.n == 1 && T.lines[0].len == 0)) {
        gfx_text(FONT_S, x + S(18), y, t->dim, ALIGN_LEFT, w - S(36), tr("textview.empty"));
    } else {
        char line[1024];
        for (int i = 0; i < rows && T.top + i < T.n; i++) {
            const TvLine *l = &T.lines[T.top + i];
            int len = l->len < (int)sizeof line - 1 ? l->len : (int)sizeof line - 1;
            memcpy(line, T.text + l->off, (size_t)len);
            line[len] = '\0';
            gfx_text(FONT_S, x + S(18), y + i * lh, t->text, ALIGN_LEFT, w - S(36), line);
        }
    }
    app_scrollbar(x + w - S(12), top + S(8), h - S(16), T.top + rows - 1 < T.n ? T.top + rows - 1 : T.n - 1, T.n, rows);
    char hints[256], pos[64];
    snprintf(pos, sizeof pos, tr("textview.pos"), T.n ? (T.top + rows < T.n ? T.top + rows : T.n) : 0, T.n);
    snprintf(hints, sizeof hints, "%s|B:%s%s", tr("textview.hints"), pos, T.truncated ? tr("textview.truncated") : "");
    app_footer(hints);
}

void textview_input(TmButton b)
{
    int rows = body_rows();
    switch (b) {
    case BTN_UP: T.top--; break;
    case BTN_DOWN: T.top++; break;
    case BTN_LEFT:
    case BTN_L1: T.top -= rows - 1; break;
    case BTN_RIGHT:
    case BTN_R1: T.top += rows - 1; break;
    case BTN_L2: T.top = 0; break;
    case BTN_R2: T.top = T.n; break;
    case BTN_B:
    case BTN_A:
    case BTN_MENU:
        A.screen = (ScreenType)T.prev_screen;
        free(T.text);
        T.text = NULL;
        T.n = 0;
        break;
    default: break;
    }
    clamp_top();
}
