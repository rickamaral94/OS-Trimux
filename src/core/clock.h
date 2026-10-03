/* Date, time and time zone.
 *
 * Done the way the stock firmware does it: the clock is set with BusyBox
 * "date" and saved to the RTC with "hwclock -w -u" (the same call the
 * firmware's own shutdown script makes, so the RTC stays in UTC); internet
 * time comes from BusyBox "ntpd -q". The time zone chosen in TriMux is
 * passed to TriMux's processes in TZ (it is not written to the internal
 * memory); without a choice, the firmware's zone (/etc/localtime) is used. */
#ifndef TRIMUX_CLOCK_H
#define TRIMUX_CLOCK_H

#include <stddef.h>
#include <time.h>

typedef struct {
    const char *id;    /* zoneinfo name */
    const char *label; /* shown in the menu */
} TmZone;

extern const TmZone tm_zones[];
extern const size_t tm_nzones;

/* Index in tm_zones, or -1. */
int tm_zone_index(const char *id);
/* TZ value for a zone (":/usr/share/zoneinfo/<id>"); -1 if unknown or the
 * zoneinfo file is missing. */
int tm_zone_tz(const char *id, char *out, size_t size);
/* Applies a zone to this process (empty = firmware zone). */
void tm_zone_apply_env(const char *id);

/* Sets the clock to a local date/time in the current TZ and saves it to the
 * RTC. Returns 0, or -1 (invalid date or the firmware tools failed). */
int tm_clock_set_local(int year, int mon, int day, int hour, int min);
/* One internet time query (BusyBox ntpd -q), then saves the RTC. */
int tm_clock_sync(int timeout_s);
/* Days in a month (mon 1..12). */
int tm_days_in_month(int year, int mon);

#endif
