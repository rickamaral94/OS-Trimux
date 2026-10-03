#define _GNU_SOURCE
#include "leds.h"
#include "log.h"
#include "power.h" /* tm_sysfs_root */
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Zones written by the stock firmware v1.1.1 (runtrimui.sh, keymon,
 * hardwareservice). Labels follow TrimUI's product page for the Brick Pro:
 * top light bar, joystick rings, F1/F2 indicators, shoulder lighting. */
static const struct {
    const char *id;
    const char *name_key;
} k_known[] = {
    {"m", "leds.zone.m"},       {"lr", "leds.zone.lr"},   {"f1", "leds.zone.f1"},
    {"f2", "leds.zone.f2"},     {"rear", "leds.zone.rear"},
};

static int attr_path(const TmLeds *leds, char *out, size_t size, const char *attr)
{
    return tm_snprintf(out, size, "%s/%s", leds->dir, attr);
}

static int attr_writable(const TmLeds *leds, const char *attr)
{
    char p[600];
    return attr_path(leds, p, sizeof p, attr) == 0 && access(p, W_OK) == 0;
}

int tm_leds_detect(TmLeds *leds)
{
    memset(leds, 0, sizeof *leds);
    if (tm_snprintf(leds->dir, sizeof leds->dir, "%s/sys/class/led_anim", tm_sysfs_root()) != 0 ||
        !tm_dir_exists(leds->dir))
        return -1;
    leds->has_enable = attr_writable(leds, "effect_enable");
    for (size_t i = 0; i < TM_ARRAY_LEN(k_known) && leds->nzones < TM_LED_MAX_ZONES; i++) {
        const char *z = k_known[i].id;
        char a[48], b[48], c[48];
        snprintf(a, sizeof a, "effect_%s", z);
        snprintf(b, sizeof b, "effect_rgb_hex_%s", z);
        snprintf(c, sizeof c, "effect_duration_%s", z);
        if (!attr_writable(leds, a) || !attr_writable(leds, b) || !attr_writable(leds, c))
            continue;
        TmLedZone *zone = &leds->zones[leds->nzones++];
        tm_strlcpy(zone->id, z, sizeof zone->id);
        tm_strlcpy(zone->name_key, k_known[i].name_key, sizeof zone->name_key);
        /* brightness attribute naming differs per zone in the driver */
        char cand[3][48];
        int nc = 0;
        snprintf(cand[nc++], sizeof cand[0], "max_scale_%s", z);
        if (strcmp(z, "f1") == 0 || strcmp(z, "f2") == 0)
            snprintf(cand[nc++], sizeof cand[0], "max_scale_f1f2");
        if (strcmp(z, "m") == 0)
            snprintf(cand[nc++], sizeof cand[0], "max_scale");
        for (int k = 0; k < nc && !zone->has_brightness; k++)
            if (attr_writable(leds, cand[k])) {
                zone->has_brightness = 1;
                tm_strlcpy(zone->brightness_attr, cand[k], sizeof zone->brightness_attr);
            }
    }
    leds->available = leds->nzones > 0;
    return leds->available ? 0 : -1;
}

const TmLedZone *tm_leds_zone(const TmLeds *leds, const char *id)
{
    for (size_t i = 0; id && i < leds->nzones; i++)
        if (strcmp(leds->zones[i].id, id) == 0)
            return &leds->zones[i];
    return NULL;
}

int tm_leds_effect_valid(int effect)
{
    return effect == TM_LED_EFFECT_OFF || effect == TM_LED_EFFECT_BREATHE || effect == TM_LED_EFFECT_STATIC;
}

static int write_attr(const TmLeds *leds, const char *attr, const char *value)
{
    char p[600];
    if (attr_path(leds, p, sizeof p, attr) != 0)
        return -1;
    if (tm_write_str(p, value) != 0) {
        LOGW("leds: writing \"%s\" to %s failed: %s", value, attr, strerror(errno));
        return -1;
    }
    return 0;
}

int tm_leds_apply(const TmLeds *leds, const char *zone_id, const TmLedSetting *s)
{
    const TmLedZone *z = tm_leds_zone(leds, zone_id);
    if (!z || !s)
        return -1;
    int effect = s->on ? s->effect : TM_LED_EFFECT_OFF;
    if (!tm_leds_effect_valid(effect))
        effect = TM_LED_EFFECT_STATIC;
    int bright = s->brightness < 0 ? 0 : s->brightness > 100 ? 100 : s->brightness;
    char attr[48], val[32];
    int rc = 0;
    if (effect != TM_LED_EFFECT_OFF) {
        /* "enable" is the firmware's master LED switch: hardwareservice sets
         * it from the stock "LED" setting (system.json ledswitch), so with
         * that setting off nothing lights up. Turning a zone on here means
         * the user wants it on while TriMux runs (not saved in the firmware). */
        if (attr_writable(leds, "enable"))
            rc |= write_attr(leds, "enable", "1");
        if (leds->has_enable)
            rc |= write_attr(leds, "effect_enable", "1");
        /* frame animations take over the effects (the firmware's own
         * fn_editor scripts turn them off before using effects) */
        if (attr_writable(leds, "anim_frames_enable"))
            rc |= write_attr(leds, "anim_frames_enable", "0");
    }
    if (z->has_brightness) {
        snprintf(val, sizeof val, "%d", bright);
        rc |= write_attr(leds, z->brightness_attr, val);
    }
    snprintf(attr, sizeof attr, "effect_rgb_hex_%s", z->id);
    snprintf(val, sizeof val, "%06X", s->color & 0xFFFFFFu);
    rc |= write_attr(leds, attr, val);
    snprintf(attr, sizeof attr, "effect_duration_%s", z->id);
    rc |= write_attr(leds, attr, "1000");
    /* Repetitions: the firmware leaves 1 here at boot (runtrimui.sh), which
     * ends an effect after one second, and never writes a negative value
     * itself; 30000 is the count its low-battery script uses (about 8 h at
     * 1 s, and the menu applies the settings again whenever it starts). */
    snprintf(attr, sizeof attr, "effect_cycles_%s", z->id);
    if (attr_writable(leds, attr))
        rc |= write_attr(leds, attr, "30000");
    snprintf(attr, sizeof attr, "effect_%s", z->id);
    snprintf(val, sizeof val, "%d", effect);
    rc |= write_attr(leds, attr, val);
    if (rc)
        LOGW("leds: zone %s partially applied", z->id);
    return rc ? -1 : 0;
}
