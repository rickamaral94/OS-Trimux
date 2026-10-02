#define _GNU_SOURCE
#include "power.h"
#include "log.h"
#include "util.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const TmPowerProfile k_profiles[] = {
    {"economy", "power.economy", "power.economy.desc", 1200000L, 0},
    {"balanced", "power.balanced", "power.balanced.desc", 1608000L, 0},
    {"performance", "power.performance", "power.performance.desc", TM_POWER_HARD_CAP_KHZ, 1008000L},
};

const char *tm_sysfs_root(void)
{
    const char *r = getenv("TRIMUX_SYSFS_ROOT");
    return r ? r : "";
}

size_t tm_power_profiles(const TmPowerProfile **out)
{
    *out = k_profiles;
    return TM_ARRAY_LEN(k_profiles);
}

const TmPowerProfile *tm_power_profile(const char *id)
{
    for (size_t i = 0; id && i < TM_ARRAY_LEN(k_profiles); i++)
        if (strcmp(k_profiles[i].id, id) == 0)
            return &k_profiles[i];
    return NULL;
}

static int join_root(char *out, size_t size, const char *path)
{
    return tm_snprintf(out, size, "%s%s", tm_sysfs_root(), path);
}

static int cmp_long(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return (x > y) - (x < y);
}

static void find_temp_sensor(TmPowerCaps *caps)
{
    char base[512];
    if (join_root(base, sizeof base, "/sys/class/thermal") != 0)
        return;
    DIR *d = opendir(base);
    if (!d)
        return;
    struct dirent *e;
    char fallback[512] = "";
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "thermal_zone", 12) != 0)
            continue;
        char type_path[512], temp_path[512], type[64];
        long t;
        if (tm_snprintf(type_path, sizeof type_path, "%s/%s/type", base, e->d_name) != 0 ||
            tm_snprintf(temp_path, sizeof temp_path, "%s/%s/temp", base, e->d_name) != 0)
            continue;
        if (tm_read_long(temp_path, &t) != 0 || t < 5000 || t > 130000)
            continue; /* implausible reading: ignore this zone */
        if (tm_read_line(type_path, type, sizeof type) == 0 && strstr(type, "cpu")) {
            tm_strlcpy(caps->temp_path, temp_path, sizeof caps->temp_path);
            break;
        }
        if (!fallback[0])
            tm_strlcpy(fallback, temp_path, sizeof fallback);
    }
    closedir(d);
    if (!caps->temp_path[0] && fallback[0])
        tm_strlcpy(caps->temp_path, fallback, sizeof caps->temp_path);
    caps->has_temp = caps->temp_path[0] != '\0';
}

int tm_power_detect(TmPowerCaps *caps)
{
    memset(caps, 0, sizeof *caps);
    static const char *const policies[] = {"/sys/devices/system/cpu/cpufreq/policy0",
                                           "/sys/devices/system/cpu/cpu0/cpufreq"};
    for (size_t i = 0; i < TM_ARRAY_LEN(policies) && !caps->policy_dir[0]; i++) {
        char dir[512], f[600];
        if (join_root(dir, sizeof dir, policies[i]) != 0)
            continue;
        if (tm_snprintf(f, sizeof f, "%s/scaling_max_freq", dir) == 0 && tm_file_exists(f))
            tm_strlcpy(caps->policy_dir, dir, sizeof caps->policy_dir);
    }
    find_temp_sensor(caps);
    if (!caps->policy_dir[0]) {
        tm_strlcpy(caps->reason, "power.reason.no_cpufreq", sizeof caps->reason);
        return -1;
    }
    char f[600], buf[512];
    tm_snprintf(f, sizeof f, "%s/cpuinfo_min_freq", caps->policy_dir);
    tm_read_long(f, &caps->cpuinfo_min);
    tm_snprintf(f, sizeof f, "%s/cpuinfo_max_freq", caps->policy_dir);
    tm_read_long(f, &caps->cpuinfo_max);
    tm_snprintf(f, sizeof f, "%s/scaling_available_governors", caps->policy_dir);
    if (tm_read_line(f, caps->governors, sizeof caps->governors) != 0)
        caps->governors[0] = '\0';
    tm_snprintf(f, sizeof f, "%s/scaling_available_frequencies", caps->policy_dir);
    if (tm_read_line(f, buf, sizeof buf) == 0) {
        char *save = NULL;
        for (char *tok = strtok_r(buf, " ", &save); tok && caps->nfreqs < TM_POWER_MAX_FREQS;
             tok = strtok_r(NULL, " ", &save)) {
            long v = strtol(tok, NULL, 10);
            if (v > 0)
                caps->freqs[caps->nfreqs++] = v;
        }
        qsort(caps->freqs, caps->nfreqs, sizeof(long), cmp_long);
    }
    if (caps->nfreqs == 0 && (caps->cpuinfo_min <= 0 || caps->cpuinfo_max <= 0)) {
        tm_strlcpy(caps->reason, "power.reason.no_freq_list", sizeof caps->reason);
        return -1;
    }
    char mx[600], mn[600];
    tm_snprintf(mx, sizeof mx, "%s/scaling_max_freq", caps->policy_dir);
    tm_snprintf(mn, sizeof mn, "%s/scaling_min_freq", caps->policy_dir);
    if (access(mx, W_OK) != 0 || access(mn, W_OK) != 0) {
        tm_strlcpy(caps->reason, "power.reason.not_writable", sizeof caps->reason);
        return -1;
    }
    caps->has_cpufreq = 1;
    return 0;
}

static int has_governor(const TmPowerCaps *caps, const char *g)
{
    char buf[256];
    tm_strlcpy(buf, caps->governors, sizeof buf);
    char *save = NULL;
    for (char *tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(NULL, " ", &save))
        if (strcmp(tok, g) == 0)
            return 1;
    return 0;
}

int tm_power_plan(const TmPowerCaps *caps, const TmPowerProfile *p, TmPowerTarget *t)
{
    if (!caps->has_cpufreq || !p)
        return -1;
    memset(t, 0, sizeof *t);
    long cap = p->max_khz < TM_POWER_HARD_CAP_KHZ ? p->max_khz : TM_POWER_HARD_CAP_KHZ;
    if (caps->nfreqs > 0) {
        long lo = caps->freqs[0], hi = -1, floor = -1;
        for (size_t i = 0; i < caps->nfreqs; i++) {
            if (caps->freqs[i] <= cap)
                hi = caps->freqs[i];
            if (floor < 0 && caps->freqs[i] >= p->min_floor_khz)
                floor = caps->freqs[i];
        }
        if (hi < 0)
            return -1; /* every OPP above the cap: refuse rather than exceed */
        if (floor < 0 || floor > hi)
            floor = lo;
        t->min_khz = p->min_floor_khz > 0 ? floor : lo;
        t->max_khz = hi;
    } else {
        long hi = caps->cpuinfo_max < cap ? caps->cpuinfo_max : cap;
        long lo = caps->cpuinfo_min;
        if (hi < lo)
            return -1;
        t->max_khz = hi;
        t->min_khz = p->min_floor_khz > lo && p->min_floor_khz <= hi ? p->min_floor_khz : lo;
    }
    /* ondemand is the firmware default; never pin "performance" */
    static const char *const prefs[] = {"ondemand", "schedutil", "interactive", "conservative"};
    for (size_t i = 0; i < TM_ARRAY_LEN(prefs) && !t->governor[0]; i++)
        if (has_governor(caps, prefs[i]))
            tm_strlcpy(t->governor, prefs[i], sizeof t->governor);
    return 0;
}

static int write_long(const char *dir, const char *name, long v)
{
    char path[600], val[32];
    tm_snprintf(path, sizeof path, "%s/%s", dir, name);
    tm_snprintf(val, sizeof val, "%ld", v);
    return tm_write_str(path, val);
}

int tm_power_read(const TmPowerCaps *caps, long *cur, long *min, long *max, char *gov, size_t gov_size)
{
    if (!caps->policy_dir[0])
        return -1;
    char f[600];
    long dummy;
    tm_snprintf(f, sizeof f, "%s/scaling_cur_freq", caps->policy_dir);
    if (tm_read_long(f, cur ? cur : &dummy) != 0 && cur)
        *cur = -1;
    tm_snprintf(f, sizeof f, "%s/scaling_min_freq", caps->policy_dir);
    if (tm_read_long(f, min ? min : &dummy) != 0 && min)
        *min = -1;
    tm_snprintf(f, sizeof f, "%s/scaling_max_freq", caps->policy_dir);
    if (tm_read_long(f, max ? max : &dummy) != 0 && max)
        *max = -1;
    if (gov) {
        tm_snprintf(f, sizeof f, "%s/scaling_governor", caps->policy_dir);
        if (tm_read_line(f, gov, gov_size) != 0)
            tm_strlcpy(gov, "?", gov_size);
    }
    return 0;
}

int tm_power_apply(const TmPowerCaps *caps, const TmPowerProfile *p)
{
    TmPowerTarget t;
    if (tm_power_plan(caps, p, &t) != 0) {
        LOGW("power: cannot plan profile %s", p ? p->id : "(null)");
        return -1;
    }
    if (t.governor[0]) {
        char path[600];
        tm_snprintf(path, sizeof path, "%s/scaling_governor", caps->policy_dir);
        if (tm_write_str(path, t.governor) != 0)
            LOGW("power: governor %s not accepted", t.governor);
    }
    long cur_max = -1;
    tm_power_read(caps, NULL, NULL, &cur_max, NULL, 0);
    int rc;
    /* the kernel rejects min > max, so order the writes */
    if (cur_max >= 0 && t.min_khz > cur_max) {
        rc = write_long(caps->policy_dir, "scaling_max_freq", t.max_khz);
        rc |= write_long(caps->policy_dir, "scaling_min_freq", t.min_khz);
    } else {
        rc = write_long(caps->policy_dir, "scaling_min_freq", t.min_khz);
        rc |= write_long(caps->policy_dir, "scaling_max_freq", t.max_khz);
    }
    long rmin = -1, rmax = -1;
    tm_power_read(caps, NULL, &rmin, &rmax, NULL, 0);
    if (rmax > TM_POWER_HARD_CAP_KHZ) {
        /* never leave the CPU above the cap, whatever happened */
        write_long(caps->policy_dir, "scaling_max_freq", t.max_khz);
        LOGE("power: max freq %ld above cap after apply, forced back", rmax);
        return -1;
    }
    if (rc != 0 || rmax != t.max_khz || rmin != t.min_khz) {
        LOGW("power: profile %s applied partially (min=%ld max=%ld want %ld..%ld)", p->id, rmin, rmax,
             t.min_khz, t.max_khz);
        return -1;
    }
    LOGI("power: profile %s gov=%s min=%ld max=%ld", p->id, t.governor, t.min_khz, t.max_khz);
    return 0;
}

long tm_power_temp_mc(const TmPowerCaps *caps)
{
    long t;
    if (!caps->has_temp || tm_read_long(caps->temp_path, &t) != 0 || t < 5000 || t > 130000)
        return -1;
    return t;
}
