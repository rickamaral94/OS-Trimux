/* Extra controls of the Brick Pro: the two FN buttons (F1/F2) and the side
 * switch. The switch is the firmware's GPIO 243 (exported as an input by the
 * stock runtrimui.sh and read by keymon/hardwareservice); TriMux only reads it.
 * Speaker mute uses /sys/class/speaker/mute, the same file keymon writes. */
#ifndef TRIMUX_BUTTONS_H
#define TRIMUX_BUTTONS_H

#include "ini.h"

/* Actions for F1/F2 in the TriMux menu. */
typedef enum {
    TM_KEY_NONE = 0,
    TM_KEY_QUICK,     /* open quick menu */
    TM_KEY_FAVORITE,  /* toggle favorite of the selected game */
    TM_KEY_SEARCH,    /* open search */
    TM_KEY_RANDOM,    /* jump to a random game */
    TM_KEY_RECENT,    /* open recent games */
    TM_KEY_POWER,     /* cycle power profile */
    TM_KEY_LEDS,      /* LEDs on/off */
    TM_KEY_COUNT
} TmKeyAction;

/* What the side switch does while it is in the "on" position. */
typedef enum {
    TM_SWITCH_NONE = 0,
    TM_SWITCH_ECONOMY, /* force the Economy profile, also in games */
    TM_SWITCH_LEDS_OFF,
    TM_SWITCH_MUTE,    /* speaker muted (headphones keep working) */
    TM_SWITCH_COUNT
} TmSwitchAction;

const char *tm_key_action_id(TmKeyAction a);
TmKeyAction tm_key_action_parse(const char *id, TmKeyAction def);
const char *tm_switch_action_id(TmSwitchAction a);
TmSwitchAction tm_switch_action_parse(const char *id);

/* Raw switch value: 1, 0, or -1 when the GPIO is not readable. */
int tm_switch_raw(void);
/* 1 when the switch is "on" after applying [buttons] switch_invert, 0 off, -1 unknown. */
int tm_switch_on(const TmIni *settings);
TmSwitchAction tm_switch_action(const TmIni *settings);
/* Active action right now (NONE if the switch is off or unreadable). */
TmSwitchAction tm_switch_active(const TmIni *settings);

int tm_speaker_mute_available(void);
int tm_speaker_mute(int on);

#endif
