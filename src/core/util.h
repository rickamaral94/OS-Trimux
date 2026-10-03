/* Small, allocation-light helpers shared by every TriMux component. */
#ifndef TRIMUX_UTIL_H
#define TRIMUX_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define TM_PATH_MAX 1024
#define TM_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* Bounded copy that always NUL-terminates. Returns 0, or -1 if truncated. */
int tm_strlcpy(char *dst, const char *src, size_t size);
/* Bounded formatted print; returns -1 on truncation or error. */
int tm_snprintf(char *dst, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

char *tm_trim(char *s);
int tm_strcasecmp_ascii(const char *a, const char *b);
int tm_ends_with_ci(const char *s, const char *suffix);
int tm_starts_with(const char *s, const char *prefix);

/* Join a and b with exactly one '/'. Returns -1 on truncation. */
int tm_path_join(char *out, size_t size, const char *a, const char *b);
/* Rejects empty paths, "..", control characters and anything outside root. */
int tm_path_is_safe_under(const char *root, const char *path);
/* Rejects names that are unsafe as a single path component. */
int tm_name_is_safe(const char *name);

int tm_file_exists(const char *path);
int tm_dir_exists(const char *path);
int tm_mkdir_p(const char *path);

/* Reads a whole (small) file. Caller frees. max_size guards runaway reads. */
char *tm_read_file(const char *path, size_t max_size, size_t *out_len);
/* Reads the first line of a sysfs-like file, trimmed. Returns -1 on error. */
int tm_read_line(const char *path, char *buf, size_t size);
int tm_read_long(const char *path, long *out);
/* Writes a short value to a sysfs-like file (no truncation semantics). */
int tm_write_str(const char *path, const char *value);

/* Crash-safe replace: write tmp in the same dir, fsync, rename, fsync dir. */
int tm_atomic_write(const char *path, const void *data, size_t len);
/* Forces a file's data, or the directory entry of a file, onto the card. */
int tm_fsync_path(const char *path);
void tm_fsync_parent(const char *path);
/* Copies src to dst atomically. */
int tm_copy_file(const char *src, const char *dst);

/* Folds UTF-8 Latin-1 accents to ASCII and lowercases, for search/sort keys. */
void tm_fold_key(const char *in, char *out, size_t size);

/* Monotonic milliseconds. */
uint64_t tm_now_ms(void);

#endif
