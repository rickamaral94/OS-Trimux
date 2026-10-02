/* On-screen keyboard. Search mode: results update as you type. Text mode:
 * lower/upper/symbol layers for Wi-Fi passwords and account names. */
#define _GNU_SOURCE
#include "app.h"
#include "../core/util.h"

#include <string.h>

#define S(v) ((v) * gfx_h() / 768)

static const char *const k_search[] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm'.&"};
static const char *const k_text[3][4] = {
    {"1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm-_."},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL@", "ZXCVBNM-_."},
    {"!?#$%&*()+", "=[]{}<>/\\|", ";:'\",~`^", "-_.@"},
};
#define KB_ROWS 5 /* 4 character rows + action row */
enum { KA_SPACE, KA_DEL, KA_CLEAR, KA_DONE, KA_COUNT };
enum { KT_SPACE, KT_DEL, KT_LAYER, KT_CLEAR, KT_DONE, KT_COUNT };

static int text_mode(void) { return A.kb_purpose != KB_SEARCH; }

static const char *row_chars(int r) { return text_mode() ? k_text[A.kb_layer][r] : k_search[r]; }

static int row_len(int r)
{
    if (r < 4)
        return (int)strlen(row_chars(r));
    return text_mode() ? KT_COUNT : KA_COUNT;
}

static void reset_cursor(void)
{
    A.kb_row = 1;
    A.kb_col = 0;
    A.kb_layer = 0;
}

void keyboard_open(const char *initial, ScreenType ret)
{
    tm_strlcpy(A.kb_buf, initial ? initial : "", sizeof A.kb_buf);
    A.kb_purpose = KB_SEARCH;
    A.kb_return = ret;
    A.kb_title[0] = A.kb_ctx[0] = '\0';
    reset_cursor();
    A.screen = SCR_KEYBOARD;
}

void keyboard_open_text(int purpose, const char *title, const char *initial, const char *ctx)
{
    tm_strlcpy(A.kb_buf, initial ? initial : "", sizeof A.kb_buf);
    A.kb_purpose = purpose;
    A.kb_return = SCR_MENU;
    tm_strlcpy(A.kb_title, title ? title : "", sizeof A.kb_title);
    tm_strlcpy(A.kb_ctx, ctx ? ctx : "", sizeof A.kb_ctx);
    reset_cursor();
    A.screen = SCR_KEYBOARD;
}

static void apply_query(void)
{
    if (text_mode())
        return;
    tm_strlcpy(A.query, A.kb_buf, sizeof A.query);
    if (A.kb_return == SCR_GAMES)
        games_rebuild();
}

static const char *layer_label(void)
{
    static const char *const next[] = {"kb.layer.upper", "kb.layer.symbols", "kb.layer.lower"};
    return tr(next[A.kb_layer]);
}

void keyboard_draw(void)
{
    const TmTheme *t = gfx_theme();
    app_header(text_mode() ? A.kb_title : tr("search.title"));
    int y = S(64) + S(24);
    gfx_round_rect(S(40), y, gfx_w() - S(80), S(64), S(12), t->panel);
    const char *ph = text_mode() ? tr("kb.placeholder") : tr("search.placeholder");
    gfx_text(FONT_L, S(64), y + (S(64) - gfx_font_height(FONT_L)) / 2, A.kb_buf[0] ? t->text : t->dim, ALIGN_LEFT,
             gfx_w() - S(300), A.kb_buf[0] ? A.kb_buf : ph);
    char cnt[64];
    if (text_mode())
        snprintf(cnt, sizeof cnt, tr("kb.count"), strlen(A.kb_buf));
    else
        snprintf(cnt, sizeof cnt, tr("search.results"), A.nview);
    gfx_text(FONT_S, gfx_w() - S(64), y + (S(64) - gfx_font_height(FONT_S)) / 2, t->dim, ALIGN_RIGHT, 0, cnt);
    y += S(96);
    int kw = S(80), kh = S(70), gap = S(10);
    for (int r = 0; r < 4; r++) {
        const char *chars = row_chars(r);
        int n = row_len(r), x0 = (gfx_w() - (n * kw + (n - 1) * gap)) / 2;
        for (int c = 0; c < n; c++) {
            int sel = r == A.kb_row && c == A.kb_col;
            int x = x0 + c * (kw + gap), yy = y + r * (kh + gap);
            gfx_round_rect(x, yy, kw, kh, S(10), sel ? t->sel : t->panel2);
            char ch[2] = {chars[c], 0};
            if (!text_mode() && ch[0] >= 'a' && ch[0] <= 'z')
                ch[0] = (char)(ch[0] - 32);
            gfx_text(FONT_L, x + kw / 2, yy + (kh - gfx_font_height(FONT_L)) / 2, sel ? t->accent_text : t->text,
                     ALIGN_CENTER, 0, ch);
        }
    }
    int na = row_len(4);
    const char *labels[KT_COUNT];
    if (text_mode()) {
        labels[KT_SPACE] = tr("search.space");
        labels[KT_DEL] = tr("search.delete");
        labels[KT_LAYER] = layer_label();
        labels[KT_CLEAR] = tr("search.clear");
        labels[KT_DONE] = tr("search.done");
    } else {
        labels[KA_SPACE] = tr("search.space");
        labels[KA_DEL] = tr("search.delete");
        labels[KA_CLEAR] = tr("search.clear");
        labels[KA_DONE] = tr("search.done");
    }
    int aw = text_mode() ? S(176) : S(200);
    int x0 = (gfx_w() - (na * aw + (na - 1) * gap)) / 2, yy = y + 4 * (kh + gap);
    for (int c = 0; c < na; c++) {
        int sel = A.kb_row == 4 && A.kb_col == c;
        gfx_round_rect(x0 + c * (aw + gap), yy, aw, kh, S(10), sel ? t->sel : t->panel2);
        gfx_text(FONT_M, x0 + c * (aw + gap) + aw / 2, yy + (kh - gfx_font_height(FONT_M)) / 2,
                 sel ? t->accent_text : t->text, ALIGN_CENTER, aw - S(10), labels[c]);
    }
    app_footer(tr(text_mode() ? "kb.hints" : "search.hints"));
}

static void type_char(char c)
{
    size_t n = strlen(A.kb_buf);
    /* text mode is for passwords and names: 63 characters is the WPA limit */
    size_t max = text_mode() ? 63 : sizeof A.kb_buf - 1;
    if (n < max) {
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

static void finish(int cancelled)
{
    if (!text_mode()) {
        apply_query();
        A.screen = A.kb_return;
        return;
    }
    char text[sizeof A.kb_buf], ctx[sizeof A.kb_ctx];
    tm_strlcpy(text, A.kb_buf, sizeof text);
    tm_strlcpy(ctx, A.kb_ctx, sizeof ctx);
    int purpose = A.kb_purpose;
    A.kb_buf[0] = '\0'; /* do not keep a typed password around */
    A.screen = A.kb_return;
    menu_keyboard_done(purpose, ctx, text, cancelled);
    memset(text, 0, sizeof text);
}

static void next_layer(void)
{
    if (text_mode())
        A.kb_layer = (A.kb_layer + 1) % 3;
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
            type_char(row_chars(A.kb_row)[A.kb_col]);
        else if (text_mode()) {
            switch (A.kb_col) {
            case KT_SPACE: type_char(' '); break;
            case KT_DEL: del_char(); break;
            case KT_LAYER: next_layer(); break;
            case KT_CLEAR: A.kb_buf[0] = '\0'; break;
            default: finish(0); return;
            }
        } else if (A.kb_col == KA_SPACE)
            type_char(' ');
        else if (A.kb_col == KA_DEL)
            del_char();
        else if (A.kb_col == KA_CLEAR) {
            A.kb_buf[0] = '\0';
            apply_query();
        } else {
            finish(0);
            return;
        }
        break;
    case BTN_X: del_char(); break;
    case BTN_Y: type_char(' '); break;
    case BTN_R1:
    case BTN_SELECT: next_layer(); break;
    case BTN_START: finish(0); return;
    case BTN_B:
        if (A.kb_buf[0])
            del_char();
        else {
            finish(1);
            return;
        }
        break;
    default: break;
    }
    int n = row_len(A.kb_row);
    if (A.kb_col < 0)
        A.kb_col = n - 1;
    if (A.kb_col >= n)
        A.kb_col = b == BTN_RIGHT ? 0 : n - 1;
}
