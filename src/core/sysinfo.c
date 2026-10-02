#define _GNU_SOURCE
#include "sysinfo.h"
#include "power.h" /* tm_sysfs_root */
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

static void read_battery(TmSysInfo *si)
{
    char base[512];
    snprintf(base, sizeof base, "%s/sys/class/power_supply", tm_sysfs_root());
    DIR *d = opendir(base);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char p[700], type[32], status[32];
        long cap;
        snprintf(p, sizeof p, "%s/%s/type", base, e->d_name);
        if (tm_read_line(p, type, sizeof type) != 0 || strcmp(type, "Battery") != 0)
            continue;
        snprintf(p, sizeof p, "%s/%s/capacity", base, e->d_name);
        if (tm_read_long(p, &cap) == 0 && cap >= 0 && cap <= 100)
            si->battery_pct = (int)cap;
        snprintf(p, sizeof p, "%s/%s/status", base, e->d_name);
        if (tm_read_line(p, status, sizeof status) == 0)
            si->charging = (strcmp(status, "Charging") == 0 || strcmp(status, "Full") == 0) ? 1 : 0;
        break;
    }
    closedir(d);
}

int tm_battery_read(int *pct, int *charging)
{
    TmSysInfo si;
    si.battery_pct = -1;
    si.charging = -1;
    read_battery(&si);
    if (pct)
        *pct = si.battery_pct;
    if (charging)
        *charging = si.charging;
    return si.battery_pct >= 0 ? 0 : -1;
}

static void read_meminfo(TmSysInfo *si)
{
    char p[512];
    snprintf(p, sizeof p, "%s/proc/meminfo", tm_sysfs_root());
    char *txt = tm_read_file(p, 65536, NULL);
    if (!txt)
        return;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        long v;
        if (sscanf(line, "MemTotal: %ld", &v) == 1)
            si->mem_total_kb = v;
        else if (sscanf(line, "MemAvailable: %ld", &v) == 1)
            si->mem_avail_kb = v;
        else if (sscanf(line, "SwapTotal: %ld", &v) == 1)
            si->swap_total_kb = v;
        else if (sscanf(line, "SwapFree: %ld", &v) == 1)
            si->swap_free_kb = v;
    }
    free(txt);
}

static void read_mount(TmSysInfo *si, const char *sd_root)
{
    char p[512];
    snprintf(p, sizeof p, "%s/proc/mounts", tm_sysfs_root());
    char *txt = tm_read_file(p, 262144, NULL);
    if (!txt)
        return;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char dev[256], mnt[256], fs[32], opts[512];
        if (sscanf(line, "%255s %255s %31s %511s", dev, mnt, fs, opts) != 4)
            continue;
        if (strcmp(mnt, sd_root) != 0)
            continue;
        si->sd_mounted = 1;
        tm_strlcpy(si->sd_fstype, fs, sizeof si->sd_fstype);
        si->sd_readonly = (strncmp(opts, "ro,", 3) == 0 || strcmp(opts, "ro") == 0);
    }
    free(txt);
}

int tm_sysinfo_is_brick_pro(void)
{
    /* The stock launcher embeds its model string ("Trimui Brick Pro"). Reading
     * it avoids depending on any undocumented ID register. Cached: the file
     * is several MB and does not change while running. */
    static int cached = -1;
    if (cached >= 0)
        return cached;
    char p[512];
    snprintf(p, sizeof p, "%s/usr/trimui/bin/MainUI", tm_sysfs_root());
    size_t len = 0;
    char *bin = tm_read_file(p, 16u << 20, &len);
    if (!bin)
        return 0;
    static const char needle[] = "Trimui Brick Pro";
    int found = memmem(bin, len, needle, sizeof needle) != NULL; /* includes NUL: exact string */
    free(bin);
    cached = found;
    return found;
}

void tm_sysinfo_read(TmSysInfo *si, const char *sd_root)
{
    memset(si, 0, sizeof *si);
    si->battery_pct = -1;
    si->charging = -1;
    read_battery(si);
    read_meminfo(si);
    read_mount(si, sd_root);
    struct statvfs vs;
    if (statvfs(sd_root, &vs) == 0) {
        si->sd_total = (unsigned long long)vs.f_blocks * vs.f_frsize;
        si->sd_free = (unsigned long long)vs.f_bavail * vs.f_frsize;
    }
    char p[512];
    snprintf(p, sizeof p, "%s/etc/version", tm_sysfs_root());
    if (tm_read_line(p, si->firmware, sizeof si->firmware) != 0)
        tm_strlcpy(si->firmware, "?", sizeof si->firmware);
    tm_strlcpy(si->model, tm_sysinfo_is_brick_pro() ? "TrimUI Brick Pro (TG4040)" : "?", sizeof si->model);
}

void tm_format_bytes(unsigned long long b, char *out, size_t size)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)b;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        u++;
    }
    char tmp[32];
    snprintf(tmp, sizeof tmp, u == 0 ? "%.0f %s" : "%.1f %s", v, units[u]);
    for (char *c = tmp; *c; c++)
        if (*c == '.')
            *c = ','; /* pt-BR decimal separator */
    tm_strlcpy(out, tmp, size);
}
