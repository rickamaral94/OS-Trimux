/* On-screen keyboard for search. Results update as you type. */
#define _GNU_SOURCE
#include "app.h"
#include "../core/util.h"

#include <string.h>

#define S(v) ((v) * gfx_h() / 768)

static const char *const k_rows[] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm'.&"};
#define KB_ROWS 5 /* 4 character rows + action row */
enum { KA_SPACE, KA_DEL, KA_CLEAR, KA_DONE, KA_COUNT };

void keyboard_open(const char *initial, ScreenType ret)
{
    tm_strlcpy(A.kb_buf, initial ? initial : "", sizeof A.kb_buf);
    A.kb_row = 1;
    A.kb_col = 0;
    A.kb_return = ret;
    A.screen = SCR_KEYBOARD;
}

static int row_len(int r) { return r < 4 ? (int)strlen(k_rows[r]) : KA_COUNT; }

static void apply_query(void)
{
    tm_strlcpy(A.query, A.kb_buf, sizeof A.query);
    if (A.kb_return == SCR_GAMES)
        games_rebuild();
}

void keyboard_draw(void)
{
    const TmTheme *t = gfx_theme();
    app_header(tr("search.title"));
    int y = S(64) + S(24);
    gfx_round_rect(S(40), y, gfx_w() - S(80), S(64), S(12), t->panel);
    gfx_text(FONT_L, S(64), y + (S(64) - gfx_font_height(FONT_L)) / 2, A.kb_buf[0] ? t->text : t->dim, ALIGN_LEFT,
             gfx_w() - S(300), A.kb_buf[0] ? A.kb_buf : tr("search.placeholder"));
    char cnt[64];
    snprintf(cnt, sizeof cnt, tr("search.results"), A.nview);
    gfx_text(FONT_S, gfx_w() - S(64), y + (S(64) - gfx_font_height(FONT_S)) / 2, t->dim, ALIGN_RIGHT, 0, cnt);
    y += S(96);
    int kw = S(80), kh = S(70), gap = S(10);
    for (int r = 0; r < 4; r++) {
        int n = row_len(r), x0 = (gfx_w() - (n * kw + (n - 1) * gap)) / 2;
        for (int c = 0; c < n; c++) {
            int sel = r == A.kb_row && c == A.kb_col;
            int x = x0 + c * (kw + gap), yy = y + r * (kh + gap);
            gfx_round_rect(x, yy, kw, kh, S(10), sel ? t->sel : t->panel2);
            char ch[2] = {(char)(k_rows[r][c] >= 'a' && k_rows[r][c] <= 'z' ? k_rows[r][c] - 32 : k_rows[r][c]), 0};
            gfx_text(FONT_L, x + kw / 2, yy + (kh - gfx_font_height(FONT_L)) / 2, sel ? t->accent_text : t->text,
                     ALIGN_CENTER, 0, ch);
        }
    }
    const char *labels[KA_COUNT] = {tr("search.space"), tr("search.delete"), tr("search.clear"), tr("search.done")};
    int aw = S(200), x0 = (gfx_w() - (KA_COUNT * aw + (KA_COUNT - 1) * gap)) / 2, yy = y + 4 * (kh + gap);
    for (int c = 0; c < KA_COUNT; c++) {
        int sel = A.kb_row == 4 && A.kb_col == c;
        gfx_round_rect(x0 + c * (aw + gap), yy, aw, kh, S(10), sel ? t->sel : t->panel2);
        gfx_text(FONT_M, x0 + c * (aw + gap) + aw / 2, yy + (kh - gfx_font_height(FONT_M)) / 2,
                 sel ? t->accent_text : t->text, ALIGN_CENTER, aw - S(10), labels[c]);
    }
    app_footer(tr("search.hints"));
}

static void type_char(char c)
{
    size_t n = strlen(A.kb_buf);
    if (n + 1 < sizeof A.kb_buf) {
        A.kb_buf[n] = c;
        A.kb_buf[n + 1] = '\0';
        apply_query();
    }
}

static void del_char(void)
{
    size_t n = strlen(A.kb_buf);
    if (n) {
        A.kb_buf[n - 1] = '\0';
        apply_query();
    }
}

static void done(void)
{
    apply_query();
    A.screen = A.kb_return;
}

void keyboard_input(TmButton b)
{
    switch (b) {
    case BTN_UP: A.kb_row = (A.kb_row + KB_ROWS - 1) % KB_ROWS; break;
    case BTN_DOWN: A.kb_row = (A.kb_row + 1) % KB_ROWS; break;
    case BTN_LEFT: A.kb_col--; break;
    case BTN_RIGHT: A.kb_col++; break;
    case BTN_A:
        if (A.kb_row < 4)
            type_char(k_rows[A.kb_row][A.kb_col]);
        else if (A.kb_col == KA_SPACE)
            type_char(' ');
        else if (A.kb_col == KA_DEL)
            del_char();
        else if (A.kb_col == KA_CLEAR) {
            A.kb_buf[0] = '\0';
            apply_query();
        } else
            done();
        break;
    case BTN_X: del_char(); break;
    case BTN_Y: type_char(' '); break;
    case BTN_START: done(); break;
    case BTN_B:
        if (A.kb_buf[0])
            del_char();
        else
            done();
        break;
    default: break;
    }
    int n = row_len(A.kb_row);
    if (A.kb_col < 0)
        A.kb_col = n - 1;
    if (A.kb_col >= n)
        A.kb_col = A.kb_row == 4 ? 0 : (A.kb_col >= n ? 0 : A.kb_col);
}
