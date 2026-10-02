#!/usr/bin/env python3
"""Cross-checks share/emulators.ini and share/systems.ini against what each
built core reports (coreprobe output): config_name must equal the core's
library_name (RetroArch stores per-core settings under that name) and every
non-archive extension of a platform must be accepted by its emulators."""
import configparser
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SHARE = os.path.join(ROOT, "sdcard", "TriMux", "share")
ARCHIVES = {"zip", "7z"}


def main(probe):
    cores = {}
    for line in open(probe):
        m = re.match(r'ok\s+\S*/(\S+)\s+api=\d+ name="([^"]*)" version="[^"]*" ext="([^"]*)"', line)
        if m:
            cores[m.group(1)] = (m.group(2), set(m.group(3).lower().split("|")))
    emus = configparser.ConfigParser(interpolation=None)
    emus.read(os.path.join(SHARE, "emulators.ini"), encoding="utf-8")
    systems = configparser.ConfigParser(interpolation=None)
    systems.read(os.path.join(SHARE, "systems.ini"), encoding="utf-8")
    errors = []
    for e in emus.sections():
        core = emus.get(e, "core")
        if core not in cores:
            if emus.get(e, "experimental", fallback="0") != "1":
                errors.append("%s: core %s not built/probed" % (e, core))
            continue
        if emus.get(e, "config_name") != cores[core][0]:
            errors.append("%s: config_name %r != library_name %r" % (e, emus.get(e, "config_name"), cores[core][0]))
    for s in systems.sections():
        exts = {x.strip() for x in systems.get(s, "extensions").split(",")} - ARCHIVES
        for e in (x.strip() for x in systems.get(s, "emulators").split(",")):
            core = emus.get(e, "core")
            if core not in cores:
                continue
            missing = exts - cores[core][1]
            if missing:
                errors.append("%s/%s: extensions not accepted by the core: %s" % (s, e, ", ".join(sorted(missing))))
    for err in errors:
        print("catalog:", err)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
