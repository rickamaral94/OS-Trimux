/* App store: a short, hand-picked list of community apps (TriMux/share/
 * store.ini) that TriMux downloads on request. Nothing from these projects
 * ships inside the TriMux image; each one is fetched from its own release
 * page, pinned to a version and checked against a SHA-256 before anything
 * on the card changes. Only the folders named in the list are written or
 * removed, all on the card.
 *
 * store.ini, one section per app:
 *   name, desc (i18n key), version, url (https), sha256, size (bytes),
 *   license, source (project page), dest (folder on the card the zip is
 *   extracted into), installed (file relative to dest that marks it as
 *   installed), keep (top-level names kept from an older install, e.g. the
 *   user's settings), remove (paths relative to the card removed by
 *   "Remover", '|' separated), touch (a file created after installing,
 *   e.g. the "Portmaster" entry in Ports), system (0 = only uses the card,
 *   1 = also changes settings of the running system while open).
 * dest, remove and touch must stay under Apps/, Emus/ or Roms/PORTS/. */
#ifndef TRIMUX_STORE_H
#define TRIMUX_STORE_H

#include "paths.h"

#include <stddef.h>

#define TM_STORE_MAX 16

typedef struct {
    char id[32];
    char name[48];
    char desc_key[48];
    char version[32];
    char url[512];
    char sha256[65];
    long size;
    char license[96];
    char source[160];
    char dest[128];
    char installed[128];
    char keep[128];
    char remove[256];
    char touch[128]; /* file created (empty) after install if missing, relative to the card */
    int system;
} TmStoreItem;

typedef struct {
    char state[16]; /* downloading, installing, done, removed, error */
    char id[32];
    char error[32]; /* i18n key suffix: nowifi, space, network, checksum, package, write */
    int percent;
} TmStoreStatus;

/* Loads and validates the list (bad entries are skipped). Returns count. */
size_t tm_store_load(const char *ini_path, TmStoreItem *out, size_t max);
const TmStoreItem *tm_store_find(const TmStoreItem *items, size_t n, const char *id);
int tm_store_installed(const TmPaths *p, const TmStoreItem *it);
int tm_store_status_read(const TmPaths *p, TmStoreStatus *st);
int tm_store_running(const TmPaths *p);
/* Download, verify, extract, move into place (trimuxctl store install). */
int tm_store_install(const TmPaths *p, const TmStoreItem *it);
int tm_store_remove(const TmPaths *p, const TmStoreItem *it);

#endif
