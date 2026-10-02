#define _GNU_SOURCE
#include "log.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static char g_path[TM_PATH_MAX];
static char g_tag[32] = "trimux";
static size_t g_max = 256 * 1024;
static int g_min_level = TM_LOG_INFO;

void tm_log_init(const char *path, size_t max_bytes, const char *tag)
{
    g_path[0] = '\0';
    if (path)
        tm_strlcpy(g_path, path, sizeof g_path);
    if (max_bytes)
        g_max = max_bytes;
    if (tag)
        tm_strlcpy(g_tag, tag, sizeof g_tag);
    const char *dbg = getenv("TRIMUX_DEBUG");
    g_min_level = (dbg && *dbg == '1') ? TM_LOG_DEBUG : TM_LOG_INFO;
}

static void rotate_if_needed(void)
{
    struct stat st;
    if (stat(g_path, &st) == 0 && (size_t)st.st_size >= g_max) {
        char old[TM_PATH_MAX];
        if (tm_snprintf(old, sizeof old, "%s.1", g_path) == 0)
            rename(g_path, old); /* replaces the previous .1 */
    }
}

void tm_log(int level, const char *fmt, ...)
{
    static const char *names[] = {"D", "I", "W", "E"};
    if (level < g_min_level)
        return;
    char msg[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    time_t now = time(NULL);
    struct tm tmv;
    char ts[32] = "";
    if (localtime_r(&now, &tmv))
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
    const char *lv = names[level < 0 ? 0 : level > 3 ? 3 : level];

    if (!g_path[0] || getenv("TRIMUX_LOG_STDERR"))
        fprintf(stderr, "%s %s[%s] %s\n", ts, g_tag, lv, msg);
    if (!g_path[0])
        return;
    rotate_if_needed();
    FILE *f = fopen(g_path, "a");
    if (!f)
        return;
    fprintf(f, "%s %s[%s] %s\n", ts, g_tag, lv, msg);
    fclose(f);
}

void tm_log_close(void) {}
