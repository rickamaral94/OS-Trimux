#!/usr/bin/env python3
"""Renders documentation screenshots of the menu with SDL's offscreen driver,
a simulated Brick Pro (tests/py/conftest.py) and a card with homebrew names.
These are host renders, not photos of the device. Output: docs/img/*.png"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(ROOT, "tests", "py"))
from conftest import UI, make_card, make_device, write  # noqa: E402

SHOTS = [
    ("01-assistente", "", False),
    ("02-inicio", "", True),
    ("03-plataforma", "DOWN,DOWN,DOWN,DOWN,DOWN,A", True),
    ("04-emulador", "DOWN,DOWN,DOWN,DOWN,DOWN,A,SELECT", True),
    ("05-configuracoes", "UP,A", True),
    ("06-energia", "UP,A,DOWN,DOWN,DOWN,A", True),
    ("07-leds", "UP,A,DOWN,DOWN,DOWN,DOWN,DOWN,A,DOWN,A", True),
    ("08-informacoes", "UP,A,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,A,A", True),
    ("09-busca", "Y,A", True),
    ("10-menu-rapido", "MENU", True),
    ("11-ps2", "UP,UP", True),
    ("12-ps2-lista", "UP,UP,A", True),
    ("13-ports", "UP,UP,UP", True),
]


def main():
    try:
        from PIL import Image
    except ImportError:
        sys.exit("pip install pillow")
    out = os.path.join(ROOT, "docs", "img")
    os.makedirs(out, exist_ok=True)
    for name, script, wizard_done in SHOTS:
        with tempfile.TemporaryDirectory() as t:
            dev = make_device(os.path.join(t, "dev"))
            sd = make_card(os.path.join(t, "sd"))
            for rel in ("Roms/SFC/Super Boss Gaiden (World).sfc", "Roms/MD/Old Towers (World).bin",
                        "Roms/GBA/Anguna (World).gba", "Roms/FC/Alter Ego (World).nes",
                        "Roms/GB/uCity (World).gb", "Roms/NGP/Homebrew (World).ngp",
                        "Roms/DOOM/freedoom1.wad", "Roms/PORTS/OpenTyrian.sh", "Roms/PORTS/SuperTux.sh"):
                write(os.path.join(sd, rel), "")
            if wizard_done:
                write(os.path.join(sd, "TriMuxData/config/trimux.ini"), "[general]\nwizard_done = 1\n")
                write(os.path.join(sd, "TriMuxData/config/recent.txt"), "Roms/GBA/Celeste Classic (World).gba\n")
                write(os.path.join(sd, "TriMuxData/config/favorites.txt"), "Roms/FC/Micro Mages (World).nes\n")
            bmp = os.path.join(t, name + ".bmp")
            env = dict(os.environ, TRIMUX_SYSFS_ROOT=dev, TRIMUX_SDCARD=sd, TRIMUX_TMP=os.path.join(t, "tmp"),
                       SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy")
            subprocess.run([UI, "--window", "1024", "768", "--script", (script + "," if script else "") +
                            "shot=" + bmp], env=env, check=False, timeout=60)
            if os.path.exists(bmp):
                Image.open(bmp).convert("RGB").save(os.path.join(out, name + ".png"), optimize=True)
                print("ok", name)
            else:
                print("FAILED", name)


if __name__ == "__main__":
    main()
