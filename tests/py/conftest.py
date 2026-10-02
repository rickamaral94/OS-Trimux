"""Shared fixtures: a simulated Brick Pro (sysfs/proc/firmware files) and a
simulated card. Values mirror what firmware v1.1.1 exposes (OPP table from its
device tree, axp2202 battery, led_anim zones written by runtrimui.sh), but this
is a SIMULATION: it validates TriMux logic, not the hardware."""
import os
import shutil
import stat
import subprocess
import textwrap

import pytest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NATIVE = os.path.join(ROOT, "build", "native")
CTL = os.path.join(NATIVE, "trimuxctl")
UI = os.path.join(NATIVE, "trimux-ui")
POLICY = "sys/devices/system/cpu/cpufreq/policy0"
# OPP list of firmware v1.1.1 (sun50iw10 opp_l_table, bin c0)
FREQS = "408000 600000 816000 1008000 1200000 1320000 1416000 1608000 1800000 2000000"


def write(path, content, mode=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w" if isinstance(content, str) else "wb") as f:
        f.write(content)
    if mode:
        os.chmod(path, mode)


def make_device(root, leds=True, cpufreq=True, brick_pro=True):
    if cpufreq:
        for name, val in {
            "scaling_available_frequencies": FREQS + " \n",
            "scaling_available_governors": "interactive conservative ondemand userspace powersave performance schedutil\n",
            "cpuinfo_min_freq": "408000\n", "cpuinfo_max_freq": "2000000\n",
            "scaling_min_freq": "408000\n", "scaling_max_freq": "2000000\n",
            "scaling_cur_freq": "1008000\n", "scaling_governor": "ondemand\n",
        }.items():
            write(os.path.join(root, POLICY, name), val)
    write(os.path.join(root, "sys/class/thermal/thermal_zone0/type"), "cpu_thermal_zone\n")
    write(os.path.join(root, "sys/class/thermal/thermal_zone0/temp"), "48000\n")
    write(os.path.join(root, "sys/class/power_supply/axp2202-battery/type"), "Battery\n")
    write(os.path.join(root, "sys/class/power_supply/axp2202-battery/capacity"), "76\n")
    write(os.path.join(root, "sys/class/power_supply/axp2202-battery/status"), "Discharging\n")
    write(os.path.join(root, "sys/class/power_supply/axp2202-usb/type"), "USB\n")
    if leds:
        for z in ("m", "lr", "f1", "f2", "rear"):
            for a in ("effect_%s", "effect_rgb_hex_%s", "effect_duration_%s", "effect_cycles_%s"):
                write(os.path.join(root, "sys/class/led_anim", a % z), "0\n")
        for a in ("max_scale", "max_scale_lr", "max_scale_f1f2", "max_scale_rear", "effect_enable"):
            write(os.path.join(root, "sys/class/led_anim", a), "0\n")
    write(os.path.join(root, "proc/meminfo"), "MemTotal: 1001012 kB\nMemAvailable: 702330 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n")
    write(os.path.join(root, "etc/version"), "1.1.1\n")
    model = b"Trimui Brick Pro\x00" if brick_pro else b"Trimui Brick\x00"
    write(os.path.join(root, "usr/trimui/bin/MainUI"), b"\x7fELF....." + model + b"....")
    return root


def make_card(sd):
    """Card tree with TriMux share files and fake (empty) cores."""
    share = os.path.join(sd, "TriMux", "share")
    shutil.copytree(os.path.join(ROOT, "sdcard", "TriMux", "share"), share)
    font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    os.makedirs(os.path.join(share, "fonts"), exist_ok=True)
    if os.path.exists(font):
        shutil.copy(font, os.path.join(share, "fonts"))
    os.makedirs(os.path.join(sd, "TriMux", "retroarch"), exist_ok=True)
    shutil.copy(os.path.join(ROOT, "sdcard", "TriMux", "retroarch", "retroarch.base.cfg"),
                os.path.join(sd, "TriMux", "retroarch", "retroarch.base.cfg"))
    cores = os.path.join(sd, "TriMux", "retroarch", "cores")
    os.makedirs(cores, exist_ok=True)
    for core in ("fceumm", "nestopia", "gambatte", "mgba", "gpsp", "pcsx_rearmed", "snes9x2005_plus",
                 "mednafen_supafaust", "picodrive", "genesis_plus_gx", "fbneo"):
        write(os.path.join(cores, core + "_libretro.so"), "fake core\n")
    write(os.path.join(sd, "TriMux", "VERSION"), "test\n")
    for rel in ("Roms/GBA/Celeste Classic (World).gba", "Roms/GBA/Goodboy Galaxy (Demo).gba",
                "Roms/FC/Micro Mages (World).nes", "Roms/GB/Tobu Tobu Girl (World).gb",
                "Roms/PS/Demo (Disc 1).cue", "Roms/PS/Demo (Disc 2).cue",
                "Roms/ARCADE/notarom.txt"):
        write(os.path.join(sd, rel), "")
    write(os.path.join(sd, "Roms/PS/Demo.m3u"), "Demo (Disc 1).cue\nDemo (Disc 2).cue\n")
    return sd


@pytest.fixture
def device(tmp_path):
    return make_device(str(tmp_path / "device"))


@pytest.fixture
def card(tmp_path):
    return make_card(str(tmp_path / "sd"))


@pytest.fixture
def env(tmp_path, device, card):
    e = dict(os.environ)
    e.update(TRIMUX_SYSFS_ROOT=device, TRIMUX_SDCARD=card, TRIMUX_TMP=str(tmp_path / "tmp"))
    e.pop("TRIMUX_LOG_STDERR", None)
    return e


def ctl(env, *args, check=True):
    r = subprocess.run([CTL, *args], env=env, capture_output=True, text=True, timeout=60)
    if check and r.returncode != 0:
        raise AssertionError("trimuxctl %s failed (%d): %s %s" % (args, r.returncode, r.stdout, r.stderr))
    return r


def read(path):
    with open(path) as f:
        return f.read().strip()


def have(*tools):
    return all(shutil.which(t) for t in tools)
