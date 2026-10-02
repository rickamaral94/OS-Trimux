/* CPU frequency profiles and temperature reading.
 *
 * Safety rules (enforced here, not in the UI):
 *  - only cpufreq policy files are written (governor, scaling_min/max_freq);
 *    thermal trip points, cooling devices and voltages are never touched;
 *  - the normal profiles never exceed TM_POWER_HARD_CAP_KHZ (1.8 GHz, the
 *    manufacturer's rated maximum for the Brick Pro);
 *  - the opt-in "boost" profile (side switch, after the user confirms) may use
 *    up to TM_POWER_BOOST_CAP_KHZ (2.0 GHz), the top entry of the firmware's own
 *    OPP table, which the stock firmware also uses in its performance modes.
 *    Never more, and only frequencies the kernel lists;
 *  - when the files are missing or unwritable the feature reports itself as
 *    unavailable instead of guessing other paths.
 */
#ifndef TRIMUX_POWER_H
#define TRIMUX_POWER_H

#include <stddef.h>

#define TM_POWER_HARD_CAP_KHZ 1800000L
#define TM_POWER_BOOST_CAP_KHZ 2000000L
#define TM_POWER_MAX_FREQS 32
#define TM_POWER_DEFAULT "balanced"

typedef struct {
    const char *id;       /* economy | balanced | performance */
    const char *name_key; /* i18n key */
    const char *desc_key;
    long max_khz;         /* upper bound, clamped by the hard cap */
    long min_floor_khz;   /* minimum frequency floor (0 = lowest available) */
    int boost;            /* allowed up to TM_POWER_BOOST_CAP_KHZ instead of the hard cap */
} TmPowerProfile;

typedef struct TmPowerCaps {
    int has_cpufreq;    /* policy files exist and are writable */
    int has_temp;       /* a plausible CPU temperature can be read */
    char policy_dir[512];
    char temp_path[512];
    long freqs[TM_POWER_MAX_FREQS];
    size_t nfreqs;
    long cpuinfo_min, cpuinfo_max;
    char governors[256];
    char reason[128];   /* why a feature is unavailable */
} TmPowerCaps;

typedef struct {
    char governor[32];
    long min_khz, max_khz;
} TmPowerTarget;

const char *tm_sysfs_root(void);
/* Normal profiles (all <= 1.8 GHz). "boost" is looked up by id only. */
size_t tm_power_profiles(const TmPowerProfile **out);
/* 1 if the kernel lists a frequency above the hard cap (boost is meaningful). */
int tm_power_boost_available(const struct TmPowerCaps *caps);
const TmPowerProfile *tm_power_profile(const char *id);

int tm_power_detect(TmPowerCaps *caps);
/* Computes the settings for a profile from detected capabilities. */
int tm_power_plan(const TmPowerCaps *caps, const TmPowerProfile *p, TmPowerTarget *t);
/* Applies and verifies. Returns 0 on success. */
int tm_power_apply(const TmPowerCaps *caps, const TmPowerProfile *p);
/* Reads current state (cur freq, min, max, governor). */
int tm_power_read(const TmPowerCaps *caps, long *cur, long *min, long *max, char *gov, size_t gov_size);

/* CPU temperature in milli-degrees C, or -1 when unavailable. */
long tm_power_temp_mc(const TmPowerCaps *caps);

#endif
