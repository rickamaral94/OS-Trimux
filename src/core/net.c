/* Wi-Fi, Bluetooth, SSH and FTP through the firmware's own tools. */
#define _GNU_SOURCE
#include "net.h"
#include "log.h"
#include "power.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define WPA_CLI "/usr/sbin/wpa_cli"
#define WPA_SUPPLICANT "/usr/sbin/wpa_supplicant"
#define BUSYBOX "/bin/busybox"
#define WPA_SOCKETS "/etc/wifi/sockets"
#define BTMANAGER_DIR "/usr/trimui/bin"
#define BTMANAGER "/usr/trimui/bin/trimui_btmanager"
#define SSHD_INIT "/etc/init.d/sshd"
#define SSHD "/usr/sbin/sshd"

/* ------------------------------------------------------------ pure helpers */

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

void tm_wpa_unescape(const char *in, char *out, size_t size)
{
    size_t o = 0;
    if (!size)
        return;
    for (const char *p = in; *p && o + 1 < size; p++) {
        if (*p != '\\' || !p[1]) {
            out[o++] = *p;
            continue;
        }
        p++;
        switch (*p) {
        case 'n': out[o++] = '\n'; break;
        case 'r': out[o++] = '\r'; break;
        case 't': out[o++] = '\t'; break;
        case 'e': out[o++] = '\033'; break;
        case 'x':
            if (hexval(p[1]) >= 0 && hexval(p[2]) >= 0) {
                out[o++] = (char)(hexval(p[1]) * 16 + hexval(p[2]));
                p += 2;
            } else {
                out[o++] = 'x';
            }
            break;
        default: out[o++] = *p; break; /* \\ \" and anything else */
        }
    }
    out[o] = '\0';
}

/* Splits one line on tabs in place; returns the number of fields. */
static int split_tabs(char *line, char **f, int max)
{
    int n = 0;
    char *p = line;
    while (n < max) {
        f[n++] = p;
        char *t = strchr(p, '\t');
        if (!t)
            break;
        *t = '\0';
        p = t + 1;
    }
    return n;
}

static TmWifiSecurity security_of(const char *flags)
{
    if (strstr(flags, "EAP") || strstr(flags, "WEP"))
        return TM_WIFI_UNSUPPORTED;
    if (strstr(flags, "PSK"))
        return TM_WIFI_PSK;
    if (strstr(flags, "WPA") || strstr(flags, "RSN") || strstr(flags, "SAE"))
        return TM_WIFI_UNSUPPORTED; /* e.g. WPA3-only (SAE) */
    return TM_WIFI_OPEN;
}

static int cmp_signal(const void *a, const void *b)
{
    const TmWifiAp *x = a, *y = b;
    if (x->signal != y->signal)
        return y->signal - x->signal;
    return strcmp(x->ssid, y->ssid);
}

size_t tm_wifi_parse_scan(const char *text, TmWifiAp *out, size_t max)
{
    size_t n = 0;
    char *copy = strdup(text ? text : "");
    if (!copy)
        return 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *f[5];
        size_t ll = strlen(line);
        if (ll && line[ll - 1] == '\r')
            line[ll - 1] = '\0';
        if (split_tabs(line, f, 5) < 5 || strchr(f[0], ':') == NULL)
            continue; /* header line or garbage */
        char ssid[TM_SSID_MAX * 4 + 1];
        tm_wpa_unescape(f[4], ssid, sizeof ssid);
        if (!ssid[0] || strlen(ssid) > TM_SSID_MAX)
            continue; /* hidden */
        int sig = atoi(f[2]);
        size_t i;
        for (i = 0; i < n; i++)
            if (strcmp(out[i].ssid, ssid) == 0)
                break;
        if (i < n) {
            if (sig > out[i].signal)
                out[i].signal = sig;
            continue;
        }
        if (n >= max)
            continue;
        memset(&out[n], 0, sizeof out[n]);
        tm_strlcpy(out[n].ssid, ssid, sizeof out[n].ssid);
        out[n].signal = sig;
        out[n].security = security_of(f[3]);
        out[n].saved_id = -1;
        n++;
    }
    free(copy);
    qsort(out, n, sizeof *out, cmp_signal);
    return n;
}

size_t tm_wifi_parse_networks(const char *text, TmWifiNet *out, size_t max)
{
    size_t n = 0;
    char *copy = strdup(text ? text : "");
    if (!copy)
        return 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line && n < max; line = strtok_r(NULL, "\n", &save)) {
        char *f[4];
        int nf = split_tabs(line, f, 4);
        if (nf < 2 || f[0][0] < '0' || f[0][0] > '9')
            continue;
        char ssid[TM_SSID_MAX * 4 + 1];
        tm_wpa_unescape(f[1], ssid, sizeof ssid);
        if (strlen(ssid) > TM_SSID_MAX)
            continue;
        out[n].id = atoi(f[0]);
        tm_strlcpy(out[n].ssid, ssid, sizeof out[n].ssid);
        out[n].current = nf >= 4 && strstr(f[3], "[CURRENT]") != NULL;
        n++;
    }
    free(copy);
    return n;
}

void tm_wifi_parse_status(const char *text, TmWifiStatus *st)
{
    memset(st, 0, sizeof *st);
    char *copy = strdup(text ? text : "");
    if (!copy)
        return;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *v = tm_trim(eq + 1);
        if (strcmp(line, "wpa_state") == 0)
            tm_strlcpy(st->state, v, sizeof st->state);
        else if (strcmp(line, "ssid") == 0) {
            char ssid[TM_SSID_MAX * 4 + 1];
            tm_wpa_unescape(v, ssid, sizeof ssid);
            tm_strlcpy(st->ssid, ssid, sizeof st->ssid);
        } else if (strcmp(line, "ip_address") == 0)
            tm_strlcpy(st->ip, v, sizeof st->ip);
    }
    free(copy);
}

void tm_wifi_mark_saved(TmWifiAp *aps, size_t n, const TmWifiNet *nets, size_t nn)
{
    for (size_t i = 0; i < n; i++) {
        aps[i].saved_id = -1;
        aps[i].current = 0;
        for (size_t k = 0; k < nn; k++)
            if (strcmp(aps[i].ssid, nets[k].ssid) == 0) {
                aps[i].saved_id = nets[k].id;
                aps[i].current = nets[k].current;
            }
    }
}

int tm_wifi_ssid_hex(const char *ssid, char *out, size_t size)
{
    size_t n = ssid ? strlen(ssid) : 0;
    if (n == 0 || n > TM_SSID_MAX || size < n * 2 + 1)
        return -1;
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = hx[(unsigned char)ssid[i] >> 4];
        out[i * 2 + 1] = hx[(unsigned char)ssid[i] & 15];
    }
    out[n * 2] = '\0';
    return 0;
}

int tm_wifi_psk_valid(const char *psk)
{
    size_t n = psk ? strlen(psk) : 0;
    if (n < 8 || n > 63)
        return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)psk[i] < 32 || (unsigned char)psk[i] > 126)
            return 0;
    return 1;
}

int tm_wifi_bars(int dbm)
{
    if (dbm >= -60)
        return 3;
    if (dbm >= -70)
        return 2;
    if (dbm >= -80)
        return 1;
    return 0;
}

static int safe_cfg_value(const char *v)
{
    for (; *v; v++)
        if (*v == '"' || *v == '\n' || *v == '\r')
            return 0;
    return 1;
}

int tm_cheevos_cfg(const TmIni *settings, char *out, size_t size)
{
    if (!size)
        return -1;
    out[0] = '\0';
    const char *user = tm_ini_get(settings, "cheevos", "user", "");
    const char *pass = tm_ini_get(settings, "cheevos", "password", "");
    if (!tm_ini_get_long(settings, "cheevos", "enable", 0) || !user[0] || !pass[0])
        return 0;
    if (!safe_cfg_value(user) || !safe_cfg_value(pass))
        return -1;
    int hard = (int)tm_ini_get_long(settings, "cheevos", "hardcore", 0);
    return tm_snprintf(out, size,
                       "cheevos_enable = \"true\"\ncheevos_username = \"%s\"\ncheevos_password = \"%s\"\n"
                       "cheevos_hardcore_mode_enable = \"%s\"",
                       user, pass, hard ? "true" : "false");
}

/* ------------------------------------------------------------ processes */

int tm_fw_path(char *out, size_t size, const char *abs)
{
    return tm_snprintf(out, size, "%s%s", tm_sysfs_root(), abs);
}

static void devnull_fd(int target, int flags)
{
    int fd = open("/dev/null", flags);
    if (fd >= 0 && fd != target) {
        dup2(fd, target);
        close(fd);
    }
}

/* Children start with default signal handling: ignored signals and the
 * blocked mask survive exec, and a server that ignores SIGTERM could not be
 * stopped. */
static void reset_signals(void)
{
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    for (int sig = 1; sig < NSIG; sig++)
        if (sig != SIGKILL && sig != SIGSTOP)
            signal(sig, SIG_DFL);
}

/* Children must not keep the menu's descriptors (display, input, log). */
static void close_from(int first)
{
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 4096)
        max = 4096;
    for (int fd = first; fd < max; fd++)
        close(fd);
}

int tm_run(char *const argv[], char *out, size_t outsz, int timeout_ms)
{
    int pfd[2];
    if (out && outsz)
        out[0] = '\0';
    if (pipe2(pfd, O_CLOEXEC) != 0)
        return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(pfd[1], STDOUT_FILENO);
        devnull_fd(STDIN_FILENO, O_RDONLY);
        devnull_fd(STDERR_FILENO, O_WRONLY);
        close_from(3);
        reset_signals();
        execv(argv[0], argv);
        _exit(127);
    }
    close(pfd[1]);
    size_t len = 0;
    uint64_t deadline = tm_now_ms() + (uint64_t)timeout_ms;
    int timed_out = 0;
    for (;;) {
        uint64_t now = tm_now_ms();
        if (now >= deadline) {
            timed_out = 1;
            break;
        }
        struct pollfd p = {pfd[0], POLLIN, 0};
        int r = poll(&p, 1, (int)(deadline - now));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            timed_out = r == 0;
            break;
        }
        char tmp[1024];
        ssize_t k = read(pfd[0], tmp, sizeof tmp);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            break;
        if (out && outsz > 1) {
            size_t room = outsz - 1 - len;
            size_t c = (size_t)k < room ? (size_t)k : room;
            memcpy(out + len, tmp, c);
            len += c;
            out[len] = '\0';
        }
    }
    close(pfd[0]);
    if (timed_out)
        kill(pid, SIGKILL);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
    }
    if (timed_out) {
        LOGW("net: %s timed out", argv[0]);
        return -1;
    }
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

pid_t tm_spawn(char *const argv[], const char *cwd, const char *ld_path)
{
    int pfd[2];
    if (pipe2(pfd, O_CLOEXEC) != 0)
        return -1;
    pid_t child = fork();
    if (child < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }
    if (child == 0) {
        setsid();
        pid_t gc = fork();
        if (gc == 0) {
            setpgid(0, 0); /* own group, so tm_ftp_stop can signal it and its children */
            pid_t self = getpid();
            ssize_t w = write(pfd[1], &self, sizeof self);
            (void)w;
            if (cwd && chdir(cwd) != 0)
                _exit(126);
            if (ld_path)
                setenv("LD_LIBRARY_PATH", ld_path, 1);
            devnull_fd(STDIN_FILENO, O_RDONLY);
            devnull_fd(STDOUT_FILENO, O_WRONLY);
            devnull_fd(STDERR_FILENO, O_WRONLY);
            reset_signals();
            /* pfd[1] is close-on-exec: the parent sees EOF once exec happened */
            for (int fd = 3; fd < 4096; fd++)
                if (fd != pfd[1])
                    close(fd);
            execv(argv[0], argv);
            _exit(127);
        }
        _exit(gc < 0);
    }
    close(pfd[1]);
    int st;
    while (waitpid(child, &st, 0) < 0 && errno == EINTR) {
    }
    pid_t gc = -1;
    if (read(pfd[0], &gc, sizeof gc) != (ssize_t)sizeof gc)
        gc = -1;
    /* Wait (bounded) until the program is really running, so a caller that
     * checks /proc/<pid>/cmdline right away sees it and not our fork. */
    uint64_t deadline = tm_now_ms() + 3000;
    for (char b; gc > 0;) {
        uint64_t now = tm_now_ms();
        if (now >= deadline)
            break;
        struct pollfd p = {pfd[0], POLLIN, 0};
        int r = poll(&p, 1, (int)(deadline - now));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0 || read(pfd[0], &b, 1) <= 0)
            break; /* EOF: exec done (or the child exited) */
    }
    close(pfd[0]);
    return gc;
}

static int busybox(const char *applet, const char *a1, const char *a2, const char *a3, int timeout_ms)
{
    char bb[TM_PATH_MAX];
    if (tm_fw_path(bb, sizeof bb, BUSYBOX) != 0)
        return -1;
    char *argv[] = {bb, (char *)applet, (char *)a1, (char *)a2, (char *)a3, NULL};
    return tm_run(argv, NULL, 0, timeout_ms);
}

int tm_proc_running(const char *name)
{
    return busybox("pidof", name, NULL, NULL, 2000) == 0;
}

static int fw_exists(const char *abs)
{
    char p[TM_PATH_MAX];
    return tm_fw_path(p, sizeof p, abs) == 0 && access(p, X_OK) == 0;
}

/* ------------------------------------------------------------ Wi-Fi */

static int wpa_cli(char *out, size_t outsz, const char *c1, const char *c2, const char *c3, const char *c4)
{
    char cli[TM_PATH_MAX];
    if (tm_fw_path(cli, sizeof cli, WPA_CLI) != 0)
        return -1;
    char *argv[] = {cli, "-p", WPA_SOCKETS, "-i", "wlan0", (char *)c1, (char *)c2, (char *)c3, (char *)c4, NULL};
    char buf[256];
    if (!out) {
        out = buf;
        outsz = sizeof buf;
    }
    int rc = tm_run(argv, out, outsz, 4000);
    /* wpa_cli exits 0 even when the command is refused; it prints FAIL */
    if (rc == 0 && strncmp(out, "FAIL", 4) == 0)
        return -1;
    return rc;
}

int tm_wifi_available(void)
{
    char p[TM_PATH_MAX];
    return fw_exists(WPA_CLI) && fw_exists(WPA_SUPPLICANT) && tm_fw_path(p, sizeof p, "/sys/class/net/wlan0") == 0 &&
           tm_dir_exists(p);
}

int tm_wifi_running(void) { return tm_proc_running("wpa_supplicant"); }

int tm_wifi_set(int on)
{
    if (!tm_wifi_available())
        return -1;
    if (!on) {
        /* same sequence as the firmware's runtrimui.sh */
        busybox("ifconfig", "wlan0", "down", NULL, 3000);
        busybox("killall", "-15", "wpa_supplicant", NULL, 3000);
        busybox("killall", "-9", "udhcpc", NULL, 3000);
        LOGI("net: wifi off");
        return 0;
    }
    busybox("ifconfig", "wlan0", "up", NULL, 3000);
    if (!tm_wifi_running()) {
        /* arguments of the firmware's /etc/init.d/wpa_supplicant, plus -B */
        char sup[TM_PATH_MAX];
        tm_fw_path(sup, sizeof sup, WPA_SUPPLICANT);
        char *argv[] = {sup, "-B", "-iwlan0", "-Dnl80211", "-c/etc/wifi/wpa_supplicant.conf",
                        "-I/etc/wifi/wpa_supplicant_overlay.conf", "-O" WPA_SOCKETS, NULL};
        if (tm_run(argv, NULL, 0, 8000) != 0) {
            LOGW("net: wpa_supplicant failed to start");
            return -1;
        }
    }
    if (!tm_proc_running("udhcpc")) {
        char bb[TM_PATH_MAX];
        tm_fw_path(bb, sizeof bb, BUSYBOX);
        char *argv[] = {bb, "udhcpc", "-i", "wlan0", NULL};
        tm_spawn(argv, "/", NULL);
    }
    LOGI("net: wifi on");
    return 0;
}

int tm_wifi_status(TmWifiStatus *st)
{
    char out[2048];
    memset(st, 0, sizeof *st);
    if (wpa_cli(out, sizeof out, "status", NULL, NULL, NULL) != 0)
        return -1;
    tm_wifi_parse_status(out, st);
    return 0;
}

int tm_wifi_scan_start(void) { return wpa_cli(NULL, 0, "scan", NULL, NULL, NULL); }

size_t tm_wifi_networks(TmWifiNet *out, size_t max)
{
    char buf[8192];
    if (wpa_cli(buf, sizeof buf, "list_network", NULL, NULL, NULL) != 0)
        return 0;
    return tm_wifi_parse_networks(buf, out, max);
}

size_t tm_wifi_scan_results(TmWifiAp *out, size_t max)
{
    char buf[16384];
    if (wpa_cli(buf, sizeof buf, "scan_result", NULL, NULL, NULL) != 0)
        return 0;
    size_t n = tm_wifi_parse_scan(buf, out, max);
    TmWifiNet nets[TM_WIFI_MAX];
    size_t nn = tm_wifi_networks(nets, TM_WIFI_MAX);
    tm_wifi_mark_saved(out, n, nets, nn);
    return n;
}

static void ensure_dhcp(void)
{
    if (tm_proc_running("udhcpc"))
        return;
    char bb[TM_PATH_MAX];
    tm_fw_path(bb, sizeof bb, BUSYBOX);
    char *argv[] = {bb, "udhcpc", "-i", "wlan0", NULL};
    tm_spawn(argv, "/", NULL);
}

int tm_wifi_connect(const char *ssid, const char *psk, int open)
{
    char hex[TM_SSID_MAX * 2 + 1], id[16], out[256];
    if (tm_wifi_ssid_hex(ssid, hex, sizeof hex) != 0)
        return -1;
    TmWifiNet nets[TM_WIFI_MAX];
    size_t nn = tm_wifi_networks(nets, TM_WIFI_MAX);
    for (size_t i = 0; i < nn; i++)
        if (strcmp(nets[i].ssid, ssid) == 0) {
            snprintf(id, sizeof id, "%d", nets[i].id);
            if (wpa_cli(NULL, 0, "select_network", id, NULL, NULL) != 0)
                return -1;
            ensure_dhcp();
            LOGI("net: connecting to saved network id %s", id);
            return 0;
        }
    if (!open && !tm_wifi_psk_valid(psk))
        return -1;
    if (wpa_cli(out, sizeof out, "add_network", NULL, NULL, NULL) != 0)
        return -1;
    /* the id is the last line of the output */
    char *last = out, *nl;
    tm_trim(out);
    while ((nl = strchr(last, '\n')))
        last = nl + 1;
    if (*last < '0' || *last > '9')
        return -1;
    snprintf(id, sizeof id, "%d", atoi(last));
    int rc = wpa_cli(NULL, 0, "set_network", id, "ssid", hex);
    if (open) {
        rc |= wpa_cli(NULL, 0, "set_network", id, "key_mgmt", "NONE");
    } else {
        char q[80];
        snprintf(q, sizeof q, "\"%s\"", psk);
        rc |= wpa_cli(NULL, 0, "set_network", id, "psk", q);
    }
    if (rc) {
        wpa_cli(NULL, 0, "remove_network", id, NULL, NULL);
        return -1;
    }
    /* same order as the stock MainUI */
    wpa_cli(NULL, 0, "enable_network", id, NULL, NULL);
    rc = wpa_cli(NULL, 0, "select_network", id, NULL, NULL);
    wpa_cli(NULL, 0, "save_config", NULL, NULL, NULL);
    ensure_dhcp();
    LOGI("net: saved new network id %s", id);
    return rc ? -1 : 0;
}

int tm_wifi_forget(int nid)
{
    char id[16];
    snprintf(id, sizeof id, "%d", nid);
    if (wpa_cli(NULL, 0, "remove_network", id, NULL, NULL) != 0)
        return -1;
    wpa_cli(NULL, 0, "save_config", NULL, NULL, NULL);
    LOGI("net: removed network id %s", id);
    return 0;
}

/* ------------------------------------------------------------ Bluetooth */

int tm_bt_available(void) { return fw_exists(BTMANAGER); }
int tm_bt_running(void) { return tm_proc_running("trimui_btmanager"); }

int tm_bt_set(int on)
{
    if (!tm_bt_available())
        return -1;
    if (!on) {
        busybox("killall", "-15", "trimui_btmanager", NULL, 3000);
        LOGI("net: bluetooth manager stopped");
        return 0;
    }
    if (tm_bt_running())
        return 0;
    char bin[TM_PATH_MAX], dir[TM_PATH_MAX], lib[TM_PATH_MAX];
    tm_fw_path(bin, sizeof bin, BTMANAGER);
    tm_fw_path(dir, sizeof dir, BTMANAGER_DIR);
    tm_fw_path(lib, sizeof lib, "/usr/trimui/lib");
    char *argv[] = {bin, NULL};
    /* started like runtrimui.sh does: from its folder, firmware libraries */
    if (tm_spawn(argv, dir, lib) < 0)
        return -1;
    LOGI("net: bluetooth manager started");
    return 0;
}

/* ------------------------------------------------------------ SSH */

int tm_ssh_available(void) { return fw_exists(SSHD_INIT) && fw_exists(SSHD); }
int tm_ssh_running(void) { return tm_proc_running("sshd"); }

int tm_ssh_set(int on)
{
    if (!tm_ssh_available())
        return -1;
    char init[TM_PATH_MAX];
    tm_fw_path(init, sizeof init, SSHD_INIT);
    /* the stock MainUI's "Enable SSH" switch runs exactly these */
    char *argv[] = {init, on ? "start" : "stop", NULL};
    int rc = tm_run(argv, NULL, 0, 15000);
    LOGI("net: ssh %s (rc %d)", on ? "start" : "stop", rc);
    return rc == 0 ? 0 : -1;
}

/* ------------------------------------------------------------ FTP */

int tm_ftp_available(void) { return fw_exists(BUSYBOX); }

static pid_t read_pid(const char *pidfile)
{
    long pid = 0;
    if (!pidfile || tm_read_long(pidfile, &pid) != 0 || pid <= 1)
        return -1;
    /* make sure the pid still belongs to our server */
    char path[64], *cmd;
    size_t len = 0;
    snprintf(path, sizeof path, "/proc/%ld/cmdline", pid);
    cmd = tm_read_file(path, 4096, &len);
    int ours = 0;
    for (size_t i = 0; cmd && i + 6 <= len; i++)
        if (memcmp(cmd + i, "tcpsvd", 6) == 0)
            ours = 1;
    free(cmd);
    return ours ? (pid_t)pid : -1;
}

int tm_ftp_running(const char *pidfile) { return read_pid(pidfile) > 0; }

int tm_ftp_start(const char *ip, int port, const char *dir, const char *pidfile)
{
    if (!ip || !ip[0] || !dir || !tm_dir_exists(dir))
        return -1;
    for (const char *p = ip; *p; p++)
        if (!((*p >= '0' && *p <= '9') || *p == '.'))
            return -1;
    tm_ftp_stop(pidfile);
    char bb[TM_PATH_MAX], ports[16];
    tm_fw_path(bb, sizeof bb, BUSYBOX);
    snprintf(ports, sizeof ports, "%d", port);
    /* ftpd chroots to dir; at most 4 clients; idle clients dropped after 10 min */
    char *argv[] = {bb, "tcpsvd", "-c", "4", (char *)ip, ports, bb, "ftpd", "-w", "-t", "600", (char *)dir, NULL};
    pid_t pid = tm_spawn(argv, "/", NULL);
    if (pid <= 0)
        return -1;
    char s[32];
    snprintf(s, sizeof s, "%d\n", (int)pid);
    if (tm_atomic_write(pidfile, s, strlen(s)) != 0) {
        kill(-pid, SIGTERM);
        return -1;
    }
    LOGI("net: ftp started on %s:%d (pid %d)", ip, port, (int)pid);
    return 0;
}

void tm_ftp_stop(const char *pidfile)
{
    pid_t pid = read_pid(pidfile);
    if (pid > 0) {
        kill(-pid, SIGTERM); /* tcpsvd and its ftpd children share the group */
        kill(pid, SIGTERM);
        for (int i = 0; i < 20 && read_pid(pidfile) > 0; i++)
            usleep(25000);
        if (read_pid(pidfile) > 0) { /* still there after 0.5 s */
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
        }
        LOGI("net: ftp stopped (pid %d)", (int)pid);
    }
    if (pidfile)
        unlink(pidfile);
}

/* ------------------------------------------------------------ boot */

void tm_net_apply(const TmIni *settings)
{
    const char *wifi = tm_ini_get(settings, "network", "wifi", "");
    if (tm_wifi_available() && (strcmp(wifi, "on") == 0 || strcmp(wifi, "off") == 0))
        tm_wifi_set(strcmp(wifi, "on") == 0);
    if (tm_bt_available() && tm_ini_get_long(settings, "network", "bluetooth", 0))
        tm_bt_set(1);
    /* The stock MainUI stops sshd unless its "Enable SSH" switch is on; TriMux
     * replaces MainUI, so it applies the same default: off unless enabled. */
    if (tm_ssh_available()) {
        int want = (int)tm_ini_get_long(settings, "network", "ssh", 0);
        if (want != tm_ssh_running())
            tm_ssh_set(want);
    }
}
