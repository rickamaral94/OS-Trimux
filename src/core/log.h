/* Size-capped log with a single rotation (file -> file.1). Writes are
 * line-buffered and rare; nothing is logged in hot paths. */
#ifndef TRIMUX_LOG_H
#define TRIMUX_LOG_H

#include <stddef.h>

enum { TM_LOG_DEBUG, TM_LOG_INFO, TM_LOG_WARN, TM_LOG_ERROR };

/* path may be NULL (log to stderr only). max_bytes default: 256 KiB. */
void tm_log_init(const char *path, size_t max_bytes, const char *tag);
void tm_log(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void tm_log_close(void);
/* TM_LOG_DEBUG enables detailed logging (setting [diag] verbose). */
void tm_log_set_level(int min_level);

#define LOGI(...) tm_log(TM_LOG_INFO, __VA_ARGS__)
#define LOGW(...) tm_log(TM_LOG_WARN, __VA_ARGS__)
#define LOGE(...) tm_log(TM_LOG_ERROR, __VA_ARGS__)
#define LOGD(...) tm_log(TM_LOG_DEBUG, __VA_ARGS__)

#endif
