/* TriMux menu application: shared state and screen interfaces. */
#ifndef TRIMUX_APP_H
#define TRIMUX_APP_H

#include "../core/catalog.h"
#include "../core/i18n.h"
#include "../core/ini.h"
#include "../core/leds.h"
#include "../core/library.h"
#include "../core/lists.h"
#include "../core/net.h"
#include "../core/paths.h"
#include "../core/popular.h"
#include "../core/power.h"
#include "../core/sysinfo.h"
#include "gfx.h"
#include "input.h"

#ifndef TRIMUX_VERSION
#define TRIMUX_VERSION "dev"
#endif

/* Exit codes understood by TriMux/scripts/supervisor.sh */
enum {
    EXIT_RESTART = 0,
    EXIT_LAUNCH = 10,
    EXIT_APP = 11,
    EXIT_STOCK = 20,
    EXIT_POWEROFF = 30,
    EXIT_REBOOT = 31,
    EXIT_CARD_GROW = 40,
};

typedef enum { SCR_HOME, SCR_GAMES, SCR_MENU, SCR_WIZARD, SCR_KEYBOARD, SCR_CTRLTEST, SCR_INFO } ScreenType;

/* special "systems" for the game list */
enum { VIEW_ALL = -1, VIEW_FAVORITES = -2, VIEW_RECENT = -3 };

#define MENU_MAX_ITEMS 64

typedef struct {
    char label[96];
    char value[64];
    char desc[1024];
    int enabled;
    int id;       /* action id (menus.c) */
    long arg;
    char sarg[256];
    uint32_t badge_color;
    char badge[16];
} MenuItem;

typedef struct {
    int page;
    long ctx;
    char sctx[256];
    char title[96];
    MenuItem items[MENU_MAX_ITEMS];
    int n, sel, top;
} Menu;

#define MENU_STACK 8

typedef struct {
    int active;
    int id;
    long arg;
    char sarg[256];
    char title[96];
    char text[512];
    int sel; /* 0 = yes/ok, 1 = no */
    int info_only;
} Dialog;

typedef struct {
    TmPaths paths;
    TmIni settings, overrides;
    int settings_dirty, overrides_dirty;
    TmCatalog cat;
    TmLibrary lib;
    TmList fav, recent;
    TmPopular pop;      /* popularity ranks per platform */
    TmIni plays;        /* play statistics (TriMuxData/state/plays.ini) */
    int *rank;          /* popularity rank per library game (0 = none) */
    size_t nrank;
    int ranks_valid;
    TmPowerCaps power;
    TmLeds leds;
    TmSysInfo si;
    uint64_t si_time;

    ScreenType screen;
    int running, exit_code, dirty;

    /* home */
    int home_sel, home_top;
    /* game list */
    int view_system;
    int *view;
    size_t nview, view_cap;
    int games_sel, games_top;
    char query[64];
    int fav_only;
    /* menus */
    Menu menus[MENU_STACK];
    int nmenus;
    ScreenType menu_return;
    /* wizard */
    int wiz_step, wiz_sel;
    /* on-screen keyboard */
    int kb_row, kb_col;
    char kb_buf[128];
    ScreenType kb_return;
    int kb_purpose; /* KB_* */
    int kb_layer;   /* text mode: 0 lower, 1 upper, 2 symbols */
    char kb_title[96];
    char kb_ctx[64];
    /* network */
    TmWifiAp aps[TM_WIFI_MAX];
    size_t naps;
    uint64_t scan_at;       /* scan requested at (0 = none pending) */
    uint64_t connect_until; /* waiting for a connection until */
    char connect_ssid[TM_SSID_MAX + 1];
    TmWifiStatus wst;
    uint64_t wst_time;
    /* controller test */
    uint64_t ctrl_start_held;
    /* dialogs / toast */
    Dialog dlg;
    char toast[160];
    uint64_t toast_until;
    uint64_t last_input;
    int idle_warned;
} App;

extern App A;
const char *tr(const char *key);

/* app.c */
void app_toast(const char *msg);
void app_dialog(int id, const char *title, const char *text, long arg, const char *sarg, int info_only);
void app_header(const char *title);
/* Smoothly moving value for the selection highlight of list `slot`
 * (0 home, 1 games, 2 menus); row = row height (long jumps snap). */
int app_anim(int slot, int target, int row);
/* Side panel with a soft shadow; band = platform colour washed in at the top (0: none). */
/* Cover image path of a game (Imgs/..., or a stock port's own icon); -1 if covers are off. */
int app_game_cover(const TmGame *g, char *out, size_t size);
void app_panel(int x, int y, int w, int h, uint32_t band);
/* Centred message for an empty list (star: the favourites icon above it). */
void app_empty(const char *msg, int star);
void app_footer(const char *hints);
void app_mark_settings(void);
void app_save_all(void);
void app_refresh_sysinfo(int force);
void app_rescan(void);
void app_key_action(TmButton b);
void app_switch_tick(void);
void app_cycle_profile(int dir);
void app_exit(int code);
const TmEmulator *app_resolve_emu(const TmGame *g, int *is_override);
void app_launch(long game_index);
void app_apply_language(void);
int app_bios_status(const TmSystem *sys, char *missing, size_t size);
/* Selection highlight of a list row (animated); draw it before the rows. */
void app_list_highlight(int x, int y, int w, int h);
void app_draw_list_row(int x, int y, int w, int h, int selected, const char *label, const char *value,
                       int enabled, uint32_t badge_color, const char *badge, int star);

/* home.c */
void home_build(void);
void home_draw(void);
void home_input(TmButton b);
size_t home_count(void);
/* colour of the selected platform, for the background (0: none) */
uint32_t home_ambient(void);

/* games.c */
void games_open(int system);
void games_rebuild(void);
enum { SORT_NAME = 0, SORT_POPULAR, SORT_PLAYED };
int games_sort_mode(void);
/* popularity rank of a library game (0 = not in the list) */
int games_rank(long gi);
void games_draw(void);
uint32_t games_ambient(void);
/* 1 when the game list is shown as a cover grid */
int games_grid(void);
void games_input(TmButton b);
long games_selected_index(void);

/* menus.c */
void menu_open(int page, long ctx, const char *sctx);
void menu_rebuild(void);
void menu_draw(void);
void menu_input(TmButton b);
void menu_close(void);
void menu_dialog_result(int id, long arg, const char *sarg, int yes);
void info_draw(void);
void info_input(TmButton b);
void ctrltest_draw(void);
void ctrltest_input(TmButton b);
void ctrltest_tick(void);

/* wizard.c */
void wizard_open(void);
void wizard_draw(void);
void wizard_input(TmButton b);

/* keyboard.c */
enum { KB_SEARCH = 0, KB_WIFI_PSK, KB_CHEEVOS_USER, KB_CHEEVOS_PASS };
void keyboard_open(const char *initial, ScreenType ret);
/* Free text entry (all printable ASCII); the result goes to
 * menu_keyboard_done(). ctx is passed back unchanged. */
void keyboard_open_text(int purpose, const char *title, const char *initial, const char *ctx);
void menu_keyboard_done(int purpose, const char *ctx, const char *text, int cancelled);
/* menus.c: Wi-Fi scan/connection polling and the FTP server lifetime */
void net_tick(void);
void net_ftp_stop(void);
/* Starts the cover downloader in the background (auto: only if enabled). */
void covers_start(int autorun, int retry);
int update_blocks_launch(void);
void app_leds_set_all(int off);
size_t apps_count(void);
void keyboard_draw(void);
void keyboard_input(TmButton b);

/* menu page ids */
enum {
    PAGE_SETTINGS = 1, PAGE_LANGUAGE, PAGE_APPEARANCE, PAGE_CONTROLS, PAGE_POWER, PAGE_LEDS, PAGE_LED_ZONE,
    PAGE_LIBRARY, PAGE_FOLDERS, PAGE_BIOS, PAGE_EMULATORS, PAGE_EMU_PLATFORM, PAGE_STORAGE, PAGE_SYSTEM,
    PAGE_QUICK, PAGE_GAME_OPTIONS, PAGE_EMU_CHOOSE, PAGE_HOTKEYS, PAGE_DISPLAY, PAGE_ABOUT, PAGE_LOG,
    PAGE_ADD_GAMES, PAGE_BUTTONS, PAGE_NETWORK, PAGE_WIFI_SCAN, PAGE_WIFI_SAVED, PAGE_CHEEVOS,
    PAGE_DIAG, PAGE_PERF, PAGE_COVERS, PAGE_UPDATE, PAGE_DATETIME, PAGE_APPS, PAGE_STATS, PAGE_CLEAN,
    PAGE_FILES,
};

/* dialog ids */
enum {
    DLG_NONE = 0, DLG_STOCK, DLG_POWEROFF, DLG_REBOOT, DLG_MKDIRS, DLG_RESTORE_EMU, DLG_GROW, DLG_EXPERIMENTAL,
    DLG_WIZ_SKIP, DLG_POWER_DEFAULT, DLG_INFO, DLG_IDLE, DLG_BOOST,
    DLG_FTP, DLG_SSH, DLG_WIFI_FORGET, DLG_CLEAR_LOGS, DLG_UPDATE_INSTALL, DLG_UPDATE_ROLLBACK, DLG_UPDATE_READY,
    DLG_STORE_INSTALL, DLG_STORE_REMOVE, DLG_WEB, DLG_CLEAN, DLG_FILE_DELETE,
};

/* menu actions of tools.c (menus.c hands every id from ACT_T_FIRST on to it) */
enum {
    ACT_T_FIRST = 500, ACT_T_PAGE = ACT_T_FIRST, ACT_T_RANDOM, ACT_T_GAME, ACT_T_WEB, ACT_T_CLEAN_TOGGLE,
    ACT_T_CLEAN_RUN, ACT_T_CLEAN_RESCAN, ACT_T_FILE_DIR, ACT_T_FILE, ACT_T_FILE_PAGE,
};

/* menus.c: rows for pages built elsewhere */
MenuItem *menu_add(Menu *m, int id, const char *label, const char *value, const char *desc);
void menu_header(Menu *m, const char *label);

/* tools.c */
void tools_items(Menu *m);
int tools_page(Menu *m);
void tools_activate(Menu *m, MenuItem *it, TmButton b);
int tools_dialog_result(int id, long arg, const char *sarg, int yes);
/* B in the file manager goes up a folder (1) before leaving the page (0) */
int tools_back(Menu *m);
void tools_web_stop(void);

#endif
