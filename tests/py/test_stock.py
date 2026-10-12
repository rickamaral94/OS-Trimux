"""Sistema oficial (Configurações › Sistema): emulators in the stock TrimUI
format written by TriMux for game folders the stock interface cannot read,
on a simulated card. The stock menu itself is not run here (it needs the
device's display): the tests check the files it reads and run the written
launch.sh with a stand-in RetroArch. Not a hardware test."""
import json
import os
import struct
import subprocess
import zlib

import pytest

from conftest import ctl, read, write
from test_ui import _ui_runs, ui
from test_uninstall import STOCK, stock_emu

needs_ui = pytest.mark.skipif(not _ui_runs(), reason="build/native/trimux-ui not built for this system")


@pytest.fixture
def ra(env, card, tmp_path):
    """Stand-in RetroArch: records its working folder, environment and arguments."""
    log = str(tmp_path / "ra.log")
    write(os.path.join(card, "TriMux/retroarch/retroarch"),
          '#!/bin/sh\n{ echo "cwd=$(pwd)"; echo "home=$HOME"; echo "args=$*"; cat "$4"; } > "%s"\n'
          'exit ${RA_EXIT:-0}\n' % log, 0o755)
    write(os.path.join(card, "TriMux/retroarch/autoconfig/TRIMUX Player1.cfg"), "input_device = x\n")
    write(os.path.join(card, "TriMux/licenses/RetroArch.txt"), "GPL")
    return log


def check(env):
    out = ctl(env, "stock", "check").stdout.splitlines()
    head = dict(l.split("=", 1) for l in out if not l.startswith("folder="))
    folders = {}
    for l in out:
        if l.startswith("folder="):
            d, state, sys_id, emu, emu_dir = l[7:].split("|")
            folders[d] = (state, sys_id, emu, emu_dir)
    return head, folders


def test_check_reports_each_game_folder(env, card, ra):
    stock_emu(os.path.join(card, "Emus"), "GB", "../../Roms/GB")            # the original card's
    write(os.path.join(card, "Roms/PORTS/game.sh"), "#!/bin/sh\n")
    write(os.path.join(card, "Roms/Zzz Unknown/a.bin"), "x")
    head, f = check(env)
    assert head["original_emulators"] == "1" and head["trimux_emulators"] == "0"
    assert f["GB"][0] == "original" and f["GB"][3] == "Emus/GB"
    assert f["GBA"] == ("missing", "GBA", "gpsp", "Emus/GBA")
    assert f["FC"][0] == "missing" and f["PS"][0] == "missing"
    assert f["PORTS"][0] == "unsupported" and f["Zzz Unknown"][0] == "unsupported"
    assert int(head["need_bytes"]) > 0


def test_prepare_writes_stock_emulators_and_keeps_the_original(env, card, ra):
    stock_emu(os.path.join(card, "Emus"), "GB", "../../Roms/GB")
    original = read(os.path.join(card, "Emus/GB/config.json"))
    write(os.path.join(card, "Emus/GBA/notes.txt"), "someone else's folder, no config.json")
    ctl(env, "stock", "prepare")
    # the original emulator is untouched; GBA's folder belongs to someone else: TriMux uses GBA-TriMux
    assert read(os.path.join(card, "Emus/GB/config.json")) == original
    assert sorted(os.listdir(os.path.join(card, "Emus/GBA"))) == ["notes.txt"]
    cfg = json.loads(read(os.path.join(card, "Emus/GBA-TriMux/config.json")))
    assert cfg["rompath"] == "../../Roms/GBA" and cfg["imgpath"] == "../../Imgs/GBA" and cfg["launch"] == "launch.sh"
    assert cfg["extlist"].split("|")[0] == "gba" and cfg["icon"] == "icon.png"
    with open(os.path.join(card, "Emus/GBA-TriMux/icon.png"), "rb") as fh:
        assert fh.read(8) == b"\x89PNG\r\n\x1a\n"
    for f in ("retroarch", "retroarch.cfg", "cores/gpsp_libretro.so", "cores/fceumm_libretro.so",
              "autoconfig/TRIMUX Player1.cfg", "licenses/RetroArch.txt"):
        assert os.path.exists(os.path.join(card, "Emus/_TriMux", f)), f
    assert not os.path.exists(os.path.join(card, "Emus/_TriMux/config.json"))   # never listed by the stock menu
    assert not os.path.exists(os.path.join(card, "Emus/_TriMux/cores/gambatte_libretro.so"))  # GB is the original's
    rcfg = read(os.path.join(card, "Emus/_TriMux/retroarch.cfg"))
    assert 'libretro_directory = "%s/Emus/_TriMux/cores"' % card in rcfg and "input_joypad_driver" in rcfg
    head, f = check(env)
    assert f["GBA"][0] == "trimux" and f["GB"][0] == "original" and f["FC"][0] == "trimux"
    assert head["need_bytes"] == "0" and head["trimux_emulators"] == "4"
    # the uninstall check now finds them too
    out = ctl(env, "uninstall", "check").stdout
    assert "not_read=ARCADE" not in out and "not_read=\n" in out


def test_written_launcher_runs_retroarch_like_trimux(env, card, ra, tmp_path):
    ctl(env, "stock", "prepare")
    launch = os.path.join(card, "Emus/GBA/launch.sh")
    rom = os.path.join(card, "Roms/GBA/Celeste Classic (World).gba")
    # the stock menu runs the launcher with the game as the last argument
    r = subprocess.run(["/bin/sh", launch, rom], capture_output=True, text=True, timeout=30)
    assert r.returncode == 0, r.stderr
    log = read(ra)
    assert "cwd=%s/Emus/_TriMux" % card in log and "home=%s/TriMuxData/retroarch" % card in log
    assert "-L %s/Emus/_TriMux/cores/gpsp_libretro.so %s" % (card, rom) in log
    assert 'savefile_directory = "%s/Saves/GBA"' % card in log            # the same saves TriMux uses
    assert 'system_directory = "%s/Bios"' % card in log
    # a game name relative to the folder works too
    subprocess.run(["/bin/sh", launch, "Celeste Classic (World).gba"], cwd=str(tmp_path), check=True, timeout=30)
    assert rom in read(ra)


def test_launcher_retries_with_sdl2_when_gles_fails(env, card, ra):
    ctl(env, "stock", "prepare")
    e = dict(os.environ, RA_EXIT="1")
    r = subprocess.run(["/bin/sh", os.path.join(card, "Emus/FC/launch.sh"),
                        os.path.join(card, "Roms/FC/Micro Mages (World).nes")], env=e, timeout=30)
    assert r.returncode == 1
    assert 'video_driver = "sdl2"' in read(ra)


def test_prepare_twice_keeps_files_and_refreshes_trimux_ones(env, card, ra):
    ctl(env, "stock", "prepare")
    p = os.path.join(card, "Emus/FC/launch.sh")
    write(p, "#!/bin/sh\nexit 9\n", 0o755)                     # an outdated launcher of TriMux's
    ctl(env, "stock", "prepare")
    assert "Emus/_TriMux" in read(p)


def png_rgba(path):
    """Decodes an 8-bit RGBA PNG (all five row filters) without PIL."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert (depth, ctype) == (8, 6)
        elif tag == b"IDAT":
            idat += body
        pos += 12 + n
    raw, rows, prev, stride = zlib.decompress(idat), [], bytearray(w * 4), w * 4
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b, c = prev[i], prev[i - 4] if i >= 4 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, rows


def test_icons_are_console_drawings(env, card, tmp_path):
    systems = [l[1:-1] for l in read(os.path.join(card, "TriMux/share/systems.ini")).splitlines()
               if l.startswith("[") and l.endswith("]")]
    assert "N64" in systems and "GBA" in systems
    seen = set()
    for sys_id in systems + ["UNKNOWN"]:
        out = str(tmp_path / ("%s.png" % sys_id))
        ctl(env, "stock", "icon", sys_id, out)
        w, h, rows = png_rgba(out)
        assert (w, h) == (300, 300)
        # transparent background (the stock menu shows the name below), a drawing in the middle
        for x, y in ((0, 0), (299, 0), (0, 299), (299, 299), (150, 2)):
            assert rows[y][x * 4 + 3] == 0, (sys_id, x, y)
        opaque = sum(1 for r in rows for i in range(3, len(r), 4) if r[i] == 255)
        assert 0.12 < opaque / (w * h) < 0.75, sys_id
        colours = {bytes(r[i:i + 3]) for r in rows[::7] for i in range(0, len(r), 28) if r[i + 3] == 255}
        assert len(colours) > 40, sys_id                         # shaded, not a flat badge
        seen.add(zlib.crc32(b"".join(rows)))
    assert len(seen) >= len(systems) - 3                         # one drawing per console (PC games share one)


def test_prepare_refreshes_icons_written_by_older_versions(env, card, ra):
    ctl(env, "stock", "prepare")
    mark = os.path.join(card, "Emus/FC/.trimux-stock")
    assert "files=2" in read(mark)
    assert check(env)[1]["FC"][0] == "trimux"
    write(mark, "FC fceumm\n")                                  # as 0.5.5 wrote it (label icons)
    write(os.path.join(card, "Emus/FC/icon.png"), "old")
    assert check(env)[1]["FC"][0] == "missing"
    ctl(env, "stock", "prepare")
    assert check(env)[1]["FC"][0] == "trimux"
    png_rgba(os.path.join(card, "Emus/FC/icon.png"))
    assert read(os.path.join(card, "Emus/FC/config.json")).count("icon.png") == 2


def test_prepare_reports_a_copy_error(env, card, ra):
    # a read-only Emus folder (where the runtime cannot be written)
    os.makedirs(os.path.join(card, "Emus"), exist_ok=True)
    os.chmod(os.path.join(card, "Emus"), 0o555)
    try:
        if os.access(os.path.join(card, "Emus"), os.W_OK):
            pytest.skip("running as root: permissions are not enforced")
        r = ctl(env, "stock", "prepare", check=False)
        assert r.returncode == 1 and "stock.err" in r.stderr
    finally:
        os.chmod(os.path.join(card, "Emus"), 0o755)


@needs_ui
def test_stock_page_prepares_and_offers_a_test(env, card, ra, tmp_path):
    shot = str(tmp_path / "stock.bmp")
    # the page lists the folders; "Preparar" is 3rd from the end: UP,UP,UP -> A -> "Sim" selected -> A
    r = ui(env, STOCK + ",shot=%s,UP,UP,UP,A,A,B,B,B,B" % shot)
    assert r.returncode == 0
    assert os.path.exists(os.path.join(card, "Emus/GBA/config.json"))
    assert "stock: 5 emulator(s) ready" in read(os.path.join(card, "TriMuxData/logs/trimux.log"))
    assert os.path.getsize(shot) > 100000
    # "Testar no sistema oficial" (2nd from the end) -> "Sim" -> the menu hands over to the stock interface
    r = ui(env, STOCK + ",UP,UP,A,A")
    assert r.returncode == 20
