#define _GNU_SOURCE
#include "input.h"
#include "../core/log.h"
#include "../core/util.h"

#include <string.h>

#define REPEAT_DELAY_MS 320
#define REPEAT_RATE_MS 70
#define AXIS_ON 20000
#define AXIS_OFF 12000

static SDL_Joystick *g_pads[4];
static int g_npads;
static int g_map[BTN_COUNT];  /* joystick button index per action, -1 none */
static int g_axis_l2 = 2, g_axis_r2 = 5, g_axis_lx = 0, g_axis_ly = 1;
static int g_swap_ab;
static TmInputState g_st;
static uint64_t g_held_since[BTN_COUNT], g_last_repeat[BTN_COUNT];
/* sources of a direction: hat, stick, keyboard, buttons (bitmask) */
static int g_src[BTN_COUNT];

enum { SRC_BUTTON = 1, SRC_HAT = 2, SRC_AXIS = 4, SRC_KEY = 8 };

static const char *const k_names[BTN_COUNT] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y",
                                               "L1", "R1", "L2", "R2", "SELECT", "START", "MENU", "HOME",
                                               "L3", "R3", "F1", "F2"};
static const char *const k_keys[BTN_COUNT] = {NULL, NULL, NULL, NULL, "btn_a", "btn_b", "btn_x", "btn_y",
                                              "btn_l1", "btn_r1", NULL, NULL, "btn_select", "btn_start",
                                              "btn_menu", "btn_home", "btn_l3", "btn_r3", "btn_f1", "btn_f2"};
/* Defaults: SDL joystick indices of the firmware's virtual pad as documented
 * by community firmware for tg5040-family devices (to be confirmed on the
 * Brick Pro: Configurações > Controles > Testar controles shows raw values). */
static const int k_defaults[BTN_COUNT] = {-1, -1, -1, -1, 1, 0, 3, 2, 4, 5, -1, -1, 6, 7, 8, 15, 9, 10, 11, 12};

const char *input_button_name(TmButton b)
{
    return (b >= 0 && b < BTN_COUNT) ? k_names[b] : "?";
}

void input_reload(const TmIni *s)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        g_map[i] = k_defaults[i];
        if (k_keys[i] && s) {
            long v = tm_ini_get_long(s, "input", k_keys[i], k_defaults[i]);
            g_map[i] = (v >= -1 && v < 32) ? (int)v : k_defaults[i];
        }
    }
    if (s) {
        g_swap_ab = (int)tm_ini_get_long(s, "input", "swap_ab", 0);
        g_axis_l2 = (int)tm_ini_get_long(s, "input", "axis_l2", 2);
        g_axis_r2 = (int)tm_ini_get_long(s, "input", "axis_r2", 5);
    }
}

static void open_pads(void)
{
    for (int i = 0; i < g_npads; i++)
        if (g_pads[i])
            SDL_JoystickClose(g_pads[i]);
    g_npads = 0;
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n && g_npads < 4; i++) {
        SDL_Joystick *j = SDL_JoystickOpen(i);
        if (!j)
            continue;
        g_pads[g_npads++] = j;
        if (g_npads == 1)
            tm_strlcpy(g_st.pad_name, SDL_JoystickName(j) ? SDL_JoystickName(j) : "?", sizeof g_st.pad_name);
        LOGI("input: pad %d '%s' buttons=%d axes=%d hats=%d", i, SDL_JoystickName(j), SDL_JoystickNumButtons(j),
             SDL_JoystickNumAxes(j), SDL_JoystickNumHats(j));
    }
}

void input_init(const TmIni *s)
{
    memset(&g_st, 0, sizeof g_st);
    input_reload(s);
    SDL_JoystickEventState(SDL_ENABLE);
    open_pads();
}

void input_quit(void)
{
    for (int i = 0; i < g_npads; i++)
        if (g_pads[i])
            SDL_JoystickClose(g_pads[i]);
    g_npads = 0;
}

static TmButton logical(TmButton b)
{
    if (g_swap_ab && b == BTN_A)
        return BTN_B;
    if (g_swap_ab && b == BTN_B)
        return BTN_A;
    return b;
}

/* Updates one action from one source; returns the action on a new press. */
static TmButton set_src(TmButton b, int src, int down)
{
    if (b == BTN_NONE)
        return BTN_NONE;
    int was = g_src[b] != 0;
    if (down)
        g_src[b] |= src;
    else
        g_src[b] &= ~src;
    int now = g_src[b] != 0;
    g_st.pressed[b] = now;
    if (now && !was) {
        g_held_since[b] = g_last_repeat[b] = tm_now_ms();
        return b;
    }
    return BTN_NONE;
}

static TmButton from_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return BTN_UP;
    case SDLK_DOWN: return BTN_DOWN;
    case SDLK_LEFT: return BTN_LEFT;
    case SDLK_RIGHT: return BTN_RIGHT;
    case SDLK_RETURN: case SDLK_a: return BTN_A;
    case SDLK_BACKSPACE: case SDLK_ESCAPE: case SDLK_b: return BTN_B;
    case SDLK_x: return BTN_X;
    case SDLK_y: return BTN_Y;
    case SDLK_q: return BTN_L1;
    case SDLK_w: return BTN_R1;
    case SDLK_1: return BTN_L2;
    case SDLK_2: return BTN_R2;
    case SDLK_TAB: return BTN_SELECT;
    case SDLK_SPACE: return BTN_START;
    case SDLK_m: return BTN_MENU;
    case SDLK_h: return BTN_HOME;
    default: return BTN_NONE;
    }
}

TmButton input_event(const SDL_Event *e)
{
    TmButton r = BTN_NONE;
    switch (e->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        if (e->key.repeat)
            return BTN_NONE;
        r = set_src(from_key(e->key.keysym.sym), SRC_KEY, e->type == SDL_KEYDOWN);
        return r == BTN_NONE ? r : logical(r);
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        int idx = e->jbutton.button, down = e->type == SDL_JOYBUTTONDOWN;
        if (idx < 32)
            g_st.raw_buttons[idx] = down;
        for (int b = 0; b < BTN_COUNT; b++)
            if (g_map[b] == idx) {
                TmButton t = set_src((TmButton)b, SRC_BUTTON, down);
                if (t != BTN_NONE)
                    r = logical(t);
            }
        return r;
    }
    case SDL_JOYHATMOTION: {
        int v = e->jhat.value;
        g_st.raw_hat = v;
        TmButton a = set_src(BTN_UP, SRC_HAT, (v & SDL_HAT_UP) != 0);
        TmButton b = set_src(BTN_DOWN, SRC_HAT, (v & SDL_HAT_DOWN) != 0);
        TmButton c = set_src(BTN_LEFT, SRC_HAT, (v & SDL_HAT_LEFT) != 0);
        TmButton d = set_src(BTN_RIGHT, SRC_HAT, (v & SDL_HAT_RIGHT) != 0);
        return a != BTN_NONE ? a : b != BTN_NONE ? b : c != BTN_NONE ? c : d;
    }
    case SDL_JOYAXISMOTION: {
        int ax = e->jaxis.axis, v = e->jaxis.value;
        if (ax < 8)
            g_st.raw_axes[ax] = v;
        if (ax == g_axis_l2)
            return set_src(BTN_L2, SRC_AXIS, v > (g_st.pressed[BTN_L2] ? AXIS_OFF : AXIS_ON));
        if (ax == g_axis_r2)
            return set_src(BTN_R2, SRC_AXIS, v > (g_st.pressed[BTN_R2] ? AXIS_OFF : AXIS_ON));
        if (ax == g_axis_lx) {
            TmButton a = set_src(BTN_LEFT, SRC_AXIS, v < -(g_src[BTN_LEFT] & SRC_AXIS ? AXIS_OFF : AXIS_ON));
            TmButton b = set_src(BTN_RIGHT, SRC_AXIS, v > (g_src[BTN_RIGHT] & SRC_AXIS ? AXIS_OFF : AXIS_ON));
            return a != BTN_NONE ? a : b;
        }
        if (ax == g_axis_ly) {
            TmButton a = set_src(BTN_UP, SRC_AXIS, v < -(g_src[BTN_UP] & SRC_AXIS ? AXIS_OFF : AXIS_ON));
            TmButton b = set_src(BTN_DOWN, SRC_AXIS, v > (g_src[BTN_DOWN] & SRC_AXIS ? AXIS_OFF : AXIS_ON));
            return a != BTN_NONE ? a : b;
        }
        return BTN_NONE;
    }
    case SDL_JOYDEVICEADDED:
    case SDL_JOYDEVICEREMOVED:
        open_pads();
        return BTN_NONE;
    default:
        return BTN_NONE;
    }
}

TmButton input_repeat(void)
{
    static const TmButton rep[] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_L1, BTN_R1};
    uint64_t now = tm_now_ms();
    for (size_t i = 0; i < TM_ARRAY_LEN(rep); i++) {
        TmButton b = rep[i];
        if (!g_st.pressed[b])
            continue;
        if (now - g_held_since[b] >= REPEAT_DELAY_MS && now - g_last_repeat[b] >= REPEAT_RATE_MS) {
            g_last_repeat[b] = now;
            return b;
        }
    }
    return BTN_NONE;
}

int input_any_held(void)
{
    for (int b = 0; b < BTN_COUNT; b++)
        if (g_st.pressed[b])
            return 1;
    return 0;
}

const TmInputState *input_state(void)
{
    return &g_st;
}
