/* LED control through the firmware's own driver interface.
 *
 * TriMux only writes to attribute files that exist under the detected
 * /sys/class/led_anim directory (TrimUI "led_anim" driver, used by the stock
 * firmware's runtrimui.sh and keymon) and only for zones whose files are all
 * present. Nothing is written to raw registers or guessed paths. */
#ifndef TRIMUX_LEDS_H
#define TRIMUX_LEDS_H

#include <stddef.h>

#define TM_LED_MAX_ZONES 8

enum { TM_LED_EFFECT_OFF = 0, TM_LED_EFFECT_BREATHE = 2, TM_LED_EFFECT_STATIC = 4 };

typedef struct {
    char id[16];            /* driver zone suffix: m, lr, f1, f2, rear ... */
    char name_key[32];      /* i18n key */
    int has_brightness;
    char brightness_attr[48];
} TmLedZone;

typedef struct {
    int available;
    char dir[512];
    int has_enable;
    TmLedZone zones[TM_LED_MAX_ZONES];
    size_t nzones;
} TmLeds;

typedef struct {
    int on;
    unsigned color;     /* 0xRRGGBB */
    int brightness;     /* 0..100 */
    int effect;         /* TM_LED_EFFECT_* */
} TmLedSetting;

int tm_leds_detect(TmLeds *leds);
const TmLedZone *tm_leds_zone(const TmLeds *leds, const char *id);
/* Validates and applies one zone. Returns -1 for unknown zone or write error. */
int tm_leds_apply(const TmLeds *leds, const char *zone, const TmLedSetting *s);
int tm_leds_effect_valid(int effect);

#endif
