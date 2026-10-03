/* Applications in the TrimUI format (a folder with config.json and the
 * launcher script it names), from the same places the stock menu reads:
 * <card>/Apps, /mnt/UDISK/Apps (apps installed on the internal memory) and
 * the firmware's own /usr/trimui/apps. TriMux only reads these folders; the
 * firmware apps that format the card, expose it over USB or rewrite the FN
 * key setup are not listed. */
#ifndef TRIMUX_APPS_H
#define TRIMUX_APPS_H

#include "paths.h"

#include <stddef.h>

#define TM_APPS_MAX 64

typedef struct {
    char dir[TM_PATH_MAX];   /* absolute folder */
    char label[64];
    char desc[192];
    char launch[64];         /* script name inside dir */
    char icon[TM_PATH_MAX];  /* absolute PNG, may be empty */
    int builtin;             /* firmware app */
} TmApp;

/* Reads one app folder; 0 when it is a valid app. lang ("pt_BR") picks
 * "label.<lang>.lang" style translations when present. */
int tm_app_load(const char *dir, const char *lang, TmApp *out);
/* Lists the apps, sorted by label (card apps first among equals). */
size_t tm_apps_scan(const TmPaths *p, const char *lang, TmApp *out, size_t max);
/* 1 if dir is an app folder TriMux may start (inside one of the app roots,
 * not a blocked firmware app, valid config). Fills out. */
int tm_app_allowed(const TmPaths *p, const char *dir, TmApp *out);

/* Hand-off between the menu and the supervisor (/tmp/trimux/app.ini). */
int tm_app_request_write(const TmPaths *p, const char *dir);
int tm_app_request_read(const TmPaths *p, char *dir, size_t size);

#endif
