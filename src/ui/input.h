/* Physical controls -> menu actions. Works with the firmware's virtual pad
 * ("TRIMUI Player1", created by trimui_inputd) through the SDL joystick API,
 * and with a keyboard for host testing. Button numbers are configurable in
 * trimux.ini [input] because they could only be cross-checked against
 * community references, not measured on hardware by this project. */
#ifndef TRIMUX_INPUT_H
#define TRIMUX_INPUT_H

#include "../core/ini.h"
#include <SDL.h>

typedef enum {
    BTN_NONE = -1,
    BTN_UP = 0, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
    BTN_A, BTN_B, BTN_X, BTN_Y,
    BTN_L1, BTN_R1, BTN_L2, BTN_R2,
    BTN_SELECT, BTN_START, BTN_MENU, BTN_HOME,
    BTN_L3, BTN_R3, BTN_F1, BTN_F2,
    BTN_COUNT
} TmButton;

typedef struct {
    int pressed[BTN_COUNT]; /* current state */
    /* raw view for the controller test screen */
    int raw_buttons[32];
    int raw_axes[8];
    int raw_hat;
    char pad_name[64];
} TmInputState;

void input_init(const TmIni *settings);
void input_reload(const TmIni *settings);
void input_quit(void);
/* Feeds one SDL event. Returns a button "press" (with auto-repeat for the
 * d-pad) or BTN_NONE. */
TmButton input_event(const SDL_Event *e);
/* Generates repeat presses for held directions. Call once per loop. */
TmButton input_repeat(void);
const TmInputState *input_state(void);
const char *input_button_name(TmButton b);
int input_any_held(void);

#endif
