"""trimuxctl against a simulated Brick Pro (see conftest.py)."""
import os

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
    ctl(env, "leds", "apply")
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
