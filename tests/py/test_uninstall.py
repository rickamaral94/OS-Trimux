"""Uninstall (Configurações › Sistema › Desinstalar o TriMux), on a simulated
card and firmware. The stock interface itself is not run here: the tests
check what it would find (stock emulators in Emus/ and the Roms folders they
read) and that only TriMux's own folders leave the card. Not a hardware test."""
import json
import os
import subprocess

import pytest

from conftest import ctl, read, write
from test_supervisor import rig, run_supervisor, scripted_ui  # noqa: F401  (fixture)
from test_ui import _ui_runs, ui

needs_ui = pytest.mark.skipif(not _ui_runs(), reason="build/native/trimux-ui not built for this system")


def stock_emu(root, name, rompath):
    write(os.path.join(root, name, "config.json"),
          json.dumps({"label": name, "launch": "launch.sh", "rompath": rompath, "extlist": "zip"}))
    write(os.path.join(root, name, "launch.sh"), "#!/bin/sh\n", 0o755)


def check(env):
    out = ctl(env, "uninstall", "check").stdout
    return dict(l.split("=", 1) for l in out.splitlines())


def system_folders(card):
    for d in ("trimui/app", "TriMux.old/bin", "trimui.old/app"):
        write(os.path.join(card, d, "x"), "x")


def test_check_without_stock_emulators(env, card):
    c = check(env)
    assert c["stock_emulators"] == "0"
    # Roms with games: GBA, FC, GB, PS, ARCADE (notarom.txt counts as a file)
    assert c["game_folders"] == "5" and set(c["not_read"].split(",")) == {"GBA", "FC", "GB", "PS", "ARCADE"}


def test_check_reads_stock_rompaths(env, card):
    emus = os.path.join(card, "Emus")
    stock_emu(emus, "FC", "../../Roms/FC")
    write(os.path.join(emus, "GBA", "config.json"),                       # escaped slashes, as some JSON writers do
          '{"label":"GBA","launch":"launch.sh","rompath":"..\\/..\\/Roms\\/GBA"}')
    stock_emu(emus, "GB", "../../Roms/gb")                                # case differs: FAT ignores it
    write(os.path.join(emus, "tg5040", "PORTS.pak", "launch.sh"), "")     # PortMaster: not a stock emulator
    stock_emu(os.path.join(env["TRIMUX_SYSFS_ROOT"], "mnt/UDISK/Emus"), "PS", "/mnt/SDCARD/Roms/PS")
    stock_emu(emus, "BAD", "../../../etc")                                # outside the card: ignored
    write(os.path.join(card, "Roms/N64/COPYING"), "license only")         # no games: not counted
    c = check(env)
    assert c["stock_emulators"] == "5"
    assert set(c["stock_reads"].split(",")) == {"FC", "GBA", "gb", "PS"}
    assert c["game_folders"] == "5" and c["not_read"] == "ARCADE"


def test_run_needs_the_menu_request(env, card):
    system_folders(card)
    r = ctl(env, "uninstall", "run", check=False)
    assert r.returncode == 2
    assert os.path.isdir(os.path.join(card, "TriMux")) and os.path.isdir(os.path.join(card, "trimui"))


def test_run_removes_only_trimux_folders(env, card):
    system_folders(card)
    write(os.path.join(card, "Saves/GBA/Celeste Classic (World).srm"), "save")
    write(os.path.join(card, "Imgs/GBA/Celeste Classic (World).png"), "png")
    write(os.path.join(card, "Bios/scph5501.bin"), "bios")
    write(os.path.join(card, "Apps/Grout/launch.sh"), "")
    stock_emu(os.path.join(card, "Emus"), "FC", "../../Roms/FC")
    write(os.path.join(card, "TriMuxData/config/trimux.ini"), "[general]\nwizard_done = 1\n")
    os.makedirs(env["TRIMUX_TMP"], exist_ok=True)
    write(os.path.join(env["TRIMUX_TMP"], "uninstall.req"), "1\n")
    before = {d: sorted(os.listdir(os.path.join(card, d))) for d in ("Roms", "Saves", "Imgs", "Bios", "Apps", "Emus")}
    ctl(env, "uninstall", "run")
    for d in ("trimui", "TriMux", "TriMux.old", "trimui.old"):
        assert not os.path.exists(os.path.join(card, d)), d
    for d, names in before.items():
        assert sorted(os.listdir(os.path.join(card, d))) == names, d
    assert read(os.path.join(card, "Saves/GBA/Celeste Classic (World).srm")) == "save"
    assert os.path.exists(os.path.join(card, "TriMuxData/config/trimux.ini"))
    assert not os.path.exists(os.path.join(env["TRIMUX_TMP"], "uninstall.req"))     # one request, one run
    log = read(os.path.join(card, "TriMuxData/logs/trimux.log"))
    assert "uninstall: removed trimui" in log and "uninstall: removed TriMux" in log


def test_run_leaves_a_file_named_like_a_trimux_folder(env, card):
    write(os.path.join(card, "TriMux.old"), "not ours: a file, not the folder")
    os.makedirs(env["TRIMUX_TMP"], exist_ok=True)
    write(os.path.join(env["TRIMUX_TMP"], "uninstall.req"), "1\n")
    assert ctl(env, "uninstall", "run", check=False).returncode == 1
    assert read(os.path.join(card, "TriMux.old")).startswith("not ours")
    assert not os.path.exists(os.path.join(card, "TriMux"))


def test_supervisor_uninstalls_and_opens_stock(rig):  # noqa: F811
    card = rig["card"]
    write(os.path.join(card, "trimui/app/preload.sh"), "#!/bin/sh\nexit 0\n", 0o755)
    write(os.path.join(card, "Roms/GBA/keep.gba"), "game")
    os.makedirs(rig["tmp"], exist_ok=True)
    scripted_ui(rig, [21], pre='echo 1 > "%s/uninstall.req"' % rig["tmp"])
    r = run_supervisor(rig)
    assert r.returncode == 0
    assert read(os.path.join(rig["tmp"], "to_stock")).strip() == "uninstalled"
    assert not os.path.exists(os.path.join(card, "trimui")) and not os.path.exists(os.path.join(card, "TriMux"))
    assert read(os.path.join(card, "Roms/GBA/keep.gba")) == "game"
    assert "uninstalled: opening the stock interface" in read(os.path.join(card, "TriMuxData/logs/trimux.log"))


def test_supervisor_without_request_keeps_trimux(rig):  # noqa: F811
    card = rig["card"]
    write(os.path.join(card, "trimui/app/preload.sh"), "#!/bin/sh\nexit 0\n", 0o755)
    os.makedirs(rig["tmp"], exist_ok=True)
    scripted_ui(rig, [21, 20])                     # exit code without the menu's request file
    run_supervisor(rig)
    assert os.path.isdir(os.path.join(card, "TriMux")) and os.path.isdir(os.path.join(card, "trimui"))
    assert "uninstall failed" in read(os.path.join(card, "TriMuxData/logs/trimux.log"))


# Configurações -> Sistema (DOWN*10) -> last item "Desinstalar o TriMux" (UP from the first row)
UNINSTALL = "UP,A" + ",DOWN" * 10 + ",A,UP,A"


@needs_ui
def test_uninstall_page_reports_and_asks(env, card, tmp_path):
    stock_emu(os.path.join(card, "Emus"), "FC", "../../Roms/FC")
    shot = str(tmp_path / "uninstall.bmp")
    # the page, then "Desinstalar agora" (last row) -> dialog with "Não" selected -> A keeps everything
    r = ui(env, UNINSTALL + ",shot=%s,UP,A,A,B,B,B,B" % shot)
    assert r.returncode == 0
    assert not os.path.exists(os.path.join(env["TRIMUX_TMP"], "uninstall.req"))
    assert os.path.getsize(shot) > 100000
    # LEFT moves to "Sim": the request is written and the menu exits with 21
    r = ui(env, UNINSTALL + ",UP,A,LEFT,A")
    assert r.returncode == 21
    assert os.path.exists(os.path.join(env["TRIMUX_TMP"], "uninstall.req"))
    assert "ui: uninstall confirmed (1 stock emulators, 1 of 5 game folders read)" in \
        read(os.path.join(card, "TriMuxData/logs/trimux.log"))
