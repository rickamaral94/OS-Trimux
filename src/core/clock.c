#define _GNU_SOURCE
#include "clock.h"
#include "log.h"
#include "net.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const TmZone tm_zones[] = {
    {"America/Sao_Paulo", "Brasília, São Paulo (UTC−3)"},
    {"America/Fortaleza", "Nordeste (UTC−3)"},
    {"America/Belem", "Belém (UTC−3)"},
    {"America/Manaus", "Manaus (UTC−4)"},
    {"America/Cuiaba", "Cuiabá, Campo Grande (UTC−4)"},
    {"America/Porto_Velho", "Porto Velho (UTC−4)"},
    {"America/Rio_Branco", "Rio Branco (UTC−5)"},
    {"America/Noronha", "Fernando de Noronha (UTC−2)"},
    {"America/Argentina/Buenos_Aires", "Buenos Aires"},
    {"America/Montevideo", "Montevidéu"},
    {"America/Santiago", "Santiago"},
    {"America/Bogota", "Bogotá, Lima"},
    {"America/Mexico_City", "Cidade do México"},
    {"America/New_York", "Nova York, Miami"},
    {"America/Chicago", "Chicago"},
    {"America/Denver", "Denver"},
    {"America/Los_Angeles", "Los Angeles"},
    {"Europe/Lisbon", "Lisboa"},
    {"Europe/London", "Londres"},
    {"Europe/Madrid", "Madri, Paris, Roma"},
    {"Europe/Berlin", "Berlim"},
    {"Asia/Tokyo", "Tóquio"},
    {"Asia/Shanghai", "Pequim, Xangai"},
    {"Australia/Sydney", "Sydney"},
    {"UTC", "UTC"},
};
const size_t tm_nzones = sizeof tm_zones / sizeof tm_zones[0];

int tm_zone_index(const char *id)
{
    for (size_t i = 0; id && i < tm_nzones; i++)
        if (strcmp(tm_zones[i].id, id) == 0)
            return (int)i;
    return -1;
}

int tm_zone_tz(const char *id, char *out, size_t size)
{
    char rel[128], file[512];
    if (tm_zone_index(id) < 0 || tm_snprintf(rel, sizeof rel, "/usr/share/zoneinfo/%s", id) != 0 ||
        tm_fw_path(file, sizeof file, rel) != 0 || !tm_file_exists(file))
        return -1;
    return tm_snprintf(out, size, ":%s", file);
}

void tm_zone_apply_env(const char *id)
{
    char tz[600];
    if (id && id[0] && tm_zone_tz(id, tz, sizeof tz) == 0)
        setenv("TZ", tz, 1);
    else
        unsetenv("TZ");
    tzset();
}

int tm_days_in_month(int year, int mon)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mon < 1 || mon > 12)
        return 0;
    if (mon == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        return 29;
    return d[mon - 1];
}

/* RTC in UTC, exactly as the firmware's sysfixtime shutdown step saves it. */
static int save_rtc(void)
{
    char bb[512];
    tm_fw_path(bb, sizeof bb, "/bin/busybox");
    char *argv[] = {bb, "hwclock", "-w", "-u", "-f", "/dev/rtc0", NULL};
    int rc = tm_run(argv, NULL, 0, 10000);
    if (rc != 0)
        LOGW("clock: saving the RTC failed (%d)", rc);
    return rc == 0 ? 0 : -1;
}

int tm_clock_set_local(int year, int mon, int day, int hour, int min)
{
    if (year < 2024 || year > 2099 || mon < 1 || mon > 12 || day < 1 || day > tm_days_in_month(year, mon) ||
        hour < 0 || hour > 23 || min < 0 || min > 59)
        return -1;
    struct tm t = {.tm_year = year - 1900, .tm_mon = mon - 1, .tm_mday = day, .tm_hour = hour, .tm_min = min,
                   .tm_isdst = -1};
    time_t when = mktime(&t);
    if (when == (time_t)-1)
        return -1;
    char bb[512], at[32];
    snprintf(at, sizeof at, "@%lld", (long long)when);
    tm_fw_path(bb, sizeof bb, "/bin/busybox");
    char *argv[] = {bb, "date", "-s", at, NULL};
    if (tm_run(argv, NULL, 0, 10000) != 0) {
        LOGW("clock: setting the date failed");
        return -1;
    }
    LOGI("clock: set to %04d-%02d-%02d %02d:%02d (local)", year, mon, day, hour, min);
    return save_rtc();
}

int tm_clock_sync(int timeout_s)
{
    char bb[512];
    tm_fw_path(bb, sizeof bb, "/bin/busybox");
    char *argv[] = {bb, "ntpd", "-n", "-q", "-p", "a.st1.ntp.br", "-p", "pool.ntp.org", "-p", "time.google.com",
                    NULL};
    int rc = tm_run(argv, NULL, 0, timeout_s * 1000);
    if (rc != 0) {
        LOGW("clock: internet time failed (%d)", rc);
        return -1;
    }
    LOGI("clock: set from the internet");
    return save_rtc();
}
