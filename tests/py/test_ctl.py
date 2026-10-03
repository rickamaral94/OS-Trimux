"""trimuxctl against a simulated Brick Pro (see conftest.py)."""
import os
import shutil
import time

from conftest import POLICY, ctl, make_device, read, write


def test_device_check(env, tmp_path):
    assert ctl(env, "device", check=False).returncode == 0
    other = make_device(str(tmp_path / "brick"), brick_pro=False)
    assert ctl(dict(env, TRIMUX_SYSFS_ROOT=other), "device", check=False).returncode == 1


def test_power_profiles_never_exceed_rated_max(env, device):
    out = ctl(env, "power", "list").stdout
    for line in out.splitlines():
        name, *kv = line.split()
        fields = dict(x.split("=") for x in kv)
        assert int(fields["max"]) <= 1800000, line
        assert fields["gov"] != "performance"
    for prof, mx in (("economy", 1200000), ("balanced", 1608000), ("performance", 1800000)):
        ctl(env, "power", "apply", prof)
        assert int(read(os.path.join(device, POLICY, "scaling_max_freq"))) == mx
    ctl(env, "power", "default")
    assert int(read(os.path.join(device, POLICY, "scaling_max_freq"))) == 1608000
    assert ctl(env, "power", "apply", "turbo", check=False).returncode == 1


def test_power_unavailable_is_reported(env, tmp_path):
    dev = make_device(str(tmp_path / "nofreq"), cpufreq=False)
    r = ctl(dict(env, TRIMUX_SYSFS_ROOT=dev), "power", "status", check=False)
    assert r.returncode == 2 and "cpufreq=no" in r.stdout
    assert ctl(dict(env, TRIMUX_SYSFS_ROOT=dev), "power", "apply", "economy", check=False).returncode == 2


def test_thermal_files_untouched(env, device):
    zone = os.path.join(device, "sys/class/thermal/thermal_zone0")
    before = sorted(os.listdir(zone))
    ctl(env, "power", "apply", "performance")
    assert sorted(os.listdir(zone)) == before
    assert read(os.path.join(zone, "temp")) == "48000"


def test_leds_detect_and_apply(env, device, card):
    out = ctl(env, "leds", "detect").stdout
    for z in ("m", "lr", "f1", "f2", "rear"):
        assert "zone %s" % z in out
    # not managed: firmware behaviour untouched
    ctl(env, "leds", "apply")
    assert read(os.path.join(device, "sys/class/led_anim/effect_rgb_hex_m")) == "0"
    cfg = os.path.join(card, "TriMuxData/config/trimux.ini")
    write(cfg, "[leds]\nmanaged = 1\n[leds.m]\ncolor = 00FF00\nbrightness = 30\neffect = 4\n")
    # state left by the firmware: master switch off (stock "LED" setting off),
    # frame animations on, one repetition from runtrimui.sh's boot flash
    led = os.path.join(device, "sys/class/led_anim")
    write(os.path.join(led, "enable"), "0\n")
    write(os.path.join(led, "anim_frames_enable"), "1\n")
    write(os.path.join(led, "effect_cycles_m"), "1\n")
    ctl(env, "leds", "apply")
    assert read(os.path.join(led, "enable")) == "1"
    assert read(os.path.join(led, "effect_enable")) == "1"
    assert read(os.path.join(led, "anim_frames_enable")) == "0"
    assert read(os.path.join(led, "effect_cycles_m")) == "30000"
    assert read(os.path.join(device, "sys/class/led_anim/effect_rgb_hex_m")) == "00FF00"
    assert read(os.path.join(device, "sys/class/led_anim/max_scale")) == "30"
    assert read(os.path.join(device, "sys/class/led_anim/effect_m")) == "4"


def test_leds_absent(env, tmp_path):
    dev = make_device(str(tmp_path / "noled"), leds=False)
    assert ctl(dict(env, TRIMUX_SYSFS_ROOT=dev), "leds", "detect", check=False).returncode == 2


def test_sysinfo(env):
    out = ctl(env, "sysinfo").stdout
    assert "model=TrimUI Brick Pro (TG4040)" in out
    assert "battery=76" in out and "firmware=1.1.1" in out and "temp_mc=48000" in out


def test_scan_writes_index_and_hides_m3u_discs(env, card):
    out = ctl(env, "scan").stdout
    assert "games=5" in out
    idx = read(os.path.join(card, "TriMuxData/cache/library.tsv"))
    assert "Roms/PS/Demo.m3u" in idx and "Disc 1).cue" not in idx
    assert "notarom" not in idx


def test_boot_counter_safe_mode(env):
    for _ in range(3):
        assert ctl(env, "boot", "begin", check=False).returncode == 0
    assert ctl(env, "boot", "begin", check=False).returncode == 10
    ctl(env, "boot", "ok")
    assert ctl(env, "boot", "begin", check=False).returncode == 0


def test_atari_folders_are_found(env, card):
    """Batocera-style lowercase folder for 2600 and stock-style A7800."""
    write(os.path.join(card, "Roms/atari2600/Adventure (Homebrew).a26"), "")
    write(os.path.join(card, "Roms/A7800/Homebrew.a78"), "")
    ctl(env, "scan")
    idx = read(os.path.join(card, "TriMuxData/cache/library.tsv"))
    assert "A2600\tRoms/atari2600/Adventure (Homebrew).a26\tAdventure" in idx
    assert "A7800\tRoms/A7800/Homebrew.a78" in idx


def net_log(device):
    p = os.path.join(device, "net.log")
    return read(p) if os.path.exists(p) else ""


def test_net_apply_stops_firmware_ssh_by_default(env, device):
    # init.d starts sshd on every boot; the stock MainUI stops it unless its
    # "Enable SSH" switch is on. TriMux replaces MainUI, so it does the same.
    write(os.path.join(device, "run/sshd"), "")
    ctl(env, "net", "apply")
    assert "sshd stop" in net_log(device)
    assert not os.path.exists(os.path.join(device, "run/sshd"))
    st = ctl(env, "net", "status").stdout
    assert "ssh_on=0" in st and "wifi_available=1" in st and "bluetooth_on=0" in st


def test_net_apply_leaves_wifi_alone_unless_chosen(env, device):
    ctl(env, "net", "apply")
    log = net_log(device)
    assert "wpa_supplicant" not in log and "ifconfig" not in log
    assert not os.path.exists(os.path.join(device, "run/trimui_btmanager"))   # bluetooth off by default


def test_net_apply_user_choices(env, device, card):
    write(os.path.join(card, "TriMuxData/config/trimux.ini"),
          "[network]\nwifi = on\nbluetooth = 1\nssh = 1\n")
    ctl(env, "net", "apply")
    log = net_log(device)
    # same command line as the firmware's /etc/init.d/wpa_supplicant (+ -B)
    assert ("wpa_supplicant -B -iwlan0 -Dnl80211 -c/etc/wifi/wpa_supplicant.conf "
            "-I/etc/wifi/wpa_supplicant_overlay.conf -O/etc/wifi/sockets") in log
    assert "busybox ifconfig wlan0 up" in log and "sshd start" in log
    for _ in range(50):   # detached processes
        if "busybox udhcpc -i wlan0" in net_log(device) and os.path.exists(os.path.join(device, "run/trimui_btmanager")):
            break
        time.sleep(0.1)
    assert "busybox udhcpc -i wlan0" in net_log(device)
    assert "btmanager cwd=%s/usr/trimui/bin ld=%s/usr/trimui/lib" % (device, device) in net_log(device)
    st = ctl(env, "net", "status").stdout
    assert "wifi_on=1" in st and "bluetooth_on=1" in st and "ssh_on=1" in st


def test_net_apply_wifi_off(env, device, card):
    write(os.path.join(device, "run/wpa_supplicant"), "")
    write(os.path.join(card, "TriMuxData/config/trimux.ini"), "[network]\nwifi = off\n")
    ctl(env, "net", "apply")
    log = net_log(device)
    assert "busybox ifconfig wlan0 down" in log and "busybox killall -15 wpa_supplicant" in log
    assert "busybox killall -9 udhcpc" in log


def test_net_without_wifi_hardware(env, tmp_path):
    bare = make_device(str(tmp_path / "bare"), net=False)
    st = ctl(dict(env, TRIMUX_SYSFS_ROOT=bare), "net", "status").stdout
    assert "wifi_available=0" in st and "bluetooth_available=0" in st and "ssh_available=0" in st
    ctl(dict(env, TRIMUX_SYSFS_ROOT=bare), "net", "apply")


GBA = "https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/"


def connect(device):
    write(os.path.join(device, "run/wpa_supplicant"), "")
    write(os.path.join(device, "netstate/status"), "wpa_state=COMPLETED\nssid=Casa\nip_address=10.0.0.5\n")


def curl_calls(device):
    return [l for l in net_log(device).splitlines() if l.startswith("curl ")]


def test_scrape_needs_wifi(env, device, card):
    r = ctl(env, "scrape", "--wait", "0", check=False)
    assert r.returncode == 2 and not curl_calls(device)
    assert "state=nowifi" in ctl(env, "scrape", "--status").stdout


def test_scrape_downloads_and_shrinks_covers(env, device, card):
    connect(device)
    write(os.path.join(device, "netstate/covers"), GBA + "Celeste%20Classic%20%28World%29.png\n")
    ctl(env, "scrape")
    cover = os.path.join(card, "Imgs/GBA/Celeste Classic (World).png")
    assert os.path.exists(cover)
    with open(cover, "rb") as f:
        head = f.read(24)
    import struct
    w, h = struct.unpack(">II", head[16:24])
    assert (w, h) == (320, 480)                      # 600x900 shrunk to fit 480x480
    st = ctl(env, "scrape", "--status").stdout
    assert "state=done" in st and "found=1" in st
    calls = curl_calls(device)
    assert any("Nintendo%20-%20Nintendo%20Entertainment%20System/Named_Boxarts/Micro%20Mages%20%28World%29.png" in c
               for c in calls)
    assert not any("notarom" in c for c in calls)    # not a game
    # games not found are remembered: a second run asks nothing new
    n = len(calls)
    ctl(env, "scrape")
    assert len(curl_calls(device)) == n
    # --retry asks again
    ctl(env, "scrape", "--retry")
    assert len(curl_calls(device)) > n
    assert os.path.exists(cover)


def test_scrape_arcade_uses_titles_and_box_kind(env, device, card):
    connect(device)
    write(os.path.join(card, "Roms/ARCADE/mslug.zip"), "")
    write(os.path.join(card, "TriMux/share/arcade-names.tsv"), "mslug\tMetal Slug - Super Vehicle-001\n")
    write(os.path.join(card, "TriMuxData/config/trimux.ini"), "[covers]\nkind = snap\n")
    url = "https://thumbnails.libretro.com/FBNeo%20-%20Arcade%20Games/Named_Snaps/Metal%20Slug%20-%20Super%20Vehicle-001.png"
    write(os.path.join(device, "netstate/covers"), url + "\n")
    ctl(env, "scrape")
    assert os.path.exists(os.path.join(card, "Imgs/ARCADE/mslug.png"))


def test_scrape_auto_respects_setting_and_network_errors(env, device, card):
    connect(device)
    ctl(env, "scrape", "--auto")
    assert not curl_calls(device)                    # automatic covers are off by default
    write(os.path.join(card, "TriMuxData/config/trimux.ini"), "[covers]\nauto = 1\n")
    write(os.path.join(device, "netstate/offline"), "")
    r = ctl(env, "scrape", "--auto", check=False)
    assert r.returncode == 3 and "state=network" in ctl(env, "scrape", "--status").stdout
    assert len(curl_calls(device)) == 3              # gives up after 3 network failures
    missing = os.path.join(card, "TriMuxData/cache/covers-missing.txt")
    assert not os.path.exists(missing) or read(missing) == ""   # network errors are not "not found"


def test_scrape_interrupted_by_a_restart_does_not_loop(env, device, card):
    """A restart in the middle of a download leaves TriMuxData/state/scrape_active:
    the next automatic run (boot) does not download again until the following
    boot; a manual run always does. The restart is simulated by creating the
    marker and clearing the RAM folder."""
    connect(device)
    write(os.path.join(card, "TriMuxData/config/trimux.ini"), "[covers]\nauto = 1\n")
    write(os.path.join(device, "netstate/covers"), GBA + "Celeste%20Classic%20%28World%29.png\n")
    marker = os.path.join(card, "TriMuxData/state/scrape_active")
    write(marker, "")
    r = ctl(env, "scrape", "--auto", check=False)
    assert r.returncode == 4 and not curl_calls(device) and not os.path.exists(marker)
    assert "state=interrupted" in ctl(env, "scrape", "--status").stdout
    assert "did not finish" in read(os.path.join(card, "TriMuxData/logs/trimux.log"))
    assert ctl(env, "scrape", "--auto", check=False).returncode == 4 and not curl_calls(device)
    # a manual run downloads, logs its progress and leaves no marker
    ctl(env, "scrape")
    assert os.path.exists(os.path.join(card, "Imgs/GBA/Celeste Classic (World).png"))
    assert not os.path.exists(marker)
    assert "scrape: progress" in read(os.path.join(card, "TriMuxData/logs/trimux.log"))
    # next boot (RAM folder cleared): automatic downloads are back
    shutil.rmtree(env["TRIMUX_TMP"])
    curl_n = len(curl_calls(device))
    ctl(env, "scrape", "--auto", "--retry")
    assert len(curl_calls(device)) > curl_n


def test_scrape_skips_platforms_turned_off(env, device, card):
    connect(device)
    write(os.path.join(card, "TriMuxData/config/trimux.ini"), "[covers]\nskip = FC, gb\n")
    ctl(env, "scrape")
    calls = curl_calls(device)
    assert calls and any("Game%20Boy%20Advance" in c for c in calls)
    assert not any("Nintendo%20Entertainment%20System" in c for c in calls)
    assert not any("Nintendo%20-%20Game%20Boy/" in c for c in calls)


def test_scrape_waits_for_the_wifi_after_a_suspend(env, device, card):
    """The Wi-Fi disappears during a download (the device was suspended):
    the scraper waits, turns the Wi-Fi back on, and continues when it is
    connected again instead of stopping."""
    import subprocess
    from conftest import CTL
    connect(device)
    write(os.path.join(device, "netstate/covers"), GBA + "Celeste%20Classic%20%28World%29.png\n")
    write(os.path.join(device, "netstate/drop"), "")
    proc = subprocess.Popen([CTL, "scrape"], env=env)
    log = os.path.join(card, "TriMuxData/logs/trimux.log")
    for _ in range(80):
        if os.path.exists(log) and "turning the Wi-Fi back on" in read(log):
            break
        time.sleep(0.25)
    assert "waiting for it to come back" in read(log)
    assert "state=waiting" in ctl(env, "scrape", "--status").stdout
    assert "wpa_supplicant -B" in net_log(device)          # turned back on, as it was when the download began
    write(os.path.join(device, "netstate/status"), "wpa_state=COMPLETED\nssid=Casa\nip_address=10.0.0.5\n")
    assert proc.wait(timeout=60) == 0
    assert os.path.exists(os.path.join(card, "Imgs/GBA/Celeste Classic (World).png"))
    assert "Wi-Fi back after" in read(log)
    st = ctl(env, "scrape", "--status").stdout
    assert "state=done" in st and "found=1" in st


def test_trimui_format_ports_are_found_and_start_from_their_folder(env, card):
    """Ports copied from the stock TrimUI card: one folder with config.json +
    launch.sh, in Roms/PORTS or in the stock Ports folder at the card root."""
    import json
    launch = '#!/bin/sh\npwd > started\n'
    for base, label in (("Roms/PORTS/Celeste", "Celeste"), ("Ports/Cave Story", "Cave Story (TrimUI)")):
        write(os.path.join(card, base, "config.json"), json.dumps({"label": label, "launch": "launch.sh", "icon": "icon.png"}))
        write(os.path.join(card, base, "launch.sh"), launch, 0o755)
        write(os.path.join(card, base, "gamedata/data.bin"), "x")
    write(os.path.join(card, "Roms/PORTS/Broken/config.json"), "{\"label\": \"No launch\"}")
    write(os.path.join(card, "Roms/PORTS/Broken/run.sh"), "#!/bin/sh\n")   # inside a folder without a valid config: ignored
    ctl(env, "scan")
    idx = read(os.path.join(card, "TriMuxData/cache/library.tsv"))
    assert "PORTS\tRoms/PORTS/Celeste/launch.sh\tCeleste" in idx
    assert "PORTS\tPorts/Cave Story/launch.sh\tCave Story (TrimUI)" in idx
    assert "Broken" not in idx and "data.bin" not in idx
    os.makedirs(env["TRIMUX_TMP"], exist_ok=True)
    write(os.path.join(env["TRIMUX_TMP"], "launch.ini"),
          "[launch]\nsystem = PORTS\nrom = Ports/Cave Story/launch.sh\nemulator = shell\n")
    ctl(env, "launch", check=False)
    assert os.path.realpath(read(os.path.join(card, "Ports/Cave Story/started"))) == \
        os.path.realpath(os.path.join(card, "Ports/Cave Story"))


def test_card_grow_auto_needs_the_image_marker(env):
    # cards prepared on a computer (no marker) are never touched automatically
    write(os.path.join(env["TRIMUX_SYSFS_ROOT"], "proc/mounts"), "/dev/mmcblk1p1 %s vfat rw 0 0\n" % env["TRIMUX_SDCARD"])
    r = ctl(env, "card-grow", "--auto", check=False)
    assert r.returncode == 1 and r.stderr == ""


def test_card_device_found_by_mount_name_or_sysfs(env):
    dev, sd = env["TRIMUX_SYSFS_ROOT"], env["TRIMUX_SDCARD"]
    # usual firmware mount: /dev/mmcblk1p1 -> disk /dev/mmcblk1 (cannot be opened here)
    write(os.path.join(dev, "proc/mounts"), "/dev/mmcblk1p1 %s vfat rw 0 0\n" % sd)
    r = ctl(env, "card-grow", "--dry-run", check=False)
    assert r.returncode == 1 and "/dev/mmcblk1" in r.stderr
    # another name for the same device: resolved through /sys/dev/block/<major>:<minor>
    st = os.stat(sd)
    blk = os.path.join(dev, "sys/devices/platform/sdc0/mmc_host/mmc1/mmc1:0001/block/mmcblk1")
    write(os.path.join(blk, "mmcblk1p1", "partition"), "1\n")
    os.makedirs(os.path.join(dev, "sys/dev/block"), exist_ok=True)
    os.symlink(os.path.join(blk, "mmcblk1p1"), os.path.join(dev, "sys/dev/block/%d:%d" % (os.major(st.st_dev), os.minor(st.st_dev))))
    write(os.path.join(dev, "proc/mounts"), "/dev/block/sdcard %s vfat rw 0 0\n" % sd)
    r = ctl(env, "card-grow", "--dry-run", check=False)
    assert r.returncode == 1 and "/dev/mmcblk1" in r.stderr
    # partition 2, or a whole-disk filesystem, is never touched
    write(os.path.join(blk, "mmcblk1p1", "partition"), "2\n")
    r = ctl(env, "card-grow", "--dry-run", check=False)
    assert r.returncode == 1 and "not a partitioned card" in r.stderr


def test_leds_keep_restores_after_firmware_override(env, device, card):
    import subprocess
    import time
    from conftest import CTL
    led = os.path.join(device, "sys/class/led_anim")
    write(os.path.join(card, "TriMuxData/config/trimux.ini"),
          "[leds]\nmanaged = 1\n[leds.m]\ncolor = 00FF00\nbrightness = 30\neffect = 4\n")
    sysjson = os.path.join(device, "mnt/UDISK/system.json")
    write(sysjson, '{"ledswitch": 0}\n')
    holder = subprocess.Popen(["sleep", "30"])
    keep = subprocess.Popen([CTL, "leds", "keep", str(holder.pid)], env=env)
    try:
        def wait_for(path, value, timeout=8):
            end = time.time() + timeout
            while time.time() < end:
                if read(path) == value:
                    return True
                time.sleep(0.2)
            return False
        assert wait_for(os.path.join(led, "effect_m"), "4")
        # keymon re-applies the stock settings after rewriting system.json
        write(os.path.join(led, "enable"), "0\n")
        write(os.path.join(led, "effect_m"), "0\n")
        write(os.path.join(led, "max_scale"), "5\n")
        os.utime(sysjson, (time.time() + 5, time.time() + 5))
        assert wait_for(os.path.join(led, "effect_m"), "4")
        assert wait_for(os.path.join(led, "enable"), "1")
        assert read(os.path.join(led, "max_scale")) == "30"
        assert read(os.path.join(led, "effect_rgb_hex_m")) == "00FF00"
        # the user turns every zone off: zones dark and the master switch off
        cfg = "[leds]\nmanaged = 1\nuser_off = 1\n"
        write(os.path.join(card, "TriMuxData/config/trimux.ini"), cfg)
        assert wait_for(os.path.join(led, "enable"), "0")
        assert read(os.path.join(led, "effect_m")) == "0"
        assert read(os.path.join(led, "effect_rgb_hex_m")) == "000000"
    finally:
        holder.kill()
        holder.wait()
    assert keep.wait(timeout=10) == 0      # stops with the process it watches


def test_leds_keep_leaves_firmware_alone_when_not_managed(env, device):
    import subprocess
    import time
    from conftest import CTL
    led = os.path.join(device, "sys/class/led_anim")
    write(os.path.join(led, "effect_m"), "6\n")             # e.g. the low-battery breathing
    holder = subprocess.Popen(["sleep", "4"])
    keep = subprocess.Popen([CTL, "leds", "keep", str(holder.pid)], env=env)
    holder.wait()
    assert keep.wait(timeout=10) == 0
    assert read(os.path.join(led, "effect_m")) == "6"
