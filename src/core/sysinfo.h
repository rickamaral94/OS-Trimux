/* Read-only device status: battery, memory, storage, model and firmware. */
#ifndef TRIMUX_SYSINFO_H
#define TRIMUX_SYSINFO_H

#include <stddef.h>

typedef struct {
    int battery_pct;      /* -1 unknown */
    int charging;         /* 1 charging, 0 not, -1 unknown */
    long mem_total_kb, mem_avail_kb, swap_total_kb, swap_free_kb;
    unsigned long long sd_total, sd_free; /* bytes */
    char sd_fstype[16];
    int sd_mounted, sd_readonly;
    char model[64];
    char firmware[32];
} TmSysInfo;

/* sd_root: mount point of the card (e.g. /mnt/SDCARD). */
void tm_sysinfo_read(TmSysInfo *si, const char *sd_root);
/* 1 if the firmware identifies itself as a TrimUI Brick Pro (TG4040). */
int tm_sysinfo_is_brick_pro(void);
/* Formats a byte count as "12,3 GB" style text. */
void tm_format_bytes(unsigned long long bytes, char *out, size_t size);

#endif
