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


def png_bytes(w, h, rgb=(200, 40, 40)):
    """A small valid PNG (no PIL needed)."""
    import struct
    import zlib
    raw = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def make_net_tools(root):
    """Fake firmware network tools. They log every call to <root>/net.log and
    keep "running" state as files in <root>/run (read by the fake pidof)."""
    run = os.path.join(root, "run")
    log = os.path.join(root, "net.log")
    os.makedirs(run, exist_ok=True)
    os.makedirs(os.path.join(root, "sys/class/net/wlan0"), exist_ok=True)
    write(os.path.join(root, "netstate/scan"),
          "bssid / frequency / signal level / flags / ssid\n"
          "aa:bb:cc:dd:ee:01\t2437\t-40\t[WPA2-PSK-CCMP][ESS]\tCasa\n"
          "aa:bb:cc:dd:ee:02\t2412\t-62\t[ESS]\tCafe\n")
    write(os.path.join(root, "netstate/networks"), "network id / ssid / bssid / flags\n")
    write(os.path.join(root, "usr/sbin/wpa_cli"), textwrap.dedent("""\
        #!/bin/sh
        S=%(root)s/netstate
        echo "wpa_cli $*" >> %(log)s
        [ -f %(run)s/wpa_supplicant ] || { echo "Failed to connect"; exit 255; }
        shift 4
        case "$1" in
        scan) echo OK ;;
        scan_result) cat $S/scan ;;
        list_network) cat $S/networks ;;
        add_network) echo 0 ;;
        set_network) [ "$3" = ssid ] && echo "$3" > $S/new_ssid; echo OK ;;
        select_network) printf 'network id / ssid / bssid / flags\\n%%s\\tCasa\\tany\\t[CURRENT]\\n' "$2" > $S/networks
                        printf 'wpa_state=COMPLETED\\nssid=Casa\\nip_address=127.0.0.1\\n' > $S/status; echo OK ;;
        status) cat $S/status 2>/dev/null || echo wpa_state=DISCONNECTED ;;
        *) echo OK ;;
        esac
        """) % {"root": root, "log": log, "run": run}, 0o755)
    write(os.path.join(root, "usr/sbin/wpa_supplicant"),
          '#!/bin/sh\necho "wpa_supplicant $*" >> %s\ntouch %s/wpa_supplicant\n' % (log, run), 0o755)
    write(os.path.join(root, "bin/busybox"), textwrap.dedent("""\
        #!/bin/sh
        echo "busybox $*" >> %(log)s
        case "$1" in
        pidof) [ -f %(run)s/"$2" ] ;;
        killall) rm -f %(run)s/"$3"; exit 0 ;;
        tcpsvd) while :; do sleep 1; done ;;
        tar) shift; exec tar "$@" ;;
        *) exit 0 ;;
        esac
        """) % {"log": log, "run": run}, 0o755)
    write(os.path.join(root, "usr/trimui/bin/trimui_btmanager"),
          '#!/bin/sh\necho "btmanager cwd=$(pwd) ld=$LD_LIBRARY_PATH" >> %s\ntouch %s/trimui_btmanager\n' % (log, run), 0o755)
    write(os.path.join(root, "usr/sbin/sshd"), "#!/bin/sh\n", 0o755)
    # fake curl: serves netstate/cover.png for URLs listed in netstate/covers,
    # files mapped in netstate/serve ("url<TAB>file" lines), 404 (exit 22) for
    # anything else, or a network error if netstate/offline exists
    write(os.path.join(root, "netstate/cover.png"), png_bytes(600, 900))
    write(os.path.join(root, "netstate/covers"), "")
    write(os.path.join(root, "netstate/serve"), "")
    write(os.path.join(root, "usr/bin/curl"), textwrap.dedent("""\
        #!/bin/sh
        S=%(root)s/netstate
        out=""; url=""
        while [ $# -gt 0 ]; do
            case "$1" in
            -o) out="$2"; shift 2 ;;
            https://*) url="$1"; shift ;;
            *) shift ;;
            esac
        done
        echo "curl $url" >> %(log)s
        [ -f $S/offline ] && exit 6
        if grep -qxF "$url" $S/covers; then cp $S/cover.png "$out"; exit 0; fi
        f=$(awk -F '\t' -v u="$url" '$1 == u { print $2 }' $S/serve)
        if [ -n "$f" ]; then cp "$f" "$out"; exit 0; fi
        exit 22
        """) % {"root": root, "log": log}, 0o755)
    write(os.path.join(root, "etc/init.d/sshd"),
          '#!/bin/sh\necho "sshd $1" >> %s\n[ "$1" = start ] && touch %s/sshd\n[ "$1" = stop ] && rm -f %s/sshd\nexit 0\n'
          % (log, run, run), 0o755)


def make_device(root, leds=True, cpufreq=True, brick_pro=True, net=True):
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
        for a in ("max_scale", "max_scale_lr", "max_scale_f1f2", "max_scale_rear", "effect_enable", "enable",
                  "anim_frames_enable"):
            write(os.path.join(root, "sys/class/led_anim", a), "0\n")
    write(os.path.join(root, "proc/meminfo"), "MemTotal: 1001012 kB\nMemAvailable: 702330 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n")
    write(os.path.join(root, "etc/version"), "1.1.1\n")
    write(os.path.join(root, "sys/class/gpio/gpio243/value"), "0\n")      # side switch
    write(os.path.join(root, "sys/class/speaker/mute"), "0\n")
    model = b"Trimui Brick Pro\x00" if brick_pro else b"Trimui Brick\x00"
    write(os.path.join(root, "usr/trimui/bin/MainUI"), b"\x7fELF....." + model + b"....")
    if net:
        make_net_tools(root)
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
