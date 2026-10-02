#!/usr/bin/env python3
"""Creates TriMux-<version>-update.zip: only the replaceable parts of the card
(trimui/ and TriMux/ plus LEIA-ME.txt). Extracting it over an existing card
updates TriMux without touching Roms, Bios, Saves, States or TriMuxData.
Entries are sorted and timestamped from SOURCE_DATE_EPOCH for reproducibility."""
import argparse
import os
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


if __name__ == "__main__":
    main()
