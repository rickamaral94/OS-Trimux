#define _GNU_SOURCE
#include "lists.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

void tm_list_init(TmList *l, size_t limit)
{
    memset(l, 0, sizeof *l);
    l->limit = (limit == 0 || limit > TM_LIST_MAX) ? TM_LIST_MAX : limit;
}

void tm_list_free(TmList *l)
{
    for (size_t i = 0; i < l->count; i++)
        free(l->items[i]);
    size_t limit = l->limit;
    tm_list_init(l, limit);
}

static int item_ok(const char *s)
{
    return s && *s && !strchr(s, '\n') && !strchr(s, '\r') && strlen(s) < TM_PATH_MAX;
}

int tm_list_load(TmList *l, const char *path)
{
    tm_list_free(l);
    char *data = tm_read_file(path, 1u << 20, NULL);
    if (!data)
        return 0; /* missing file = empty list */
    char *save = NULL;
    for (char *line = strtok_r(data, "\r\n", &save); line && l->count < l->limit;
         line = strtok_r(NULL, "\r\n", &save)) {
        char *t = tm_trim(line);
        if (item_ok(t) && tm_list_index(l, t) < 0) {
            char *dup = strdup(t);
            if (dup)
                l->items[l->count++] = dup;
        }
    }
    free(data);
    return 0;
}

int tm_list_save(const TmList *l, const char *path)
{
    size_t len = 0;
    for (size_t i = 0; i < l->count; i++)
        len += strlen(l->items[i]) + 1;
    char *buf = malloc(len + 1);
    if (!buf)
        return -1;
    size_t o = 0;
    for (size_t i = 0; i < l->count; i++) {
        size_t n = strlen(l->items[i]);
        memcpy(buf + o, l->items[i], n);
        o += n;
        buf[o++] = '\n';
    }
    int rc = tm_atomic_write(path, buf, o);
    free(buf);
    return rc;
}

long tm_list_index(const TmList *l, const char *item)
{
    for (size_t i = 0; item && i < l->count; i++)
        if (strcmp(l->items[i], item) == 0)
            return (long)i;
    return -1;
}

int tm_list_remove(TmList *l, const char *item)
{
    long i = tm_list_index(l, item);
    if (i < 0)
        return -1;
    free(l->items[i]);
    memmove(&l->items[i], &l->items[i + 1], (l->count - (size_t)i - 1) * sizeof(char *));
    l->count--;
    return 0;
}

int tm_list_push_front(TmList *l, const char *item)
{
    if (!item_ok(item))
        return -1;
    char *dup = strdup(item);
    if (!dup)
        return -1;
    tm_list_remove(l, item);
    if (l->count >= l->limit) {
        free(l->items[l->count - 1]);
        l->count--;
    }
    memmove(&l->items[1], &l->items[0], l->count * sizeof(char *));
    l->items[0] = dup;
    l->count++;
    return 0;
}

int tm_list_toggle(TmList *l, const char *item)
{
    if (tm_list_index(l, item) >= 0)
        return tm_list_remove(l, item) == 0 ? 0 : -1;
    if (!item_ok(item) || l->count >= l->limit)
        return -1;
    char *dup = strdup(item);
    if (!dup)
        return -1;
    l->items[l->count++] = dup;
    return 1;
}
