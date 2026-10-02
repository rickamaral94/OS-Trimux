#!/usr/bin/env python3
"""Builds the TriMux microSD image (raw .img for Rufus/balenaEtcher) and .img.xz.

Layout (MBR, one partition):
  sector 0          MBR, partition 1 type 0x0C (FAT32 LBA), bootable flag off
  sectors 1..2047   zero. The Allwinner boot ROM looks for an "eGON.BT0" boot
                    header at 8 KiB on the card; keeping this area empty makes
                    the device ignore the card for booting and start its own
                    firmware from eMMC, which then mounts this partition.
  sector 2048..     FAT32 "TRIMUX", 32 KiB clusters

The FAT tables are sized for a 1 TiB partition, so after flashing the
filesystem can grow to the whole card by rewriting metadata only (see
src/core/fatgrow.c and "Configurações > Armazenamento > Expandir partição").

Requires: mkfs.fat (dosfstools >= 4.2), mcopy/mmd (mtools), fsck.fat, xz.
"""
import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

SECTOR = 512
PART_START = 2048                    # 1 MiB alignment
CLUSTER_SECTORS = 64                 # 32 KiB clusters
FAT_DESIGN_BYTES = 1 << 40           # FAT tables sized for a 1 TiB filesystem
VOLUME_ID = "54524d58"               # "TRMX"
LABEL = "TRIMUX"


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.stderr.write("command failed: %s\n%s%s" % (" ".join(cmd), r.stdout, r.stderr))
        raise SystemExit(1)
    return r.stdout


def tree_bytes(root):
    total = 0
    for d, dirs, files in os.walk(root):
        total += 32768 * (1 + len(dirs))
        for f in files:
            sz = os.path.getsize(os.path.join(d, f))
            total += (sz + 32767) // 32768 * 32768
    return total


def make_fs(part_path, part_sectors):
    """mkfs.fat on a sparse 1 TiB file, then shrink the BPB to part_sectors."""
    design_kib = FAT_DESIGN_BYTES // 1024
    if os.path.exists(part_path):
        os.unlink(part_path)
    run(["mkfs.fat", "-C", "-F", "32", "-S", str(SECTOR), "-s", str(CLUSTER_SECTORS), "-R", "32",
         "-h", str(PART_START), "-i", VOLUME_ID, "-n", LABEL, "--invariant", part_path, str(design_kib)])
    with open(part_path, "r+b") as f:
        bs = bytearray(f.read(SECTOR))
        rsvd = struct.unpack_from("<H", bs, 14)[0]
        nfats = bs[16]
        fatsz = struct.unpack_from("<I", bs, 36)[0]
        fsinfo = struct.unpack_from("<H", bs, 48)[0]
        backup = struct.unpack_from("<H", bs, 50)[0]
        data_start = rsvd + nfats * fatsz
        if part_sectors <= data_start + 1024:
            raise SystemExit("image too small for the FAT layout")
        # whole clusters only
        part_sectors = data_start + (part_sectors - data_start) // CLUSTER_SECTORS * CLUSTER_SECTORS
        for sec in (0, backup):
            f.seek(sec * SECTOR)
            b = bytearray(f.read(SECTOR))
            struct.pack_into("<I", b, 32, part_sectors)
            f.seek(sec * SECTOR)
            f.write(b)
        for sec in (fsinfo, backup + fsinfo):
            f.seek(sec * SECTOR)
            b = bytearray(f.read(SECTOR))
            if struct.unpack_from("<I", b, 0)[0] == 0x41615252:
                struct.pack_into("<I", b, 488, 0xFFFFFFFF)   # free count unknown
                struct.pack_into("<I", b, 492, 0xFFFFFFFF)   # next free unknown
                f.seek(sec * SECTOR)
                f.write(b)
        f.truncate(part_sectors * SECTOR)
    return part_sectors, data_start, fatsz


def copy_tree(part_path, tree, epoch):
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1", MTOOLS_NO_VFAT="0", SOURCE_DATE_EPOCH=str(epoch),
               TZ="UTC", LC_ALL="C.UTF-8")
    # Deterministic order and timestamps.
    for d, dirs, files in os.walk(tree):
        dirs.sort()
        for name in sorted(dirs):
            rel = os.path.relpath(os.path.join(d, name), tree)
            run(["mmd", "-i", part_path, "::/" + rel.replace(os.sep, "/")], env=env)
        for name in sorted(files):
            src = os.path.join(d, name)
            rel = os.path.relpath(src, tree)
            os.utime(src, (epoch, epoch))
            run(["mcopy", "-m", "-o", "-i", part_path, src, "::/" + rel.replace(os.sep, "/")], env=env)


def write_mbr(img, part_sectors, disk_id):
    mbr = bytearray(SECTOR)
    struct.pack_into("<I", mbr, 440, disk_id)
    e = 446
    mbr[e] = 0x00
    mbr[e + 1:e + 4] = b"\xfe\xff\xff"           # CHS start/end unused (LBA only)
    mbr[e + 4] = 0x0C
    mbr[e + 5:e + 8] = b"\xfe\xff\xff"
    struct.pack_into("<II", mbr, e + 8, PART_START, part_sectors)
    mbr[510:512] = b"\x55\xaa"
    img.seek(0)
    img.write(mbr)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--size-mib", type=int, default=1024, help="image size (default 1024 MiB)")
    ap.add_argument("--no-xz", action="store_true")
    args = ap.parse_args()
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "1767225600"))  # 2026-01-01 when unset
    os.makedirs(args.out, exist_ok=True)
    name = "TriMux-%s-brickpro" % args.version
    img_path = os.path.join(args.out, name + ".img")
    total = args.size_mib * 1024 * 1024 // SECTOR
    part_sectors = total - PART_START
    with tempfile.TemporaryDirectory(dir=args.out) as tmp:
        part = os.path.join(tmp, "part.fat")
        part_sectors, data_start, fatsz = make_fs(part, part_sectors)
        need = tree_bytes(args.tree)
        free = (part_sectors - data_start) * SECTOR
        if need > free * 0.95:
            raise SystemExit("content (%d MiB) does not fit in a %d MiB image" % (need >> 20, args.size_mib))
        copy_tree(part, args.tree, epoch)
        # The image's own check: must be clean before it is published.
        run(["fsck.fat", "-n", "-v", part])
        with open(img_path, "wb") as img:
            img.truncate(total * SECTOR)
            write_mbr(img, part_sectors, int(VOLUME_ID, 16))
            img.seek(PART_START * SECTOR)
            with open(part, "rb") as p:
                shutil.copyfileobj(p, img, 1 << 20)
    sums = [(os.path.basename(img_path), sha256(img_path))]
    if not args.no_xz:
        xz_path = img_path + ".xz"
        if os.path.exists(xz_path):
            os.unlink(xz_path)
        run(["xz", "-T0", "-9", "-k", "-f", img_path])
        sums.append((os.path.basename(xz_path), sha256(xz_path)))
    with open(os.path.join(args.out, name + ".sha256"), "w") as f:
        for n, h in sums:
            f.write("%s  %s\n" % (h, n))
    print("image: %s (%d MiB, partition %d sectors, FAT %d sectors x2, data from sector %d)"
          % (img_path, args.size_mib, part_sectors, fatsz, PART_START + data_start))
    for n, h in sums:
        print("sha256 %s  %s" % (h, n))


if __name__ == "__main__":
    main()
