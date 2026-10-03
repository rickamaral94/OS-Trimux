"""Menu behaviour with SDL's offscreen driver and scripted button presses.

Simulated environment only: proves navigation logic, persistence and the
launch hand-off, not rendering speed or the physical controls."""
import os
import subprocess

import pytest

from conftest import UI, read, write

def _ui_runs():
    """The UI binary exists and its libraries resolve here (a host build copied
    into the container does not)."""
    if not os.path.exists(UI):
        return False
    r = subprocess.run(["ldd", UI], capture_output=True, text=True)
    return r.returncode == 0 and "not found" not in r.stdout


pytestmark = pytest.mark.skipif(not _ui_runs(), reason="build/native/trimux-ui not built for this system (make ui-native)")


def ui(env, script, done_wizard=True):
    if done_wizard:
        cfg = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini")
        if not os.path.exists(cfg):
            write(cfg, "[general]\nwizard_done = 1\n")
    e = dict(env, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy")
    return subprocess.run([UI, "--window", "1024", "768", "--script", script], env=e, capture_output=True,
                          text=True, timeout=60)


def test_wizard_then_home(env, tmp_path):
    shot = str(tmp_path / "w.bmp")
    r = ui(env, "shot=%s,A,A,A,DOWN,A,A,A,A" % shot, done_wizard=False)
    assert r.returncode == 0
    assert os.path.getsize(shot) > 100000
    cfg = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))
    assert "wizard_done = 1" in cfg


def test_launch_writes_validated_request(env):
    # Home: Favoritos, Todos, NES, GB, GBA, PS1... -> open "Todos os jogos" and play the first game
    r = ui(env, "DOWN,A,A")
    assert r.returncode == 10
    req = read(os.path.join(env["TRIMUX_TMP"], "launch.ini"))
    assert "rom = Roms/" in req and "emulator = " in req
    recent = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/recent.txt"))
    assert recent.startswith("Roms/")


def test_favorite_and_per_game_emulator(env):
    # Todos os jogos -> first game: X favorite, SELECT -> emulator list -> choose 2nd option (mgba or other)
    r = ui(env, "DOWN,A,X,SELECT,DOWN,A,B,B,B")
    assert r.returncode == 0
    fav = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/favorites.txt"))
    assert fav.startswith("Roms/")
    ov = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/overrides.ini")
    assert os.path.exists(ov) and "[games]" in read(ov)


def test_stock_and_poweroff_need_confirmation(env):
    assert ui(env, "MENU,UP,UP,UP,A,B").returncode == 0            # "Abrir sistema oficial" then No
    assert ui(env, "MENU,UP,A,LEFT,A").returncode == 30             # Desligar -> Sim
    assert ui(env, "MENU,UP,UP,UP,A,LEFT,A").returncode == 20       # Sistema oficial -> Sim


def test_missing_bios_blocks_launch(env):
    sd = env["TRIMUX_SDCARD"]
    write(os.path.join(sd, "Roms/NEOGEO/mslug.zip"), "")
    r = ui(env, "DOWN,A,UP,A")   # last game in "Todos" is mslug (alphabetical: m... depends) -> just ensure no crash
    assert r.returncode in (0, 10)
    if r.returncode == 10:
        assert "NEOGEO" not in read(os.path.join(env["TRIMUX_TMP"], "launch.ini"))


def test_search_filters(env, tmp_path):
    shot = str(tmp_path / "s.bmp")
    r = ui(env, "Y,DOWN,DOWN,DOWN,RIGHT,RIGHT,RIGHT,A,START,shot=%s,A" % shot)
    # typed a letter and confirmed; A on results either launches or nothing if no match
    assert r.returncode in (0, 10)
    assert os.path.getsize(shot) > 100000


def test_corrupt_settings_do_not_crash(env):
    write(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"),
          "\x00\x01garbage\n[general\nwizard_done = 1\nlanguage = ../../etc\n")
    write(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/cache/library.tsv"), "#TRIMUX-LIB 1\nXX\tbad\n")
    assert ui(env, "DOWN,DOWN,UP").returncode == 0


def test_switch_boost_needs_confirmation_in_menu(env):
    cfg = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini")
    page = "UP,A,DOWN,DOWN,A,DOWN,DOWN,A,DOWN,DOWN"
    # none -> economy -> leds_off -> mute -> boost (dialog, default answer "No")
    assert ui(env, page + ",RIGHT,RIGHT,RIGHT,RIGHT,A,B,B,B").returncode == 0
    assert "boost" not in read(cfg)
    assert ui(env, page + ",RIGHT,LEFT,A,B,B,B").returncode == 0   # from "mute": dialog, "Yes"
    text = read(cfg)
    assert "switch = boost" in text and "boost_ack = 1" in text


NET = "UP,A" + ",DOWN" * 6 + ",A"     # Home -> Configurações -> Rede e conexões (lands on "Wi-Fi")


def net_log(env):
    p = os.path.join(env["TRIMUX_SYSFS_ROOT"], "net.log")
    return read(p) if os.path.exists(p) else ""


def wifi_connected(env):
    dev = env["TRIMUX_SYSFS_ROOT"]
    write(os.path.join(dev, "run/wpa_supplicant"), "")
    write(os.path.join(dev, "netstate/status"), "wpa_state=COMPLETED\nssid=Casa\nip_address=127.0.0.1\n")


def test_wifi_on_scan_and_connect_with_password(env):
    # Wi-Fi on -> Procurar redes -> "Casa" (strongest) -> type 8 letters -> START
    r = ui(env, NET + ",A,DOWN,DOWN,A,DOWN,A" + ",A" * 8 + ",START,wait=1200,B,B,B")
    assert r.returncode == 0
    log = net_log(env)
    assert "wpa_supplicant -B -iwlan0" in log
    assert "set_network 0 ssid 43617361" in log          # "Casa", hex-encoded
    assert 'set_network 0 psk "qqqqqqqq"' in log and "save_config" in log
    cfg = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))
    assert "wifi = on" in cfg and "qqqqqqqq" not in cfg
    assert "qqqqqqqq" not in read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/logs/trimux.log"))


def test_wifi_short_password_is_not_sent(env):
    wifi_connected(env)
    write(os.path.join(env["TRIMUX_SYSFS_ROOT"], "netstate/status"), "wpa_state=SCANNING\n")
    r = ui(env, NET + ",DOWN,DOWN,A,DOWN,A,A,A,A,START,B,B,B,B")
    assert r.returncode == 0
    assert "add_network" not in net_log(env)


def test_ftp_runs_only_while_dialog_is_open(env, tmp_path):
    wifi_connected(env)
    shot = str(tmp_path / "ftp.bmp")
    r = ui(env, NET + ",DOWN" * 6 + ",A,shot=%s,B" % shot)
    assert r.returncode == 0
    dev, sd = env["TRIMUX_SYSFS_ROOT"], env["TRIMUX_SDCARD"]
    assert "busybox tcpsvd -c 4 127.0.0.1 21 %s/bin/busybox ftpd -w -t 600 %s" % (dev, sd) in net_log(env)
    assert not os.path.exists(os.path.join(env["TRIMUX_TMP"], "ftp.pid"))
    ps = subprocess.run(["pgrep", "-f", "%s/bin/busybox tcpsvd" % dev], capture_output=True)
    assert ps.returncode == 1, "FTP server left running"


def test_ftp_stops_when_menu_exits(env):
    wifi_connected(env)
    assert ui(env, NET + ",DOWN" * 6 + ",A").returncode == 0   # script ends with the dialog open
    ps = subprocess.run(["pgrep", "-f", "%s/bin/busybox tcpsvd" % env["TRIMUX_SYSFS_ROOT"]], capture_output=True)
    assert ps.returncode == 1


def test_ssh_needs_confirmation(env):
    ssh = NET + ",DOWN" * 7
    assert ui(env, ssh + ",A,A,B,B,B").returncode == 0          # dialog defaults to "No"
    assert "sshd start" not in net_log(env)
    assert ui(env, ssh + ",A,LEFT,A,B,B,B").returncode == 0
    assert "sshd start" in net_log(env)
    assert "ssh = 1" in read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))


def test_retroachievements_account_entry(env):
    cheevos = NET + ",DOWN" * 5 + ",A"
    # enable, user "qqq"; password with a quote is refused
    r = ui(env, cheevos + ",A,DOWN,A,A,A,A,START,DOWN,A,R1,R1,DOWN,RIGHT,RIGHT,RIGHT,A,START,B,B,B")
    assert r.returncode == 0
    cfg = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))
    assert "[cheevos]" in cfg and "enable = 1" in cfg and "user = qqq" in cfg and "password" not in cfg
    r = ui(env, cheevos + ",DOWN,DOWN,A,A,A,A,A,A,A,START,B,B,B")
    cfg = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))
    assert "password = qqqqqq" in cfg


DIAG = "UP,A" + ",DOWN" * 10 + ",A" + ",DOWN" * 4 + ",A"   # Configurações -> Sistema -> Registros e desempenho


def test_diagnostics_toggles(env):
    assert ui(env, DIAG + ",A,DOWN,A,DOWN,DOWN,A,B,B,B").returncode == 0   # perf, FPS, detailed log
    cfg = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))
    assert "[diag]" in cfg and "perf = 1" in cfg and "show_fps = 1" in cfg and "verbose = 1" in cfg


def test_sessions_page_and_clear(env, tmp_path):
    perf = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/logs/perf")
    write(os.path.join(perf, "sessions.csv"),
          "inicio;plataforma;emulador;jogo;perfil;duracao_s;cpu_media_mhz;cpu_max_mhz;temp_inicio_c;temp_max_c;"
          "temp_fim_c;bateria_inicio;bateria_fim;carregando;protecao_termica;saida\n"
          "2026-10-02 13:00;GBA;gpsp;Celeste.gba;balanced;1800;1404;1608;45;61;58;90;80;0;0;0\n"
          "2026-10-02 14:00;PS;pcsx_rearmed;Demo.cue;boost;1800;1890;2000;46;74;71;80;66;0;1;0\n")
    write(os.path.join(perf, "20261002-140000_PS.csv"), "tempo_s;cpu_mhz\n")
    write(os.path.join(perf, "minhas-notas.txt"), "x")
    shot = str(tmp_path / "perf.bmp")
    assert ui(env, DIAG + ",DOWN,DOWN,A,shot=%s,B,B,B,B" % shot).returncode == 0
    assert os.path.getsize(shot) > 100000
    # clear needs confirmation (default "No"), then removes only performance files
    assert ui(env, DIAG + ",UP,A,A,B,B,B").returncode == 0
    assert os.path.exists(os.path.join(perf, "sessions.csv"))
    assert ui(env, DIAG + ",UP,A,LEFT,A,B,B,B").returncode == 0
    assert not os.path.exists(os.path.join(perf, "sessions.csv"))
    assert not os.path.exists(os.path.join(perf, "20261002-140000_PS.csv"))
    assert os.path.exists(os.path.join(perf, "minhas-notas.txt"))


def bmp_pixel(path, x, y):
    import struct
    with open(path, "rb") as f:
        data = f.read()
    off, = struct.unpack_from("<I", data, 10)
    w, h = struct.unpack_from("<ii", data, 18)
    bpp, = struct.unpack_from("<H", data, 28)
    stride = ((w * bpp // 8) + 3) & ~3
    row = (h - 1 - y) if h > 0 else y
    p = off + row * stride + x * (bpp // 8)
    b, g, r = data[p], data[p + 1], data[p + 2]
    return r, g, b


def test_cover_shown_in_game_panel(env, tmp_path):
    from conftest import png_bytes
    sd = env["TRIMUX_SDCARD"]
    write(os.path.join(sd, "Imgs/GBA/Celeste Classic (World).png"), png_bytes(320, 480, (200, 40, 40)))
    on, off = str(tmp_path / "on.bmp"), str(tmp_path / "off.bmp")
    assert ui(env, "DOWN,A,shot=%s" % on).returncode == 0          # "Todos os jogos", Celeste first
    assert bmp_pixel(on, 811, 200) == (200, 40, 40)
    write(os.path.join(sd, "TriMuxData/config/trimux.ini"), "[general]\nwizard_done = 1\n[covers]\nshow = 0\n")
    assert ui(env, "DOWN,A,shot=%s" % off).returncode == 0
    assert bmp_pixel(off, 811, 200) != (200, 40, 40)


COVERS = "UP,A" + ",DOWN" * 7 + ",A" + ",DOWN" * 5 + ",A"   # Configurações -> Biblioteca -> Capas dos jogos


def test_covers_download_from_menu(env):
    import shutil
    import time
    from conftest import CTL
    sd, dev = env["TRIMUX_SDCARD"], env["TRIMUX_SYSFS_ROOT"]
    os.makedirs(os.path.join(sd, "TriMux/bin"), exist_ok=True)
    shutil.copy(CTL, os.path.join(sd, "TriMux/bin/trimuxctl"))
    # without Wi-Fi: explanation, nothing started
    assert ui(env, COVERS + ",A,A,B,B,B").returncode == 0
    assert "curl" not in net_log(env)
    wifi_connected(env)
    write(os.path.join(dev, "netstate/covers"),
          "https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/"
          "Celeste%20Classic%20%28World%29.png\n")
    assert ui(env, COVERS + ",A,B,B,B").returncode == 0
    cover = os.path.join(sd, "Imgs/GBA/Celeste Classic (World).png")
    for _ in range(100):
        if os.path.exists(cover):
            break
        time.sleep(0.1)
    assert os.path.exists(cover)


def test_covers_options_saved(env):
    # show off; type boxart -> snap; automatic on
    assert ui(env, COVERS + ",DOWN,DOWN,DOWN,RIGHT,DOWN,A,DOWN,A,B,B,B").returncode == 0
    cfg = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))
    assert "[covers]" in cfg and "kind = snap" in cfg and "auto = 1" in cfg and "show = 0" in cfg


def test_card_grow_result_shown_after_reboot(env):
    state = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/state")
    write(os.path.join(state, "card-grown"), "1048576\n")         # 1 MiB before: the card grew
    assert ui(env, "wait=50").returncode == 0
    assert not os.path.exists(os.path.join(state, "card-grown"))
    log = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/logs/trimux.log"))
    assert "card grown" in log
    write(os.path.join(state, "card-grown"), "%d\n" % (1 << 50))   # bigger than now: it did not work
    assert ui(env, "wait=50").returncode == 0
    assert "did not take effect" in read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/logs/trimux.log"))


def test_start_screen(env, tmp_path):
    shot = str(tmp_path / "splash.bmp")
    e = dict(env, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy")
    import subprocess as sp
    from conftest import UI
    assert sp.run([UI, "--window", "1024", "768", "--splash-shot", shot], env=e, timeout=60).returncode == 0
    assert bmp_pixel(shot, 600, 470) == (0x2e, 0x86, 0xde)   # "Mux" in the accent colour


POWER = "UP,A,DOWN,DOWN,DOWN,A"   # Configurações -> Energia


def test_profile_locked_while_boost_switch_is_on(env):
    cfg = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini")
    gpio = os.path.join(env["TRIMUX_SYSFS_ROOT"], "sys/class/gpio/gpio243/value")
    write(cfg, "[general]\nwizard_done = 1\n[power]\nprofile = balanced\nboost_ack = 1\n[buttons]\nswitch = boost\n")
    write(gpio, "1\n")                                       # switch on
    assert ui(env, POWER + ",RIGHT,A,B,B,B").returncode == 0
    assert "profile = balanced" in read(cfg)                  # unchanged: locked
    write(gpio, "0\n")                                       # switch off: free again
    assert ui(env, POWER + ",RIGHT,B,B,B").returncode == 0
    assert "profile = balanced" not in read(cfg)
