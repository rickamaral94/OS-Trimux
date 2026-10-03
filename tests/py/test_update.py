"""Online updater (trimuxctl update, trimui/app/MainUI rollback).

GitHub is simulated: the fake curl serves a releases JSON, the checksum file
and an update package built by scripts/make_update_zip.py from URLs mapped in
netstate/serve. This checks the updater's decisions and file operations, not
the real network, TLS or the device's SD card."""
import hashlib
import json
import os
import shutil
import subprocess
import sys

import pytest

from conftest import ROOT, ctl, read, write

API = "https://api.github.com/repos/rickamaral94/OS-Trimux/releases?per_page=15"
DL = "https://github.com/rickamaral94/OS-Trimux/releases/download"


def wifi_connected(env):
    dev = env["TRIMUX_SYSFS_ROOT"]
    write(os.path.join(dev, "run/wpa_supplicant"), "")
    write(os.path.join(dev, "netstate/status"), "wpa_state=COMPLETED\nssid=Casa\nip_address=127.0.0.1\n")


def build_package(tmp, version, inner_version=None):
    """A minimal update built with the real packaging script."""
    tree = os.path.join(tmp, "tree-" + version)
    v = inner_version or version
    for rel, body in {
        "TriMux/bin/trimuxctl": "new ctl %s\n" % v,
        "TriMux/bin/trimux-ui": "new ui %s\n" % v,
        "TriMux/scripts/supervisor.sh": "#!/bin/sh\n",
        "TriMux/share/systems.ini": "[x]\n",
        "TriMux/VERSION": v + "\n",
        "trimui/app/MainUI": "#!/bin/sh\n# new %s\n" % v,
        "trimui/app/preload.sh": "#!/bin/sh\n",
        "LEIA-ME.txt": "leia-me %s\n" % v,
        "Roms/LEIA-ME.txt": "roms\n",
        "Bios/prboom.wad": "wad\n",
    }.items():
        write(os.path.join(tree, rel), body, 0o755 if "/bin/" in rel or rel.endswith("MainUI") else None)
    for d in ("Roms/GBA", "Roms/PS"):
        os.makedirs(os.path.join(tree, d), exist_ok=True)
    out = os.path.join(tmp, "out-" + version)
    subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "make_update_zip.py"), "--tree", tree,
                    "--out", out, "--version", version], check=True, capture_output=True)
    return os.path.join(out, "TriMux-%s-update.tar.gz" % version)


def publish(env, tmp, version, prerelease=True, bad_hash=False, inner_version=None):
    """Serves a releases list with one release carrying the online-update assets."""
    dev = env["TRIMUX_SYSFS_ROOT"]
    pkg = build_package(tmp, version, inner_version)
    digest = hashlib.sha256(open(pkg, "rb").read()).hexdigest()
    if bad_hash:
        digest = "0" * 64
    sums = os.path.join(tmp, "sums-" + version)
    write(sums, "%s  TriMux-%s-brickpro.img.xz\n%s  TriMux-%s-update.tar.gz\n" % ("1" * 64, version, digest, version))
    tag = "v" + version
    releases = [{
        "tag_name": tag, "draft": False, "prerelease": prerelease,
        "body": "# TriMux %s\n\n**Novo:** atualizador online.\n" % version,
        "assets": [
            {"name": "TriMux-%s-update.tar.gz" % version, "size": os.path.getsize(pkg),
             "browser_download_url": "%s/%s/TriMux-%s-update.tar.gz" % (DL, tag, version)},
            {"name": "TriMux-%s-brickpro.sha256" % version, "size": 200,
             "browser_download_url": "%s/%s/TriMux-%s-brickpro.sha256" % (DL, tag, version)},
        ]}, {"tag_name": "v0.3.0", "draft": False, "prerelease": True, "body": "old", "assets": []}]
    rel = os.path.join(tmp, "releases.json")
    write(rel, json.dumps(releases))
    write(os.path.join(dev, "netstate/serve"),
          "%s\t%s\n%s/%s/TriMux-%s-update.tar.gz\t%s\n%s/%s/TriMux-%s-brickpro.sha256\t%s\n"
          % (API, rel, DL, tag, version, pkg, DL, tag, version, sums))


@pytest.fixture
def upd(env, card, tmp_path):
    write(os.path.join(card, "TriMux", "VERSION"), "0.3.0\n")
    for rel in ("TriMux/bin/trimuxctl", "TriMux/bin/trimux-ui"):
        write(os.path.join(card, rel), "old\n", 0o755)
    write(os.path.join(card, "trimui/app/MainUI"), "#!/bin/sh\n# old\n", 0o755)
    write(os.path.join(card, "Saves/GBA/game.srm"), "save")
    wifi_connected(env)
    return env


def status(env):
    return dict(line.split("=", 1) for line in read(os.path.join(env["TRIMUX_TMP"], "update.status")).splitlines())


def sd(env, *rel):
    return os.path.join(env["TRIMUX_SDCARD"], *rel)


def net_log(env):
    p = os.path.join(env["TRIMUX_SYSFS_ROOT"], "net.log")
    return open(p).read() if os.path.exists(p) else ""


def test_check_finds_newer_release(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0")
    r = ctl(upd, "update", "check", check=False)
    assert r.returncode == 2 and "available=0.9.0" in r.stdout
    st = status(upd)
    assert st["state"] == "available" and st["version"] == "0.9.0"
    notes = read(os.path.join(upd["TRIMUX_TMP"], "update-notes.txt"))
    assert "Novo: atualizador online." in notes and "**" not in notes


def test_check_respects_prerelease_setting(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0", prerelease=True)
    write(sd(upd, "TriMuxData/config/trimux.ini"), "[update]\nprerelease = 0\n")
    r = ctl(upd, "update", "check", check=False)
    assert r.returncode == 0 and status(upd)["state"] == "uptodate"


def test_auto_check_only_when_enabled(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0")
    assert ctl(upd, "update", "check", "--auto").returncode == 0
    assert "api.github.com" not in net_log(upd)
    write(sd(upd, "TriMuxData/config/trimux.ini"), "[update]\nauto_check = 1\n")
    assert ctl(upd, "update", "check", "--auto", check=False).returncode == 2


def test_check_errors(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0")
    write(os.path.join(upd["TRIMUX_SYSFS_ROOT"], "netstate/offline"), "")
    assert ctl(upd, "update", "check", check=False).returncode == 1
    assert status(upd)["error"] == "network"
    os.remove(os.path.join(upd["TRIMUX_SYSFS_ROOT"], "run/wpa_supplicant"))
    assert ctl(upd, "update", "check", check=False).returncode == 1
    assert status(upd)["error"] == "nowifi"


def test_install_swaps_folders_and_keeps_previous(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0")
    ctl(upd, "update", "check", check=False)
    ctl(upd, "update", "install")
    assert read(sd(upd, "TriMux/VERSION")) == "0.9.0"
    assert read(sd(upd, "TriMux/bin/trimuxctl")) == "new ctl 0.9.0"
    assert "new 0.9.0" in read(sd(upd, "trimui/app/MainUI"))
    assert read(sd(upd, "LEIA-ME.txt")) == "leia-me 0.9.0"
    assert os.access(sd(upd, "TriMux/bin/trimux-ui"), os.X_OK)
    # previous version kept, user data untouched, staging removed
    assert read(sd(upd, "TriMux.old/VERSION")) == "0.3.0"
    assert "old" in read(sd(upd, "trimui.old/app/MainUI"))
    assert read(sd(upd, "Saves/GBA/game.srm")) == "save"
    assert os.path.exists(sd(upd, "Roms/GBA/Celeste Classic (World).gba"))
    assert not os.path.exists(sd(upd, ".trimux-new"))
    assert read(sd(upd, "TriMuxData/state/update_pending")) == "0.3.0"
    st = status(upd)
    assert st["state"] == "ready" and st["percent"] == "100"
    assert not os.path.exists(os.path.join(upd["TRIMUX_TMP"], "update.tar.gz"))


def test_checksum_mismatch_changes_nothing(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0", bad_hash=True)
    ctl(upd, "update", "check", check=False)
    assert ctl(upd, "update", "install", check=False).returncode == 1
    assert status(upd)["error"] == "checksum"
    assert read(sd(upd, "TriMux/VERSION")) == "0.3.0"
    assert not os.path.exists(sd(upd, "TriMux.old")) and not os.path.exists(sd(upd, ".trimux-new"))
    assert not os.path.exists(sd(upd, "TriMuxData/state/update_pending"))


def test_package_of_another_version_is_refused(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0", inner_version="0.8.0")
    ctl(upd, "update", "check", check=False)
    assert ctl(upd, "update", "install", check=False).returncode == 1
    assert status(upd)["error"] == "package"
    assert read(sd(upd, "TriMux/VERSION")) == "0.3.0"
    assert not os.path.exists(sd(upd, ".trimux-new"))


def test_low_battery_refuses_before_download(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0")
    ctl(upd, "update", "check", check=False)
    write(os.path.join(upd["TRIMUX_SYSFS_ROOT"], "sys/class/power_supply/axp2202-battery/capacity"), "20\n")
    assert ctl(upd, "update", "install", check=False).returncode == 1
    assert status(upd)["error"] == "battery"
    assert "update.tar.gz" not in net_log(upd)
    assert read(sd(upd, "TriMux/VERSION")) == "0.3.0"


def test_rollback_and_confirm(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0")
    ctl(upd, "update", "check", check=False)
    ctl(upd, "update", "install")
    pending = sd(upd, "TriMuxData/state/update_pending")
    # the menu still running from before the reboot proves nothing
    ctl(upd, "boot", "ok")
    assert os.path.exists(pending)
    # started by the new MainUI (tries >= 1): confirmed
    write(sd(upd, "TriMuxData/state/update_tries"), "1\n")
    ctl(upd, "boot", "ok")
    assert not os.path.exists(pending)
    assert "backup=1" in ctl(upd, "update", "status").stdout
    ctl(upd, "update", "rollback")
    assert read(sd(upd, "TriMux/VERSION")) == "0.3.0"
    assert read(sd(upd, "TriMux.old/VERSION")) == "0.9.0"
    assert "old" in read(sd(upd, "trimui/app/MainUI"))
    assert not os.path.exists(sd(upd, "TriMux.swap"))


def mainui_script(env, tmp_path):
    src = open(os.path.join(ROOT, "sdcard", "trimui", "app", "MainUI")).read()
    src = src.replace("SD=/mnt/SDCARD", "SD=%s" % env["TRIMUX_SDCARD"])
    src = src.replace("TMP=/tmp/trimux", "TMP=%s" % env["TRIMUX_TMP"])
    src = src.replace('exec /bin/sh "$SD/TriMux/scripts/supervisor.sh"', "exit 0")
    p = str(tmp_path / "MainUI.test")
    write(p, src, 0o755)
    return p


def test_mainui_restores_previous_version_after_failed_starts(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0")
    ctl(upd, "update", "check", check=False)
    ctl(upd, "update", "install")
    p = mainui_script(upd, tmp_path)
    for n in range(1, 4):   # three starts that never reach the menu
        for f in ("entry_count", "entry_first"):   # separate boots for the restart guard
            if os.path.exists(os.path.join(upd["TRIMUX_TMP"], f)):
                os.remove(os.path.join(upd["TRIMUX_TMP"], f))
        subprocess.run(["/bin/sh", p], check=True)
        assert read(sd(upd, "TriMuxData/state/update_tries")) == str(n)
        assert read(sd(upd, "TriMux/VERSION")) == "0.9.0"
    os.remove(os.path.join(upd["TRIMUX_TMP"], "entry_count"))
    subprocess.run(["/bin/sh", p], check=True)
    assert read(sd(upd, "TriMux/VERSION")) == "0.3.0"
    assert "old" in read(sd(upd, "trimui/app/MainUI"))
    assert read(sd(upd, "TriMux.bad/VERSION")) == "0.9.0"
    assert not os.path.exists(sd(upd, "TriMuxData/state/update_pending"))
    assert "previous version 0.3.0 restored" in read(sd(upd, "TriMuxData/logs/trimux.log"))
    assert read(sd(upd, "Saves/GBA/game.srm")) == "save"


def test_mainui_ignores_normal_boots(upd, tmp_path):
    p = mainui_script(upd, tmp_path)
    subprocess.run(["/bin/sh", p], check=True)
    assert not os.path.exists(sd(upd, "TriMuxData/state/update_tries"))
    assert read(sd(upd, "TriMux/VERSION")) == "0.3.0"


def test_update_tarball_is_reproducible(tmp_path):
    a = build_package(str(tmp_path / "a"), "0.9.0")
    b = build_package(str(tmp_path / "b"), "0.9.0")
    assert open(a, "rb").read() == open(b, "rb").read()
    out = subprocess.run(["tar", "-tzvf", a], capture_output=True, text=True, check=True).stdout
    assert "TriMux/bin/trimuxctl" in out and "trimui/app/MainUI" in out and "LEIA-ME.txt" in out
    assert "Roms" not in out and "Bios" not in out and "TriMuxData" not in out


def test_update_zip_brings_platform_folders(tmp_path):
    import zipfile
    build_package(str(tmp_path), "0.9.0")
    names = zipfile.ZipFile(str(tmp_path / "out-0.9.0" / "TriMux-0.9.0-update.zip")).namelist()
    assert "Roms/GBA/" in names and "Roms/PS/" in names        # empty folders kept
    assert "Roms/LEIA-ME.txt" in names and "Bios/prboom.wad" in names
    assert "TriMux/VERSION" in names and not any(n.startswith("TriMuxData") for n in names)


# ---- menu (SDL offscreen driver, scripted buttons) ----

from test_ui import _ui_runs, ui  # noqa: E402

needs_ui = pytest.mark.skipif(not _ui_runs(), reason="build/native/trimux-ui not built for this system")
UPDATE = "UP,A" + ",DOWN" * 10 + ",A" + ",DOWN" * 4 + ",A"   # Configurações -> Sistema -> Atualização


def real_ctl(env):
    from conftest import CTL
    shutil.copy(CTL, sd(env, "TriMux/bin/trimuxctl"))


@needs_ui
def test_menu_check_install_and_reboot(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0")
    real_ctl(upd)
    # "Procurar atualização" (row 3: versão, situação, procurar)
    assert ui(upd, "wait=50," + UPDATE + ",DOWN,DOWN,A,wait=1500,B,B,B").returncode == 0
    assert status(upd)["state"] == "available"
    # Instalar (now row 3) -> confirmation defaults to "No"
    assert ui(upd, "wait=50," + UPDATE + ",DOWN,DOWN,A,A,wait=300,B,B,B").returncode == 0
    assert read(sd(upd, "TriMux/VERSION")) == "0.3.0"
    shot = str(tmp_path / "update.bmp")
    r = ui(upd, "wait=50," + UPDATE + ",shot=%s,DOWN,DOWN,A,LEFT,A,wait=1500,wait=1500,LEFT,A" % shot)
    assert os.path.getsize(shot) > 100000
    assert read(sd(upd, "TriMux/VERSION")) == "0.9.0"
    assert r.returncode == 31   # "installed, reboot now?" -> yes


@needs_ui
def test_menu_blocks_games_until_reboot(upd, tmp_path):
    os.makedirs(upd["TRIMUX_TMP"], exist_ok=True)
    write(os.path.join(upd["TRIMUX_TMP"], "update.status"), "state=ready\nversion=0.9.0\nerror=\npercent=100\n")
    r = ui(upd, "DOWN,A,A,A")   # Todos os jogos -> Celeste -> play, dialog dismissed
    assert r.returncode == 0
    assert not os.path.exists(os.path.join(upd["TRIMUX_TMP"], "launch.ini"))


@needs_ui
def test_menu_options_and_rollback(upd, tmp_path):
    assert ui(upd, UPDATE + ",DOWN,DOWN,DOWN,A,DOWN,A,B,B,B").returncode == 0
    cfg = read(sd(upd, "TriMuxData/config/trimux.ini"))
    assert "[update]" in cfg and "prerelease = 0" in cfg and "auto_check = 1" in cfg
    # with a previous version kept: "Voltar à versão anterior" -> yes -> reboot
    shutil.copytree(sd(upd, "TriMux"), sd(upd, "TriMux.old"))
    write(sd(upd, "TriMux.old/VERSION"), "0.2.0\n")
    shutil.copytree(sd(upd, "trimui"), sd(upd, "trimui.old"))
    r = ui(upd, UPDATE + ",DOWN,DOWN,DOWN,DOWN,DOWN,A,LEFT,A")
    assert r.returncode == 31
    assert read(sd(upd, "TriMux/VERSION")) == "0.2.0"
    assert read(sd(upd, "TriMux.old/VERSION")) == "0.3.0"


def test_mainui_recovers_from_interrupted_swap(upd, tmp_path):
    os.rename(sd(upd, "TriMux"), sd(upd, "TriMux.old"))   # power lost between the two renames
    subprocess.run(["/bin/sh", mainui_script(upd, tmp_path)], check=True)
    assert read(sd(upd, "TriMux/VERSION")) == "0.3.0"
    assert not os.path.exists(sd(upd, "TriMux.old"))


def test_failed_download_reports_network(upd, tmp_path):
    publish(upd, str(tmp_path), "0.9.0")
    ctl(upd, "update", "check", check=False)
    serve = os.path.join(upd["TRIMUX_SYSFS_ROOT"], "netstate/serve")
    lines = [l for l in open(serve).read().splitlines() if "update.tar.gz" not in l]
    write(serve, "\n".join(lines) + "\n")   # the package URL now answers 404
    assert ctl(upd, "update", "install", check=False).returncode == 1
    assert status(upd)["error"] == "network"
    assert read(sd(upd, "TriMux/VERSION")) == "0.3.0"


def test_covers_saves_and_games_survive_update_and_rollback(upd, tmp_path):
    keep = {
        "Imgs/GBA/Celeste Classic (World).png": b"\x89PNG cover",
        "Saves/GBA/Celeste Classic (World).srm": b"save",
        "States/GBA/Celeste Classic (World).state1": b"state",
        "TriMuxData/cache/covers-missing.txt": b"Roms/GBA/x.gba\n",
        "Bios/gba_bios.bin": b"bios",
    }
    for rel, data in keep.items():
        write(sd(upd, rel), data)
    publish(upd, str(tmp_path), "0.9.0")
    ctl(upd, "update", "check", check=False)
    ctl(upd, "update", "install")
    for rel, data in keep.items():
        assert open(sd(upd, rel), "rb").read() == data, rel
    ctl(upd, "update", "rollback")
    for rel, data in keep.items():
        assert open(sd(upd, rel), "rb").read() == data, rel
