#!/usr/bin/env python3
"""Inspect and unpack Allwinner IMAGEWTY firmware images (.awimg / PhoenixSuit).

Only the unencrypted format version 0x300 used by the TrimUI TG4040 firmware is
supported. The tool never writes to block devices: it reads an image file and
extracts members into a directory.

    awimg.py list  trimui_tg4040.awimg
    awimg.py extract trimui_tg4040.awimg outdir/
    awimg.py sunxi-mbr sunxi_mbr.fex       # print the eMMC partition layout
"""
import argparse
import os
import struct
import sys

MAGIC = b"IMAGEWTY"
HEADER_BLOCK = 1024


class AwImageError(Exception):
    pass


def read_entries(path):
    with open(path, "rb") as f:
        head = f.read(HEADER_BLOCK)
        if len(head) < 0x60 or head[:8] != MAGIC:
            raise AwImageError("not an IMAGEWTY image (bad magic)")
        header_version, header_size = struct.unpack_from("<II", head, 8)
        if header_version != 0x300:
            raise AwImageError("unsupported IMAGEWTY header version 0x%x" % header_version)
        num_files = struct.unpack_from("<I", head, 0x3C)[0]
        if not 0 < num_files < 1024:
            raise AwImageError("implausible file count %d" % num_files)
        size = os.fstat(f.fileno()).st_size
        entries = []
        for i in range(num_files):
            f.seek(HEADER_BLOCK * (i + 1))
            fh = f.read(HEADER_BLOCK)
            filename_len, total = struct.unpack_from("<II", fh, 0)
            if total != HEADER_BLOCK:
                raise AwImageError("file header %d has size %d" % (i, total))
            maintype = fh[8:16].rstrip(b"\0").decode("ascii", "replace")
            subtype = fh[16:32].rstrip(b"\0").decode("ascii", "replace")
            name = fh[36:36 + 256].split(b"\0")[0].decode("ascii", "replace")
            stored, _, original, _, offset = struct.unpack_from("<IIIII", fh, 292)
            if offset + original > size:
                raise AwImageError("entry %s points outside the image" % name)
            if "/" in name or "\\" in name or name in ("", ".", ".."):
                raise AwImageError("unsafe member name %r" % name)
            entries.append({"maintype": maintype, "subtype": subtype, "name": name,
                            "stored": stored, "length": original, "offset": offset})
        return entries


def extract(path, outdir, only=None):
    os.makedirs(outdir, exist_ok=True)
    with open(path, "rb") as f:
        for e in read_entries(path):
            if only and e["name"] not in only:
                continue
            f.seek(e["offset"])
            remaining = e["length"]
            with open(os.path.join(outdir, e["name"]), "wb") as out:
                while remaining:
                    chunk = f.read(min(remaining, 1 << 20))
                    if not chunk:
                        raise AwImageError("truncated member %s" % e["name"])
                    out.write(chunk)
                    remaining -= len(chunk)


def parse_sunxi_mbr(data):
    """Parse an Allwinner 'softw411' MBR. Returns a list of partitions in sectors."""
    if data[8:16] != b"softw411":
        raise AwImageError("not a sunxi MBR")
    count = struct.unpack_from("<I", data, 24)[0]
    parts = []
    for i in range(min(count, 120)):
        p = data[32 + 128 * i: 32 + 128 * (i + 1)]
        ah, al, lh, ll = struct.unpack_from("<IIII", p, 0)
        name = p[32:48].rstrip(b"\0").decode("ascii", "replace")
        parts.append({"name": name, "start": (ah << 32) | al, "length": (lh << 32) | ll,
                      "user_type": struct.unpack_from("<I", p, 48)[0]})
    return parts


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list"); p.add_argument("image")
    p = sub.add_parser("extract"); p.add_argument("image"); p.add_argument("outdir")
    p.add_argument("--only", nargs="*")
    p = sub.add_parser("sunxi-mbr"); p.add_argument("file")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "list":
            for e in read_entries(args.image):
                print("%-8s %-16s %-24s %10d @0x%x" % (e["maintype"], e["subtype"], e["name"], e["length"], e["offset"]))
        elif args.cmd == "extract":
            extract(args.image, args.outdir, set(args.only) if args.only else None)
        elif args.cmd == "sunxi-mbr":
            with open(args.file, "rb") as f:
                data = f.read(16384)
            for part in parse_sunxi_mbr(data):
                print("%-12s start=%-9d len=%-9d (%.1f MiB)" % (part["name"], part["start"], part["length"],
                                                                  part["length"] * 512 / 2 ** 20))
    except (AwImageError, OSError) as exc:
        print("awimg: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
