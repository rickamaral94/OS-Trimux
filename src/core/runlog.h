/* What apps and port scripts print while they run. The output goes through a
 * pipe into a small ring buffer in memory (the card is mounted "sync", so
 * writing it there line by line would slow the program down); only the last
 * TM_TAIL_SIZE bytes are written to TriMuxData/logs/apps/<name>.log when the
 * program ends. The menu reads the result of the last run to tell the user
 * when something closed right after opening. */
#ifndef TRIMUX_RUNLOG_H
#define TRIMUX_RUNLOG_H

#include "paths.h"

#include <stddef.h>

#define TM_TAIL_SIZE (32 * 1024)

typedef struct {
    char buf[TM_TAIL_SIZE];
    size_t pos;   /* next write position */
    size_t total; /* bytes seen in all */
} TmTail;

void tm_tail_init(TmTail *t);
void tm_tail_add(TmTail *t, const char *data, size_t len);
/* Copies the kept bytes in order; returns their count. */
size_t tm_tail_get(const TmTail *t, char *out, size_t size);
/* Reads everything available on a non-blocking fd. Returns 1 at end of
 * file (all writers closed), 0 otherwise. */
int tm_tail_drain(TmTail *t, int fd);

/* Log file of one program: <logdir>/apps/<name>.log, the name reduced to
 * letters, digits, '-', '_' and '.'. */
int tm_runlog_path(const TmPaths *p, const char *name, char *out, size_t size);
/* Writes the header (program, exit code, time) and the kept output. */
int tm_runlog_save(const char *path, const char *label, int code, unsigned long secs, const TmTail *t);

typedef struct {
    char label[64];
    char log[TM_PATH_MAX]; /* relative to the card */
    int code;
    unsigned long secs;
} TmLastRun;

/* /tmp/trimux/lastrun.ini, written after each app or port script. */
int tm_lastrun_write(const TmPaths *p, const TmLastRun *r);
int tm_lastrun_read(const TmPaths *p, TmLastRun *r);
void tm_lastrun_clear(const TmPaths *p);
/* 1 when the run looks like a failure worth telling: ended with an error
 * within 30 s, or ended by itself within 3 s. */
int tm_lastrun_failed(const TmLastRun *r);

#endif
