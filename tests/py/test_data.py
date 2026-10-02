"""Consistency of translations, platform and emulator catalogs."""
import configparser
import glob
import os
import re

from conftest import ROOT

SHARE = os.path.join(ROOT, "sdcard", "TriMux", "share")


def load_lang(code):
    keys = {}
    for line in open(os.path.join(SHARE, "i18n", code + ".lang"), encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        k, _, v = line.partition("=")
        keys[k.strip()] = v.strip()
    return keys


def ini(name):
    cp = configparser.ConfigParser(interpolation=None, strict=True)
    cp.read(os.path.join(SHARE, name), encoding="utf-8")
    return cp


def used_keys():
    keys = set()
    srcs = glob.glob(os.path.join(ROOT, "src", "ui", "*.c")) + glob.glob(os.path.join(ROOT, "src", "core", "*.c"))
    for path in srcs:
        text = open(path, encoding="utf-8").read()
        keys |= set(re.findall(r'tr\("([a-z0-9_.]+)"\)', text))
        keys |= set(re.findall(r'"((?:power|leds|launch|color|hk|emu|sys|theme)\.[a-z0-9_.]+)"', text))
    for cp in (ini("systems.ini"), ini("emulators.ini")):
        for s in cp.sections():
            if cp.get(s, "note", fallback=""):
                keys.add(cp.get(s, "note"))
    return keys


def test_default_language_has_every_key():
    pt = load_lang("pt_BR")
    dynamic = {"theme.dark", "theme.light", "theme.contrast", "system.log"}
    dynamic |= {"wizard.step%d.%s" % (i, k) for i in range(6) for k in ("title", "text")}
    dynamic |= {"add.step%d" % i for i in range(1, 7)}
    files = re.compile(r"\.(h|ini|txt|tsv|cfg|so|log|json|lang|ttf)$")
    missing = sorted(k for k in used_keys() | dynamic if k not in pt and not files.search(k))
    assert not missing, missing


def test_translations_match_format_specifiers():
    pt = load_lang("pt_BR")
    spec = re.compile(r"%[-0-9.]*[a-z]+")
    for path in glob.glob(os.path.join(SHARE, "i18n", "*.lang")):
        code = os.path.basename(path)[:-5]
        other = load_lang(code)
        assert "lang.name" in other
        for k, v in other.items():
            assert k in pt, "%s: unknown key %s" % (code, k)
            assert spec.findall(v) == spec.findall(pt[k]), "%s: %s" % (code, k)


def test_catalog_references():
    systems, emus = ini("systems.ini"), ini("emulators.ini")
    built = open(os.path.join(ROOT, "scripts", "build_cores.sh")).read()
    folders = {}
    for s in systems.sections():
        exts = [e.strip() for e in systems.get(s, "extensions").split(",")]
        assert exts and all(re.fullmatch(r"[a-z0-9]+", e) for e in exts), s
        for f in systems.get(s, "folders").split(","):
            f = f.strip().lower()
            assert f not in folders, "folder %s used by %s and %s" % (f, folders[f], s)
            folders[f] = s
        for e in systems.get(s, "emulators").split(","):
            assert emus.has_section(e.strip()), "%s -> %s" % (s, e)
    for e in emus.sections():
        core = emus.get(e, "core")
        if emus.get(e, "experimental", fallback="0") != "1":
            assert core in built, "%s core %s is not built by build_cores.sh" % (e, core)
        assert emus.get(e, "profile") in ("economy", "balanced", "performance")


def test_no_copyrighted_payload_in_repo():
    banned = re.compile(r"\.(gba|gbc|nes|sfc|smc|gen|iso|chd|cue|pbp|bin|rom|awimg|img|7z|zip)$", re.I)
    for d, dirs, files in os.walk(ROOT):
        dirs[:] = [x for x in dirs if not x.startswith(".") and not x.startswith("build")]
        for f in files:
            assert not banned.search(f), os.path.join(d, f)
