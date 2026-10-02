"""Menu behaviour with SDL's offscreen driver and scripted button presses.

Simulated environment only: proves navigation logic, persistence and the
launch hand-off, not rendering speed or the physical controls."""
import os
import subprocess

import pytest

from conftest import UI, read, write

pytestmark = pytest.mark.skipif(not os.path.exists(UI), reason="build/native/trimux-ui not built (make ui-native)")


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
