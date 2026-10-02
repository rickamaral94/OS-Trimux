#!/usr/bin/env python3
"""Checks that aarch64 binaries only need what firmware v1.1.1 provides.

- symbol versions: GLIBC <= 2.33, GLIBCXX <= 3.4.28, CXXABI <= 1.3.12, GCC <= 10
- NEEDED libraries: must be in the firmware's library list (firmware/libs-v1.1.1.txt)

Usage: check_abi.py <elf>...   (uses aarch64-linux-gnu-readelf or readelf)
"""
import os
import re
import shutil
import subprocess
import sys

LIMITS = {"GLIBC": (2, 33), "GLIBCXX": (3, 4, 28), "CXXABI": (1, 3, 12), "GCC": (10, 0)}
HERE = os.path.dirname(os.path.abspath(__file__))
LIBS_FILE = os.path.join(HERE, "..", "firmware", "libs-v1.1.1.txt")


def readelf():
    for name in ("aarch64-linux-gnu-readelf", "readelf"):
        if shutil.which(name):
            return name
    sys.exit("readelf not found")


def parse_version(s):
    return tuple(int(x) for x in s.split("."))


def check(path, tool, firmware_libs):
    problems = []
    out = subprocess.run([tool, "-d", "-V", "-h", path], capture_output=True, text=True).stdout
    if "AArch64" not in out:
        problems.append("not an AArch64 binary")
    for lib in re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", out):
        if firmware_libs is not None and lib not in firmware_libs:
            problems.append("needs %s, which firmware v1.1.1 does not ship" % lib)
    for prefix, ver in set(re.findall(r"\b(GLIBC|GLIBCXX|CXXABI|GCC)_([0-9.]+)\b", out)):
        if parse_version(ver) > LIMITS[prefix]:
            problems.append("requires %s_%s" % (prefix, ver))
    return problems


def main(paths):
    tool = readelf()
    firmware_libs = None
    if os.path.exists(LIBS_FILE):
        with open(LIBS_FILE) as f:
            firmware_libs = {l.strip() for l in f if l.strip() and not l.startswith("#")}
    bad = 0
    for p in paths:
        problems = check(p, tool, firmware_libs)
        status = "ok " if not problems else "BAD"
        print("%s %s%s" % (status, os.path.basename(p), "" if not problems else ": " + "; ".join(problems)))
        bad += bool(problems)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
