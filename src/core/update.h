/* Online updater: TriMux updates itself from the project's GitHub releases.
 *
 * Only the replaceable parts of the card change (TriMux/, trimui/,
 * LEIA-ME.txt); games, BIOS, saves, states, covers and TriMuxData are never
 * touched. Steps: ask the GitHub API for releases, pick the newest version
 * above the installed one, download TriMux-<v>-update.tar.gz and
 * TriMux-<v>-brickpro.sha256 over verified HTTPS, check the SHA-256, extract
 * next to the current version, check the tree, then swap folders and keep the
 * previous version as TriMux.old / trimui.old. trimui/app/MainUI restores the
 * previous version by itself if the new one fails to reach the menu. */
#ifndef TRIMUX_UPDATE_H
#define TRIMUX_UPDATE_H

#include "paths.h"

#include <stddef.h>

typedef struct {
    char tag[32];      /* e.g. v0.4.0 */
    char version[32];  /* e.g. 0.4.0 */
    int prerelease;
    char notes[1536];  /* release notes, first part */
    char pkg_name[96]; /* TriMux-0.4.0-update.tar.gz */
    char pkg_url[512];
    long pkg_size;
    char sha_url[512];
} TmRelease;

/* ---- pure helpers (unit tested) ---- */

/* Compares dotted versions ("v0.10.0" > "0.9.3"); a leading 'v' is ignored. */
int tm_version_cmp(const char *a, const char *b);
/* From the GitHub "list releases" JSON, picks the newest release newer than
 * current (drafts skipped; prereleases only if allowed) that has both the
 * update package and the checksum file. Returns 1 found, 0 none, -1 bad JSON. */
int tm_update_pick(const char *json, size_t len, const char *current, int allow_prerelease, TmRelease *out);
/* Finds "<hash>  <name>" in a sha256sum file. Returns 0 and the lower-case hash. */
int tm_update_sha_lookup(const char *text, const char *name, char hex[65]);

/* ---- status shared with the menu (/tmp/trimux/update.status) ---- */

typedef struct {
    char state[16]; /* checking, uptodate, available, downloading, installing, ready, error */
    char version[32];
    char error[48]; /* i18n key suffix when state = error */
    int percent;
} TmUpdateStatus;

int tm_update_status_read(const TmPaths *p, TmUpdateStatus *st);
/* Release found by the last check (/tmp/trimux/update.ini). */
int tm_update_info_read(const TmPaths *p, TmRelease *r);
int tm_update_running(const TmPaths *p);
/* Installed version (TriMux/VERSION). */
int tm_update_current(const TmPaths *p, char *out, size_t size);
/* 1 if TriMux.old exists (a previous version to go back to). */
int tm_update_has_backup(const TmPaths *p);

/* ---- actions (trimuxctl update ...) ---- */

int tm_update_check(const TmPaths *p, int allow_prerelease, int wait_wifi_s, TmRelease *out);
int tm_update_install(const TmPaths *p, const TmRelease *r);
int tm_update_rollback(const TmPaths *p);
/* Called once the menu is on screen: the update worked (only counts after
 * trimui/app/MainUI started the new version, see update.c). */
void tm_update_confirm(const TmPaths *p);

#endif
