"""Boot hook and supervisor scripts, run with stub firmware commands.

The scripts run under /bin/sh with fake `pgrep`, `tinymix`, `poweroff` and
`reboot`, a scripted menu binary and a fake RetroArch. This checks control
flow (stock fallback, crash loops, launch hand-off, power limits); it is not
a substitute for booting the real device."""
import os
import shutil
import subprocess
import time

import pytest

from conftest import CTL, POLICY, ROOT, read, write

SUPERVISOR = os.path.join(ROOT, "sdcard", "TriMux", "scripts", "supervisor.sh")


@pytest.fixture
def rig(tmp_path, env, card):
    stubs = tmp_path / "stubs"
    stubs.mkdir()
    for name, body in {
        "pgrep": "exit 0",                      # services already running
        "tinymix": "exit 0",
        "poweroff": 'echo poweroff >> "$RIG_LOG"; kill -TERM $PPID; exit 0',
        "reboot": 'echo reboot >> "$RIG_LOG"; kill -TERM $PPID; exit 0',
    }.items():
        write(str(stubs / name), "#!/bin/sh\n%s\n" % body, 0o755)
    tm = os.path.join(card, "TriMux")
    os.makedirs(os.path.join(tm, "bin"), exist_ok=True)
    os.makedirs(os.path.join(tm, "scripts"), exist_ok=True)
    shutil.copy(CTL, os.path.join(tm, "bin", "trimuxctl"))
    for s in ("supervisor.sh", "premenu.sh"):
        shutil.copy(os.path.join(ROOT, "sdcard", "TriMux", "scripts", s), os.path.join(tm, "scripts", s))
    # Fake RetroArch: records its arguments
    write(os.path.join(tm, "retroarch", "retroarch"),
          '#!/bin/sh\necho "$@" > "$RIG_DIR/ra_args"\ncat "$4" > "$RIG_DIR/ra_append"\n'
          'cat "%s/sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq" > "$RIG_DIR/ra_maxfreq"\nexit 0\n'
          % env["TRIMUX_SYSFS_ROOT"], 0o755)
    e = dict(env, TRIMUX_EXTRA_PATH=str(stubs), RIG_LOG=str(tmp_path / "rig.log"), RIG_DIR=str(tmp_path))
    return {"env": e, "card": card, "tm": tm, "tmp": env["TRIMUX_TMP"], "dir": str(tmp_path)}


def scripted_ui(rig, codes, pre=""):
    """Menu stub that exits with the next code from a list on every start."""
    seq = os.path.join(rig["dir"], "ui_seq")
    write(seq, "\n".join(str(c) for c in codes) + "\n")
    write(os.path.join(rig["tm"], "bin", "trimux-ui"),
          '#!/bin/sh\nn=$(head -1 "%s")\nsed -i 1d "%s"\n[ -z "$n" ] && n=20\n%s\nexit $n\n' % (seq, seq, pre), 0o755)


def run_supervisor(rig, timeout=40):
    return subprocess.run(["/bin/sh", os.path.join(rig["tm"], "scripts", "supervisor.sh")], env=rig["env"],
                          capture_output=True, text=True, timeout=timeout)


def test_stock_requested(rig):
    scripted_ui(rig, [20])
    r = run_supervisor(rig)
    assert r.returncode == 0
    assert "requested" in read(os.path.join(rig["tmp"], "to_stock"))


def test_not_brick_pro_goes_stock(rig, tmp_path):
    from conftest import make_device
    rig["env"]["TRIMUX_SYSFS_ROOT"] = make_device(str(tmp_path / "other"), brick_pro=False)
    scripted_ui(rig, [0])
    run_supervisor(rig)
    assert "not a TrimUI Brick Pro" in read(os.path.join(rig["tmp"], "to_stock"))


def test_crash_loop_falls_back_to_stock(rig):
    scripted_ui(rig, [139, 139, 139, 0])
    r = run_supervisor(rig, timeout=60)
    assert r.returncode == 0
    assert "crashed 3 times" in read(os.path.join(rig["tmp"], "to_stock"))


def test_safe_mode_after_failed_boots(rig):
    state = os.path.join(rig["card"], "TriMuxData", "state")
    write(os.path.join(state, "bootcount"), "3\n")
    scripted_ui(rig, [0])
    run_supervisor(rig)
    assert "safe mode" in read(os.path.join(rig["tmp"], "to_stock"))


def test_launch_flow_applies_limits_before_emulator(rig, env):
    os.makedirs(rig["tmp"], exist_ok=True)
    write(os.path.join(rig["tmp"], "launch.ini"),
          "[launch]\nsystem = GBA\nrom = Roms/GBA/Celeste Classic (World).gba\nemulator = gpsp\n")
    write(os.path.join(rig["card"], "TriMuxData/config/trimux.ini"), "[power]\nprofile = performance\n")
    scripted_ui(rig, [10, 20])
    run_supervisor(rig)
    args = read(os.path.join(rig["dir"], "ra_args"))
    assert "gpsp_libretro.so" in args and "Celeste Classic (World).gba" in args
    assert read(os.path.join(rig["dir"], "ra_maxfreq")) == "1800000"   # applied before start
    app = read(os.path.join(rig["dir"], "ra_append"))
    assert "/Saves/GBA" in app and "/States/GBA" in app and 'user_language = "7"' in app
    # back in the menu: default profile restored, crash marker removed
    assert read(os.path.join(env["TRIMUX_SYSFS_ROOT"], POLICY, "scaling_max_freq")) == "1608000"
    assert not os.path.exists(os.path.join(rig["card"], "TriMuxData/state/in_game"))
    assert os.path.exists(os.path.join(rig["card"], "Saves/GBA"))


def test_auto_profile_uses_emulator_recommendation(rig):
    os.makedirs(rig["tmp"], exist_ok=True)
    write(os.path.join(rig["tmp"], "launch.ini"),
          "[launch]\nsystem = FC\nrom = Roms/FC/Micro Mages (World).nes\nemulator = fceumm\n")
    scripted_ui(rig, [10, 20])
    run_supervisor(rig)
    assert read(os.path.join(rig["dir"], "ra_maxfreq")) == "1200000"   # fceumm -> economy


def test_tampered_launch_request_is_refused(rig):
    os.makedirs(rig["tmp"], exist_ok=True)
    write(os.path.join(rig["tmp"], "launch.ini"),
          "[launch]\nsystem = GBA\nrom = ../../etc/shadow.gba\nemulator = gpsp\n")
    scripted_ui(rig, [10, 20])
    run_supervisor(rig)
    assert not os.path.exists(os.path.join(rig["dir"], "ra_args"))
    log = read(os.path.join(rig["card"], "TriMuxData/logs/trimux.log"))
    assert "launch refused" in log


def test_poweroff(rig):
    scripted_ui(rig, [30])
    run_supervisor(rig)
    assert "poweroff" in read(rig["env"]["RIG_LOG"])


def entry(rig, name):
    return os.path.join(ROOT, "sdcard", "trimui", "app", name)


def test_preload_exit_codes(rig, tmp_path):
    # preload.sh uses absolute paths; run it with a fake root via sed substitution
    src = open(entry(rig, "preload.sh")).read()
    src = src.replace("/tmp/trimux", rig["tmp"]).replace("/mnt/SDCARD", rig["card"])
    p = str(tmp_path / "preload.sh")
    write(p, src, 0o755)
    assert subprocess.run(["/bin/sh", p]).returncode == 0
    write(os.path.join(rig["tmp"], "to_stock"), "x")
    assert subprocess.run(["/bin/sh", p]).returncode == 1


def test_mainui_restart_guard(rig, tmp_path):
    src = open(entry(rig, "MainUI")).read()
    src = src.replace("SD=/mnt/SDCARD", "SD=%s" % rig["card"]).replace("TMP=/tmp/trimux", "TMP=%s" % rig["tmp"])
    src = src.replace('exec /bin/sh "$SD/TriMux/scripts/supervisor.sh"', "exit 0")
    p = str(tmp_path / "MainUI")
    write(p, src, 0o755)
    for _ in range(2):
        subprocess.run(["/bin/sh", p], check=True)
    assert not os.path.exists(os.path.join(rig["tmp"], "to_stock"))
    subprocess.run(["/bin/sh", p], check=True)
    assert "restarted 3 times" in read(os.path.join(rig["tmp"], "to_stock"))


def test_scripts_are_posix_and_lf():
    for d in ("sdcard/trimui/app", "sdcard/TriMux/scripts"):
        for name in os.listdir(os.path.join(ROOT, d)):
            path = os.path.join(ROOT, d, name)
            data = open(path, "rb").read()
            assert b"\r\n" not in data, path
            assert data.startswith(b"#!/bin/sh"), path
            assert subprocess.run(["sh", "-n", path]).returncode == 0, path
            if shutil.which("shellcheck"):
                r = subprocess.run(["shellcheck", "-s", "sh", "-S", "error", path], capture_output=True, text=True)
                assert r.returncode == 0, r.stdout


def test_port_script_runs_from_its_folder_with_limits(rig, env):
    os.makedirs(rig["tmp"], exist_ok=True)
    write(os.path.join(rig["card"], "Roms/PORTS/My Port.sh"),
          '#!/bin/sh\npwd > "$RIG_DIR/port_cwd"\necho "$TRIMUX_DEVICE" > "$RIG_DIR/port_env"\n'
          'cat "%s/sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq" > "$RIG_DIR/port_freq"\n'
          % env["TRIMUX_SYSFS_ROOT"])
    write(os.path.join(rig["card"], "Roms/PORTS/myport/helper.sh"), "#!/bin/sh\n")
    write(os.path.join(rig["tmp"], "launch.ini"),
          "[launch]\nsystem = PORTS\nrom = Roms/PORTS/My Port.sh\nemulator = shell\n")
    scripted_ui(rig, [10, 20])
    run_supervisor(rig)
    assert read(os.path.join(rig["dir"], "port_cwd")).endswith("Roms/PORTS")
    assert read(os.path.join(rig["dir"], "port_env")) == "brickpro"
    assert read(os.path.join(rig["dir"], "port_freq")) == "1608000"      # shell -> balanced
    # helper scripts inside port folders are not listed as games
    from conftest import ctl
    ctl(rig["env"], "scan")
    idx = read(os.path.join(rig["card"], "TriMuxData/cache/library.tsv"))
    assert "Roms/PORTS/My Port.sh" in idx and "helper.sh" not in idx


def test_side_switch_economy_overrides_profile(rig, env):
    write(os.path.join(env["TRIMUX_SYSFS_ROOT"], "sys/class/gpio/gpio243/value"), "1\n")
    os.makedirs(rig["tmp"], exist_ok=True)
    write(os.path.join(rig["tmp"], "launch.ini"),
          "[launch]\nsystem = GBA\nrom = Roms/GBA/Celeste Classic (World).gba\nemulator = gpsp\n")
    write(os.path.join(rig["card"], "TriMuxData/config/trimux.ini"),
          "[power]\nprofile = performance\n[buttons]\nswitch = economy\n")
    scripted_ui(rig, [10, 20])
    run_supervisor(rig)
    assert read(os.path.join(rig["dir"], "ra_maxfreq")) == "1200000"


def test_cpu_limit_raised_by_firmware_shortcut_is_restored(rig, env):
    """Simulates keymon's FN 'CPU switcher' writing 2.0 GHz during a game."""
    maxf = os.path.join(env["TRIMUX_SYSFS_ROOT"], POLICY, "scaling_max_freq")
    write(os.path.join(rig["tm"], "retroarch", "retroarch"),
          '#!/bin/sh\necho 2000000 > "%s"\nsleep 12\ncat "%s" > "$RIG_DIR/after"\nexit 0\n' % (maxf, maxf), 0o755)
    os.makedirs(rig["tmp"], exist_ok=True)
    write(os.path.join(rig["tmp"], "launch.ini"),
          "[launch]\nsystem = FC\nrom = Roms/FC/Micro Mages (World).nes\nemulator = fceumm\n")
    scripted_ui(rig, [10, 20])
    run_supervisor(rig, timeout=60)
    assert read(os.path.join(rig["dir"], "after")) == "1200000"
    assert "limit raised externally" in read(os.path.join(rig["card"], "TriMuxData/logs/trimux.log"))


def boost_rig(rig, env, switch_value, ack="1"):
    write(os.path.join(env["TRIMUX_SYSFS_ROOT"], "sys/class/gpio/gpio243/value"), switch_value + "\n")
    os.makedirs(rig["tmp"], exist_ok=True)
    write(os.path.join(rig["tmp"], "launch.ini"),
          "[launch]\nsystem = GBA\nrom = Roms/GBA/Celeste Classic (World).gba\nemulator = mgba\n")
    write(os.path.join(rig["card"], "TriMuxData/config/trimux.ini"),
          "[power]\nprofile = performance\nboost_ack = %s\n[buttons]\nswitch = boost\n" % ack)
    scripted_ui(rig, [10, 20])
    run_supervisor(rig)
    return read(os.path.join(rig["dir"], "ra_maxfreq"))


def test_switch_boost_on_allows_2ghz(rig, env):
    assert boost_rig(rig, env, "1") == "2000000"
    log = read(os.path.join(rig["card"], "TriMuxData/logs/trimux.log"))
    assert "profile boost" in log


def test_switch_boost_off_keeps_1_8ghz_ceiling(rig, env):
    assert boost_rig(rig, env, "0") == "1800000"


def test_switch_boost_requires_confirmation(rig, env):
    assert boost_rig(rig, env, "1", ack="0") == "1800000"


def test_retroachievements_account_reaches_retroarch_only_when_enabled(rig):
    os.makedirs(rig["tmp"], exist_ok=True)
    req = "[launch]\nsystem = GBA\nrom = Roms/GBA/Celeste Classic (World).gba\nemulator = gpsp\n"
    cfg = os.path.join(rig["card"], "TriMuxData/config/trimux.ini")
    write(cfg, "[cheevos]\nenable = 0\nuser = jogador\npassword = segredo\n")
    write(os.path.join(rig["tmp"], "launch.ini"), req)
    scripted_ui(rig, [10, 20])
    run_supervisor(rig)
    assert "cheevos" not in read(os.path.join(rig["dir"], "ra_append"))
    write(cfg, "[cheevos]\nenable = 1\nuser = jogador\npassword = segredo\nhardcore = 1\n")
    write(os.path.join(rig["tmp"], "launch.ini"), req)
    scripted_ui(rig, [10, 20])
    run_supervisor(rig)
    app = read(os.path.join(rig["dir"], "ra_append"))
    assert 'cheevos_enable = "true"' in app and 'cheevos_username = "jogador"' in app
    assert 'cheevos_password = "segredo"' in app and 'cheevos_hardcore_mode_enable = "true"' in app
    assert "segredo" not in read(os.path.join(rig["card"], "TriMuxData/logs/trimux.log"))


def test_supervisor_applies_network_settings(rig, env):
    write(os.path.join(env["TRIMUX_SYSFS_ROOT"], "run/sshd"), "")
    scripted_ui(rig, [20])
    run_supervisor(rig)
    log = os.path.join(env["TRIMUX_SYSFS_ROOT"], "net.log")
    for _ in range(50):   # runs in the background
        if os.path.exists(log) and "sshd stop" in read(log):
            break
        time.sleep(0.1)
    assert "sshd stop" in read(log)
