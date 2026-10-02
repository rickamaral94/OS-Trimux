#define _GNU_SOURCE
#include "buttons.h"
#include "power.h" /* tm_sysfs_root */
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *const k_keys[TM_KEY_COUNT] = {"none", "quick", "favorite", "search",
                                                 "random", "recent", "power", "leds"};
static const char *const k_switch[TM_SWITCH_COUNT] = {"none", "economy", "leds_off", "mute", "boost"};

const char *tm_key_action_id(TmKeyAction a)
{
    return (a >= 0 && a < TM_KEY_COUNT) ? k_keys[a] : "none";
}

TmKeyAction tm_key_action_parse(const char *id, TmKeyAction def)
{
    for (int i = 0; id && i < TM_KEY_COUNT; i++)
        if (strcmp(id, k_keys[i]) == 0)
            return (TmKeyAction)i;
    return def;
}

const char *tm_switch_action_id(TmSwitchAction a)
{
    return (a >= 0 && a < TM_SWITCH_COUNT) ? k_switch[a] : "none";
}

TmSwitchAction tm_switch_action_parse(const char *id)
{
    for (int i = 0; id && i < TM_SWITCH_COUNT; i++)
        if (strcmp(id, k_switch[i]) == 0)
            return (TmSwitchAction)i;
    return TM_SWITCH_NONE;
}

int tm_switch_raw(void)
{
    char p[512];
    long v;
    snprintf(p, sizeof p, "%s/sys/class/gpio/gpio243/value", tm_sysfs_root());
    if (tm_read_long(p, &v) != 0 || (v != 0 && v != 1))
        return -1;
    return (int)v;
}

int tm_switch_on(const TmIni *s)
{
    int raw = tm_switch_raw();
    if (raw < 0)
        return -1;
    return tm_ini_get_long(s, "buttons", "switch_invert", 0) ? !raw : raw;
}

TmSwitchAction tm_switch_action(const TmIni *s)
{
    TmSwitchAction a = tm_switch_action_parse(tm_ini_get(s, "buttons", "switch", "none"));
    /* boost only counts after the user confirmed the warning in the menu */
    if (a == TM_SWITCH_BOOST && !tm_ini_get_long(s, "power", "boost_ack", 0))
        return TM_SWITCH_NONE;
    return a;
}

TmSwitchAction tm_switch_active(const TmIni *s)
{
    return tm_switch_on(s) == 1 ? tm_switch_action(s) : TM_SWITCH_NONE;
}

static void mute_path(char *p, size_t size)
{
    snprintf(p, size, "%s/sys/class/speaker/mute", tm_sysfs_root());
}

int tm_speaker_mute_available(void)
{
    char p[512];
    mute_path(p, sizeof p);
    return access(p, W_OK) == 0;
}

int tm_speaker_mute(int on)
{
    char p[512];
    mute_path(p, sizeof p);
    return tm_write_str(p, on ? "1" : "0");
}
