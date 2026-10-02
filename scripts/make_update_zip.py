#!/usr/bin/env python3
"""Creates the update packages, both with only the replaceable parts of the
card (trimui/ and TriMux/ plus LEIA-ME.txt):

  TriMux-<version>-update.zip     manual update: extract over an existing card
  TriMux-<version>-update.tar.gz  online update (trimuxctl update install);
                                  the firmware's busybox has tar but no unzip

Neither touches Roms, Bios, Saves, States or TriMuxData. Entries are sorted and
timestamped from SOURCE_DATE_EPOCH for reproducibility."""
import argparse
import gzip
import io
import os
import tarfile
import time
import zipfile

KEEP = ("trimui", "TriMux", "LEIA-ME.txt")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--version", required=True)
    a = ap.parse_args()
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "1767225600"))
    stamp = time.gmtime(epoch)[:6]
    os.makedirs(a.out, exist_ok=True)
    path = os.path.join(a.out, "TriMux-%s-update.zip" % a.version)
    files = []
    for top in KEEP:
        p = os.path.join(a.tree, top)
        if os.path.isfile(p):
            files.append(top)
        for d, dirs, fs in os.walk(p):
            dirs.sort()
            for f in sorted(fs):
                files.append(os.path.relpath(os.path.join(d, f), a.tree))
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel in files:
            info = zipfile.ZipInfo(rel.replace(os.sep, "/"), date_time=stamp)
            src = os.path.join(a.tree, rel)
            mode = 0o755 if (rel.endswith(".sh") or os.access(src, os.X_OK)) else 0o644
            info.external_attr = (0o100000 | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(src, "rb") as f:
                z.writestr(info, f.read())
    print(path)
    print(write_tar(a.tree, a.out, a.version, files, epoch))


def write_tar(tree, out, version, files, epoch):
    """Deterministic ustar + gzip (no name or time in the gzip header)."""
    path = os.path.join(out, "TriMux-%s-update.tar.gz" % version)
    dirs = set()
    for rel in files:
        parts = rel.split(os.sep)[:-1]
        for i in range(1, len(parts) + 1):
            dirs.add("/".join(parts[:i]))
    entries = sorted([(d, True) for d in dirs] + [(f.replace(os.sep, "/"), False) for f in files])
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.USTAR_FORMAT) as t:
        for rel, is_dir in entries:
            info = tarfile.TarInfo(rel)
            info.mtime = epoch
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            if is_dir:
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                t.addfile(info)
                continue
            src = os.path.join(tree, rel)
            info.mode = 0o755 if (rel.endswith(".sh") or os.access(src, os.X_OK)) else 0o644
            with open(src, "rb") as f:
                data = f.read()
            info.size = len(data)
            t.addfile(info, io.BytesIO(data))
    with open(path, "wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=9) as g:
        g.write(buf.getvalue())
    return path


if __name__ == "__main__":
    main()
