"""Card image format and on-device partition growth (run on image files).

Needs mkfs.fat, mtools and fsck.fat (present in the toolchain container)."""
import glob
import os
import shutil
import struct
import subprocess

import pytest

from conftest import CTL, ROOT, have, write

pytestmark = pytest.mark.skipif(not have("mkfs.fat", "mcopy", "fsck.fat"), reason="dosfstools/mtools missing")
MAKE = os.path.join(ROOT, "scripts", "make_image.py")


def build(tmp_path, size_mib=320):
    tree = tmp_path / "tree"
    write(str(tree / "TriMux" / "VERSION"), "t\n")
    write(str(tree / "trimui" / "app" / "MainUI"), "#!/bin/sh\n")
    write(str(tree / "Roms" / "Game Boy Advance (GBA)" / "LEIA-ME.txt"), "ç acentuação\n")
    write(str(tree / "TriMux" / "retroarch" / "autoconfig" / "linuxraw" / "TRIMUI Player1.cfg"), "x\n")
    out = tmp_path / "out"
    subprocess.run(["python3", MAKE, "--tree", str(tree), "--out", str(out), "--version", "t",
                    "--size-mib", str(size_mib), "--no-xz"], check=True, capture_output=True)
    return str(out / "TriMux-t-brickpro.img")


def mbr(path):
    with open(path, "rb") as f:
        return f.read(512)


def check_fs(img, start=2048):
    part = img + ".part"
    subprocess.run(["dd", "if=" + img, "of=" + part, "bs=1M", "skip=%d" % (start // 2048), "conv=sparse",
                    "status=none"], check=True)
    r = subprocess.run(["fsck.fat", "-n", part], capture_output=True, text=True)
    os.unlink(part)
    return r


def test_layout(tmp_path):
    img = build(tmp_path)
    m = mbr(img)
    assert m[510:512] == b"\x55\xaa"
    ptype, = struct.unpack_from("B", m, 446 + 4)
    start, length = struct.unpack_from("<II", m, 446 + 8)
    assert ptype == 0x0C and start == 2048
    assert all(m[446 + 16 * i + 4] == 0 for i in (1, 2, 3))
    with open(img, "rb") as f:
        f.seek(512)
        gap = f.read(2047 * 512)
        assert gap == bytes(len(gap)), "boot area must stay empty (no eGON.BT0)"
        f.seek(2048 * 512)
        bs = f.read(512)
    assert bs[82:90] == b"FAT32   " and bs[71:77] == b"TRIMUX"
    assert struct.unpack_from("<I", bs, 28)[0] == 2048          # hidden sectors
    assert struct.unpack_from("<I", bs, 32)[0] <= length
    assert check_fs(img).returncode == 0
    listing = subprocess.run(["mdir", "-/", "-b", "-i", img + "@@1M", "::/"], capture_output=True, text=True).stdout
    assert "TRIMUI Player1.cfg" in listing and "Game Boy Advance (GBA)" in listing
    # only the image asks for the first-boot growth (never the update packages)
    state = subprocess.run(["mdir", "-b", "-i", img + "@@1M", "::/TriMuxData/state"], capture_output=True, text=True).stdout
    assert "autogrow" in state


def test_grow_on_bigger_card(tmp_path):
    img = build(tmp_path)
    card = str(tmp_path / "card.img")
    shutil.copy(img, card)
    with open(card, "r+b") as f:
        f.truncate(8 * 1024 ** 3)                                 # an 8 GiB card (sparse)
    plan = subprocess.run([CTL, "fat-grow", "plan", card], capture_output=True, text=True, check=True).stdout
    assert "new_sectors=" in plan and "nothing to do" not in plan
    subprocess.run([CTL, "fat-grow", "apply", card], check=True, capture_output=True)
    start, length = struct.unpack_from("<II", mbr(card), 446 + 8)
    assert start == 2048 and length > 7 * 1024 ** 3 // 512
    assert check_fs(card).returncode == 0
    # the new space is usable: write a file well beyond the old size
    big = str(tmp_path / "big.bin")
    with open(big, "wb") as f:
        f.truncate(200 * 1024 * 1024)
    subprocess.run(["mcopy", "-i", card + "@@1M", big, "::/big.bin"], check=True)
    assert check_fs(card).returncode == 0
    again = subprocess.run([CTL, "fat-grow", "plan", card], capture_output=True, text=True, check=True).stdout
    assert "nothing to do" in again


def test_grow_while_mounted_by_linux(tmp_path):
    """Linux marks a mounted FAT32 volume dirty in the primary boot sector only
    (byte 65); the backup copy keeps 0. That is the state of the card while
    TriMux runs, and also after a power cut. Growth must still be allowed."""
    img = build(tmp_path)
    card = str(tmp_path / "card.img")
    shutil.copy(img, card)
    with open(card, "r+b") as f:
        f.truncate(8 * 1024 ** 3)
        f.seek(2048 * 512 + 65)
        f.write(b"\x01")
    plan = subprocess.run([CTL, "fat-grow", "plan", card], capture_output=True, text=True)
    assert plan.returncode == 0 and "new_sectors=" in plan.stdout, plan.stderr
    subprocess.run([CTL, "fat-grow", "apply", card], check=True, capture_output=True)
    with open(card, "rb") as f:
        f.seek(2048 * 512)
        primary = f.read(512)
        f.seek((2048 + 6) * 512)
        backup = f.read(512)
    assert struct.unpack_from("<I", primary, 32) == struct.unpack_from("<I", backup, 32)
    assert primary[65] == 1 and backup[65] == 0      # each copy keeps its own state byte
    assert check_fs(card).returncode in (0, 1)       # fsck may only report the dirty flag


def test_grow_refuses_foreign_layouts(tmp_path):
    img = build(tmp_path)
    two = str(tmp_path / "two.img")
    shutil.copy(img, two)
    with open(two, "r+b") as f:
        f.seek(446 + 16 + 4)
        f.write(b"\x83")                                          # a second partition
        f.truncate(1024 ** 3)
    r = subprocess.run([CTL, "fat-grow", "apply", two], capture_output=True, text=True)
    assert r.returncode == 1 and "more than one partition" in r.stderr
    blank = str(tmp_path / "blank.img")
    with open(blank, "wb") as f:
        f.truncate(64 * 1024 ** 2)
    assert subprocess.run([CTL, "fat-grow", "plan", blank], capture_output=True).returncode == 1


def test_grow_respects_fat_capacity(tmp_path):
    """A card larger than the FAT design size grows only up to 1 TiB-ish."""
    img = build(tmp_path)
    card = str(tmp_path / "huge.img")
    shutil.copy(img, card)
    with open(card, "r+b") as f:
        f.truncate(1536 * 1024 ** 3)                              # 1.5 TiB (sparse)
    out = subprocess.run([CTL, "fat-grow", "plan", card], capture_output=True, text=True, check=True).stdout
    fields = dict(x.split("=") for x in out.split()[:4])
    assert int(fields["new_sectors"]) <= int(fields["max_sectors"])
    assert int(fields["max_sectors"]) * 512 <= 1.01 * 1024 ** 4


@pytest.mark.skipif(not glob.glob(os.path.join(ROOT, "build", "out", "*.img")), reason="no release image built")
def test_release_image():
    img = sorted(glob.glob(os.path.join(ROOT, "build", "out", "*.img")))[-1]
    assert check_fs(img).returncode == 0
    listing = subprocess.run(["mdir", "-/", "-b", "-i", img + "@@1M", "::/"], capture_output=True, text=True).stdout
    for must in ("trimui/app/MainUI", "trimui/app/preload.sh", "TriMux/bin/trimux-ui", "TriMux/bin/trimuxctl",
                 "TriMux/retroarch/retroarch", "TriMux/share/systems.ini", "TriMux/licenses/retroarch.txt"):
        assert must in listing, must
    for banned in (".gba", ".nes", ".iso", "scph", "gba_bios", "neogeo.zip", ".awimg"):
        assert banned not in listing.lower().replace("leia-me", ""), banned
