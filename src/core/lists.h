/* Favorites and recently played: one relative path per line, saved atomically. */
#ifndef TRIMUX_LISTS_H
#define TRIMUX_LISTS_H

#include <stddef.h>

#define TM_LIST_MAX 500
#define TM_RECENT_MAX 30

typedef struct {
    char *items[TM_LIST_MAX];
    size_t count;
    size_t limit;
} TmList;

void tm_list_init(TmList *l, size_t limit);
void tm_list_free(TmList *l);
int tm_list_load(TmList *l, const char *path);
int tm_list_save(const TmList *l, const char *path);
long tm_list_index(const TmList *l, const char *item);
/* Moves/inserts item at the front (MRU). Drops the oldest beyond limit. */
int tm_list_push_front(TmList *l, const char *item);
int tm_list_remove(TmList *l, const char *item);
/* Returns 1 if added, 0 if removed, -1 on error. */
int tm_list_toggle(TmList *l, const char *item);

#endif
