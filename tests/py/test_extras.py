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


@needs_ui
def test_apps_section_lists_and_starts(env, apps):
    shot = os.path.join(os.path.dirname(apps["marker"]), "apps.bmp")
    # home: ... platforms, Aplicativos, Configurações (last): UP,UP reaches Aplicativos
    r = ui(env, "UP,UP,A,shot=%s,A" % shot)
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
