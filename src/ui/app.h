/* TriMux menu application: shared state and screen interfaces. */
#ifndef TRIMUX_APP_H
#define TRIMUX_APP_H

#include "../core/catalog.h"
#include "../core/i18n.h"
#include "../core/ini.h"
#include "../core/leds.h"
#include "../core/library.h"
#include "../core/lists.h"
#include "../core/paths.h"
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
    char desc[320];
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
    char kb_buf[64];
    ScreenType kb_return;
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
void app_draw_list_row(int x, int y, int w, int h, int selected, const char *label, const char *value,
                       int enabled, uint32_t badge_color, const char *badge, int star);

/* home.c */
void home_build(void);
void home_draw(void);
void home_input(TmButton b);
size_t home_count(void);

/* games.c */
void games_open(int system);
void games_rebuild(void);
void games_draw(void);
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
void keyboard_open(const char *initial, ScreenType ret);
void keyboard_draw(void);
void keyboard_input(TmButton b);

/* menu page ids */
enum {
    PAGE_SETTINGS = 1, PAGE_LANGUAGE, PAGE_APPEARANCE, PAGE_CONTROLS, PAGE_POWER, PAGE_LEDS, PAGE_LED_ZONE,
    PAGE_LIBRARY, PAGE_FOLDERS, PAGE_BIOS, PAGE_EMULATORS, PAGE_EMU_PLATFORM, PAGE_STORAGE, PAGE_SYSTEM,
    PAGE_QUICK, PAGE_GAME_OPTIONS, PAGE_EMU_CHOOSE, PAGE_HOTKEYS, PAGE_DISPLAY, PAGE_ABOUT, PAGE_LOG,
    PAGE_ADD_GAMES, PAGE_BUTTONS,
};

/* dialog ids */
enum {
    DLG_NONE = 0, DLG_STOCK, DLG_POWEROFF, DLG_REBOOT, DLG_MKDIRS, DLG_RESTORE_EMU, DLG_GROW, DLG_EXPERIMENTAL,
    DLG_WIZ_SKIP, DLG_POWER_DEFAULT, DLG_INFO, DLG_IDLE, DLG_BOOST,
};

#endif
