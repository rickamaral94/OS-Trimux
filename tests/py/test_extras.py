"""Apps section, date and time, the volume/brightness indicator and the LED
master switch, on a simulated Brick Pro (fake firmware tools log their calls
to net.log). Not a hardware test."""
import os
import subprocess
import threading
import time

import pytest

from conftest import CTL, ctl, read, write
from test_ui import _ui_runs, bmp_pixel, ui

needs_ui = pytest.mark.skipif(not _ui_runs(), reason="build/native/trimux-ui not built for this system")
SYSTEM = "UP,A" + ",DOWN" * 10 + ",A"          # Configurações -> Sistema
DATETIME = SYSTEM + ",DOWN" * 3 + ",A"         # -> Data e hora
LEDS = "UP,A" + ",DOWN" * 5 + ",A"             # Configurações -> LEDs


def net_log(env):
    p = os.path.join(env["TRIMUX_SYSFS_ROOT"], "net.log")
    return open(p).read() if os.path.exists(p) else ""


def make_app(root, name, label, script_body="", desc="Teste"):
    d = os.path.join(root, name)
    write(os.path.join(d, "config.json"),
          '{"label":"%s","icon":"","icontop":"","launch":"launch.sh","description":"%s"}\n' % (label, desc))
    write(os.path.join(d, "launch.sh"), "#!/bin/sh\n" + script_body, 0o755)
    return d


@pytest.fixture
def apps(env, tmp_path):
    sd, dev = env["TRIMUX_SDCARD"], env["TRIMUX_SYSFS_ROOT"]
    marker = str(tmp_path / "ran")
    card = make_app(os.path.join(sd, "Apps"), "Hello", "Olá", 'pwd > "%s"\necho "$TRIMUX" >> "%s"\n' % (marker, marker))
    make_app(os.path.join(dev, "usr/trimui/apps"), "musicplayer", "Music")
    make_app(os.path.join(dev, "usr/trimui/apps"), "zformatter_fat32", "SD Formatter")   # never listed
    make_app(os.path.join(dev, "mnt/UDISK/Apps"), "Notes", "Notas")
    write(os.path.join(sd, "Apps/Broken/config.json"), '{"label":"x","launch":"../../evil.sh"}')
    return {"card": card, "marker": marker}


# ---- apps ----

def test_app_runs_from_its_folder(env, apps):
    os.makedirs(env["TRIMUX_TMP"], exist_ok=True)
    write(os.path.join(env["TRIMUX_TMP"], "app.ini"), "[app]\ndir = %s\n" % apps["card"])
    ctl(env, "app")
    out = read(apps["marker"]).splitlines()
    assert os.path.realpath(out[0]) == os.path.realpath(apps["card"]) and out[1] == "1"
    assert not os.path.exists(os.path.join(env["TRIMUX_TMP"], "app.ini"))      # one request, one start


def test_app_requests_outside_the_app_folders_are_refused(env, apps, tmp_path):
    os.makedirs(env["TRIMUX_TMP"], exist_ok=True)
    evil = make_app(str(tmp_path / "elsewhere"), "Evil", "Evil", 'touch "%s/pwned"\n' % tmp_path)
    blocked = os.path.join(env["TRIMUX_SYSFS_ROOT"], "usr/trimui/apps/zformatter_fat32")
    for d in (evil, blocked, os.path.join(env["TRIMUX_SDCARD"], "Apps/Broken"), apps["card"] + "/.."):
        write(os.path.join(env["TRIMUX_TMP"], "app.ini"), "[app]\ndir = %s\n" % d)
        assert ctl(env, "app", check=False).returncode == 1
    assert not os.path.exists(str(tmp_path / "pwned"))


def test_app_starts_like_the_stock_menu_with_relative_launcher(env, apps):
    """Grout's launch.sh finds its bundled library from a relative $0 (the
    stock menu runs "cd <folder>; ./launch.sh"); with a full path it fails."""
    sd = env["TRIMUX_SDCARD"]
    d = make_app(os.path.join(sd, "Apps"), "Grout", "Grout",
                 'CUR_DIR="$(dirname "$0")"\ncd "$CUR_DIR"/grout || exit 1\n'
                 'export LD_LIBRARY_PATH=$CUR_DIR/lib:$LD_LIBRARY_PATH\n'
                 '[ -f "${LD_LIBRARY_PATH%%:*}/libSDL2_gfx-1.0.so.0" ] || { echo "lib missing" >&2; exit 127; }\n'
                 'echo started > ../ran\n')
    write(os.path.join(d, "grout/lib/libSDL2_gfx-1.0.so.0"), "x")
    os.makedirs(env["TRIMUX_TMP"], exist_ok=True)
    write(os.path.join(env["TRIMUX_TMP"], "app.ini"), "[app]\ndir = %s\n" % d)
    ctl(env, "app")
    assert read(os.path.join(d, "ran")).strip() == "started"


def test_app_output_is_saved_and_a_quick_failure_is_reported(env, apps):
    sd, tmp = env["TRIMUX_SDCARD"], env["TRIMUX_TMP"]
    d = make_app(os.path.join(sd, "Apps"), "Fails", "Falha", 'echo "hello from app"\necho "no libfoo.so" >&2\nexit 3\n')
    os.makedirs(tmp, exist_ok=True)
    write(os.path.join(tmp, "app.ini"), "[app]\ndir = %s\n" % d)
    ctl(env, "app")
    log = read(os.path.join(sd, "TriMuxData/logs/apps/Fails.log"))
    assert "hello from app" in log and "no libfoo.so" in log and "exit code 3" in log
    last = read(os.path.join(tmp, "lastrun.ini"))
    assert "label = Falha" in last and "code = 3" in last and "log = TriMuxData/logs/apps/Fails.log" in last


def test_app_output_keeps_only_the_end(env, apps):
    sd, tmp = env["TRIMUX_SDCARD"], env["TRIMUX_TMP"]
    # ~200 KB of output: only the last 32 KiB reach the card
    d = make_app(os.path.join(sd, "Apps"), "Chatty", "Chatty",
                 'i=0\nwhile [ $i -lt 4000 ]; do echo "line $i ................................"; i=$((i+1)); done\n')
    os.makedirs(tmp, exist_ok=True)
    write(os.path.join(tmp, "app.ini"), "[app]\ndir = %s\n" % d)
    ctl(env, "app")
    log = read(os.path.join(sd, "TriMuxData/logs/apps/Chatty.log"))
    assert "line 3999 " in log and "line 0 " not in log and "only the last 32 KiB" in log
    assert len(log) < 33 * 1024


@needs_ui
def test_menu_tells_when_an_app_closed_right_away(env, apps, tmp_path):
    tmp = env["TRIMUX_TMP"]
    os.makedirs(tmp, exist_ok=True)
    write(os.path.join(tmp, "lastrun.ini"),
          "[run]\nlabel = Grout\nlog = TriMuxData/logs/apps/Grout.log\ncode = 127\nseconds = 1\n")
    shot = str(tmp_path / "closed.bmp")
    ui(env, "shot=%s" % shot)
    assert "Grout closed after 1 s with code 127" in read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/logs/trimux.log"))
    assert not os.path.exists(os.path.join(tmp, "lastrun.ini"))
    # a normal session (minutes, code 0) says nothing
    write(os.path.join(tmp, "lastrun.ini"), "[run]\nlabel = Notes\ncode = 0\nseconds = 600\n")
    ui(env, "shot=%s" % shot)
    assert "Notes closed" not in read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/logs/trimux.log"))


@needs_ui
def test_apps_section_lists_and_starts(env, apps):
    shot = os.path.join(os.path.dirname(apps["marker"]), "apps.bmp")
    # home: ... platforms, Aplicativos, Configurações (last): UP,UP reaches Aplicativos;
    # the 5 TriMux tools come first, then the installed apps
    r = ui(env, "UP,UP,A,shot=%s,DOWN,DOWN,DOWN,DOWN,DOWN,A" % shot)
    assert r.returncode == 11
    ini = read(os.path.join(env["TRIMUX_TMP"], "app.ini"))
    assert "musicplayer" in ini or "Apps/Hello" in ini or "Notes" in ini
    assert os.path.getsize(shot) > 100000


# ---- date and time ----

def test_time_tz_and_sync(env, tmp_path):
    dev = env["TRIMUX_SYSFS_ROOT"]
    write(os.path.join(dev, "usr/share/zoneinfo/America/Sao_Paulo"), "TZif")
    assert ctl(env, "time", "tz").stdout == ""                 # firmware zone by default
    write(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"), "[time]\nzone = America/Sao_Paulo\n")
    assert ctl(env, "time", "tz").stdout.strip() == ":" + os.path.join(dev, "usr/share/zoneinfo/America/Sao_Paulo")
    write(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"), "[time]\nzone = Mars/Base\n")
    assert ctl(env, "time", "tz").stdout == ""                 # unknown zones are ignored
    # internet time: only with Wi-Fi; then the RTC is saved in UTC like the firmware does
    assert ctl(env, "time", "sync", check=False).returncode == 1
    assert "ntpd" not in net_log(env)
    write(os.path.join(dev, "run/wpa_supplicant"), "")
    write(os.path.join(dev, "netstate/status"), "wpa_state=COMPLETED\nssid=Casa\nip_address=127.0.0.1\n")
    ctl(env, "time", "sync")
    log = net_log(env)
    assert "busybox ntpd -n -q" in log and "busybox hwclock -w -u -f /dev/rtc0" in log
    write(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"), "[time]\nntp = 0\n")
    open(os.path.join(dev, "net.log"), "w").close()
    ctl(env, "time", "sync", "--auto")
    assert "ntpd" not in net_log(env)


@needs_ui
def test_datetime_page_zone_and_manual_set(env):
    dev = env["TRIMUX_SYSFS_ROOT"]
    write(os.path.join(dev, "usr/share/zoneinfo/America/Sao_Paulo"), "TZif")
    # zone: RIGHT from "the stock system's" -> first zone in the list
    assert ui(env, DATETIME + ",DOWN,RIGHT,B,B,B").returncode == 0
    assert "zone = America/Sao_Paulo" in read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))
    # manual: year +1, then "Aplicar data e hora" -> busybox date -s @<epoch> and hwclock
    r = ui(env, DATETIME + ",DOWN" * 6 + ",RIGHT,DOWN,DOWN,DOWN,A,B,B,B")   # Ano +1, Aplicar
    assert r.returncode == 0
    log = net_log(env)
    assert "busybox date -s @" in log and "busybox hwclock -w -u -f /dev/rtc0" in log
    epoch = int(log.split("busybox date -s @")[1].split()[0])
    assert abs(time.gmtime(epoch).tm_year - (time.localtime().tm_year + 1)) <= 1   # the year was raised by one


# ---- volume / brightness indicator ----

@needs_ui
def test_volume_indicator_follows_keymon(env, tmp_path):
    sysjson = os.path.join(env["TRIMUX_SYSFS_ROOT"], "mnt/UDISK/system.json")
    write(sysjson, '{"vol": 10, "brightness": 6}\n')
    shot = str(tmp_path / "osd.bmp")

    def press_volume_up():
        time.sleep(1.2)
        write(sysjson, '{"vol": 11, "brightness": 6}\n')
        os.utime(sysjson, (time.time() + 3, time.time() + 3))
    t = threading.Thread(target=press_volume_up)
    t.start()
    assert ui(env, "wait=300,wait=1500,wait=300,shot=%s" % shot).returncode == 0
    t.join()
    # 11 of 20 segments lit in the accent colour, the 12th not
    h, pad = 768, 20
    w = h * 560 // 768
    x = (1024 - w) // 2 + pad
    sy = h * 88 // 768 + h * 104 // 768 - pad - h * 22 // 768 + 5
    gap = h * 4 // 768
    sw = (w - 2 * pad - 19 * gap) // 20
    accent = (0x2e, 0x86, 0xde)
    assert bmp_pixel(shot, x + 10 * (sw + gap) + sw // 2, sy) == accent
    assert bmp_pixel(shot, x + 11 * (sw + gap) + sw // 2, sy) != accent


# ---- LED master switch ----

@needs_ui
def test_led_master_switch_turns_everything_off_now(env):
    led = os.path.join(env["TRIMUX_SYSFS_ROOT"], "sys/class/led_anim")
    write(os.path.join(led, "enable"), "1\n")
    write(os.path.join(led, "effect_m"), "4\n")
    assert ui(env, LEDS + ",A,B,B").returncode == 0                 # "Luzes" -> Apagadas
    cfg = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))
    assert "user_off = 1" in cfg and "managed = 1" in cfg
    assert read(os.path.join(led, "effect_m")) == "0"
    assert read(os.path.join(led, "effect_rgb_hex_m")) == "000000"
    assert read(os.path.join(led, "enable")) == "0"
    assert ui(env, LEDS + ",A,B,B").returncode == 0                 # and back on
    assert "user_off = 0" in read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))
    assert read(os.path.join(led, "effect_m")) == "4" and read(os.path.join(led, "enable")) == "1"


@needs_ui
def test_all_lights_sets_every_zone_at_once(env):
    led = os.path.join(env["TRIMUX_SYSFS_ROOT"], "sys/class/led_anim")
    # "Todas as luzes" (third row) -> Cor (second row) -> next colour
    assert ui(env, LEDS + ",DOWN,DOWN,A,DOWN,RIGHT,B,B,B").returncode == 0
    cfg = read(os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini"))
    colors = [l.split("=")[1].strip() for l in cfg.splitlines() if l.startswith("color =")]
    assert len(colors) >= 4 and len(set(colors)) == 1 and colors[0] != "FFFFFF"
    hexes = {read(os.path.join(led, "effect_rgb_hex_%s" % z)).strip().upper() for z in ("m", "lr", "f1", "f2", "rear")}
    assert hexes == {colors[0]}


# ---- game picture (Settings > Emulators) ----
EMULATORS = "UP,A" + ",DOWN" * 8 + ",A"         # Configurações -> Emuladores


@needs_ui
def test_image_page_per_platform_and_all(env, tmp_path):
    cfg = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini")
    shot = str(tmp_path / "fc.bmp")
    # NES (first platform): Formato -> Pixels perfeitos, Visual -> two steps (Pixel suave)
    assert ui(env, EMULATORS + ",DOWN,A,RIGHT,DOWN,RIGHT,RIGHT,shot=%s,B,B,B" % shot).returncode == 0
    ini = read(cfg)
    assert "[video.FC]" in ini and "aspect = integer" in ini and "filter = pixel" in ini
    assert "[video.SFC]" not in ini
    # all platforms at once: platforms differ ("Variado"), one step left = TV antiga
    assert ui(env, EMULATORS + ",A,DOWN,LEFT,shot=%s,B,B,B" % shot).returncode == 0
    ini = read(cfg)
    assert ini.count("filter = crt") >= 20 and "[video.GBA]" in ini


@needs_ui
def test_image_page_extras_follow_the_emulator(env, tmp_path):
    cfg = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini")
    os.makedirs(os.path.join(env["TRIMUX_SDCARD"], "Bios/HdPacks/Micro Mages (World)"), exist_ok=True)
    write(os.path.join(env["TRIMUX_SDCARD"], "Bios/HdPacks/Micro Mages (World)/hires.txt"), "<ver>106\n")
    # NES: Formato, Visual, Resolução (não disponível), Texturas HD -> off
    shot = str(tmp_path / "hd.bmp")
    assert ui(env, EMULATORS + ",DOWN,A,DOWN,DOWN,DOWN,shot=%s,A,B,B,B" % shot).returncode == 0
    assert "hdpacks = 0" in read(cfg)


@needs_ui
def test_fast_mode_switch_on_heavy_platforms(env, tmp_path):
    cfg = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini")
    shot = str(tmp_path / "psp.bmp")
    write(os.path.join(env["TRIMUX_SDCARD"], "TriMux/retroarch/cores/ppsspp_libretro.so"), "fake core\n")
    # PSP (13th platform): Formato, Visual, Resolução interna, Modo rápido -> on
    assert ui(env, EMULATORS + ",DOWN" * 13 + ",A,DOWN,DOWN,DOWN,A,shot=%s,B,B,B" % shot).returncode == 0
    ini = read(cfg)
    assert "[video.PSP]" in ini and "speed = 1" in ini
    assert os.path.getsize(shot) > 100000


# ---- game order (Settings > Library) ----
LIBRARY = "UP,A" + ",DOWN" * 7 + ",A"           # Configurações -> Biblioteca


@needs_ui
def test_game_order_popularity_and_most_played(env):
    sd = env["TRIMUX_SDCARD"]
    for f in ("Aaa Homebrew (World).nes", "Tetris (USA).nes", "Super Mario Bros. 3 (USA) (Rev 1).nes"):
        write(os.path.join(sd, "Roms/FC", f), "")
    cfg = os.path.join(sd, "TriMuxData/config/trimux.ini")

    def first_game(sort):
        write(cfg, "[general]\nwizard_done = 1\nsort = %s\n" % sort)
        for f in (os.path.join(sd, "TriMuxData/config/recent.txt"), os.path.join(env["TRIMUX_TMP"], "ui_state.ini")):
            if os.path.exists(f):
                os.unlink(f)                                  # start from the home screen each time
        assert ui(env, "DOWN,A,A").returncode == 10          # Todos os jogos -> play the first one
        return read(os.path.join(env["TRIMUX_TMP"], "launch.ini"))

    assert "Aaa Homebrew" in first_game("name")
    # NES list: Super Mario Bros. 3 is 3rd, Tetris 4th; the other games have no rank
    assert "Super Mario Bros. 3" in first_game("popular")
    write(os.path.join(sd, "TriMuxData/state/plays.ini"),
          "[plays]\nRoms/GBA/Celeste Classic (World).gba = 2 600 0\nRoms/FC/Micro Mages (World).nes = 1 1200 0\n")
    assert "Micro Mages" in first_game("played")


@needs_ui
def test_game_order_menu_cycles(env):
    cfg = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini")
    # Biblioteca: last item is "Ordem dos jogos"
    assert ui(env, LIBRARY + ",UP,RIGHT,B,B,B").returncode == 0
    assert "sort = popular" in read(cfg)
    assert ui(env, LIBRARY + ",UP,RIGHT,B,B,B").returncode == 0
    assert "sort = played" in read(cfg)
