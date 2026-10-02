#!/usr/bin/env python3
"""Arcade zip name -> title list for the cover downloader.

Reads a FinalBurn Neo ClrMame Pro XML DAT and writes "name<TAB>title" lines,
sorted, so "mslug.zip" can be looked up as "Metal Slug - Super Vehicle-001"
in libretro-thumbnails. Deterministic output (same input, same bytes)."""
import sys
import xml.etree.ElementTree as ET


def main(dat, out):
    rows = {}
    for game in ET.parse(dat).getroot().iter("game"):
        name = (game.get("name") or "").strip()
        desc = (game.findtext("description") or "").strip()
        if name and desc and "\t" not in name + desc and "\n" not in name + desc:
            rows[name] = desc
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        for name in sorted(rows):
            f.write("%s\t%s\n" % (name, rows[name]))
    print("arcade-names: %d titles" % len(rows))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
