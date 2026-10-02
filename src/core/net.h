/* Wi-Fi, Bluetooth, SSH and FTP, driven through the firmware's own tools.
 *
 * Every command mirrors what the stock firmware already runs (runtrimui.sh,
 * /etc/init.d/wpa_supplicant and the stock MainUI's wpa_cli calls): the same
 * binaries, the same control socket (/etc/wifi/sockets) and the same network
 * list (/etc/wifi/wpa_supplicant.conf), so networks saved in either system work
 * in both. Commands are executed with execv (no shell). Binary paths are
 * prefixed with TRIMUX_SYSFS_ROOT so tests can supply fake tools. */
#ifndef TRIMUX_NET_H
#define TRIMUX_NET_H

#include "ini.h"

#include <stddef.h>
#include <sys/types.h>

#define TM_WIFI_MAX 32
#define TM_SSID_MAX 32 /* bytes, IEEE 802.11 */

typedef enum { TM_WIFI_OPEN = 0, TM_WIFI_PSK, TM_WIFI_UNSUPPORTED } TmWifiSecurity;

typedef struct {
    char ssid[TM_SSID_MAX + 1];
    int signal; /* dBm */
    TmWifiSecurity security;
    int saved_id; /* network id in wpa_supplicant, or -1 */
    int current;
} TmWifiAp;

typedef struct {
    int id;
    char ssid[TM_SSID_MAX + 1];
    int current;
} TmWifiNet;

typedef struct {
    char state[32]; /* wpa_state, e.g. COMPLETED */
    char ssid[TM_SSID_MAX + 1];
    char ip[48];
} TmWifiStatus;

/* ---- pure helpers (unit tested) ---- */

/* Decodes wpa_supplicant's printf-style escapes (\xNN, \\, \", \e, \n...). */
void tm_wpa_unescape(const char *in, char *out, size_t size);
/* Parses `wpa_cli scan_result`. Hidden networks are skipped, duplicates keep
 * the strongest signal, result sorted by signal (strongest first). */
size_t tm_wifi_parse_scan(const char *text, TmWifiAp *out, size_t max);
/* Parses `wpa_cli list_network`. */
size_t tm_wifi_parse_networks(const char *text, TmWifiNet *out, size_t max);
void tm_wifi_parse_status(const char *text, TmWifiStatus *st);
/* Marks scan entries that are saved / current. */
void tm_wifi_mark_saved(TmWifiAp *aps, size_t n, const TmWifiNet *nets, size_t nn);
/* SSID as hex (accepted by wpa_supplicant without quoting rules). */
int tm_wifi_ssid_hex(const char *ssid, char *out, size_t size);
/* WPA passphrase: 8..63 printable ASCII characters. */
int tm_wifi_psk_valid(const char *psk);
/* 0..3 bars from dBm. */
int tm_wifi_bars(int dbm);
/* RetroAchievements lines for the RetroArch append config, from settings
 * [cheevos]. Empty when disabled or incomplete. Returns -1 if a value cannot
 * be written safely (quote or newline). */
int tm_cheevos_cfg(const TmIni *settings, char *out, size_t size);

/* ---- process helpers ---- */

/* Runs argv[0] (absolute path) with argv, stdout captured into out (may be
 * NULL), stderr discarded. Returns the exit status, or -1 on error/timeout. */
int tm_run(char *const argv[], char *out, size_t outsz, int timeout_ms);
/* Starts a detached process (own session, stdio on /dev/null, reparented to
 * init). Returns its pid or -1. cwd and ld_path may be NULL. */
pid_t tm_spawn(char *const argv[], const char *cwd, const char *ld_path);
/* Firmware path with the test root prefix applied. */
int tm_fw_path(char *out, size_t size, const char *abs);
int tm_proc_running(const char *name);

/* HTTPS download with the firmware's curl, TLS verified against ca_file.
 * accept may be NULL. Returns curl's exit code (0 ok, 22 HTTP error such as
 * 404, others network/TLS), or -1. */
int tm_https_get(const char *url, const char *dst, const char *ca_file, long max_bytes, int timeout_s,
                 const char *accept);

/* ---- Wi-Fi ---- */

int tm_wifi_available(void); /* wpa_cli present and wlan0 exists */
int tm_wifi_running(void);   /* wpa_supplicant running */
int tm_wifi_set(int on);
int tm_wifi_status(TmWifiStatus *st);
int tm_wifi_scan_start(void);
size_t tm_wifi_scan_results(TmWifiAp *out, size_t max);
size_t tm_wifi_networks(TmWifiNet *out, size_t max);
/* Connects to a saved network (by SSID) or saves a new one. psk is ignored
 * for saved and open networks. Returns 0 when the request was accepted. */
int tm_wifi_connect(const char *ssid, const char *psk, int open);
int tm_wifi_forget(int id);

/* ---- Bluetooth (TrimUI's own manager; pairing stays in the stock UI) ---- */

int tm_bt_available(void);
int tm_bt_running(void);
int tm_bt_set(int on);

/* ---- firmware SSH/SFTP server (OpenSSH, /etc/init.d/sshd) ---- */

int tm_ssh_available(void);
int tm_ssh_running(void);
int tm_ssh_set(int on);

/* ---- FTP (busybox tcpsvd + ftpd, chrooted to dir) ---- */

int tm_ftp_available(void);
/* Listens on ip:port only. Writes the pid to pidfile. */
int tm_ftp_start(const char *ip, int port, const char *dir, const char *pidfile);
void tm_ftp_stop(const char *pidfile);
int tm_ftp_running(const char *pidfile);

/* Applies the [network] settings at boot. Values: wifi = on|off|(empty: leave
 * as the firmware set it), bluetooth = 0|1, ssh = 0|1 (default 0). */
void tm_net_apply(const TmIni *settings);

#endif
