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
    ("08-informacoes", "UP,A" + ",DOWN" * 10 + ",A,A", True),
    ("09-busca", "Y,A", True),
    ("10-menu-rapido", "MENU", True),
    ("11-ports", "UP,UP", True),
    ("12-botoes", "UP,A,DOWN,DOWN,A,DOWN,DOWN,A,DOWN,DOWN", True),
    # network shots use a simulated, connected Wi-Fi (see NET_SHOTS)
    ("13-rede", "UP,A" + ",DOWN" * 6 + ",A", True),
    ("14-wifi-redes", "UP,A" + ",DOWN" * 6 + ",A,DOWN,DOWN,A,wait=4200,DOWN,DOWN", True),
    ("15-senha-wifi", "UP,A" + ",DOWN" * 6 + ",A,DOWN,DOWN,A,DOWN,A,A,RIGHT,A,RIGHT,A,R1,DOWN,A,RIGHT,A", True),
]
NET_SHOTS = {"13-rede", "14-wifi-redes", "15-senha-wifi"}
DIAG = "UP,A" + ",DOWN" * 10 + ",A" + ",DOWN" * 5 + ",A"
SHOTS += [
    ("16-registros", DIAG, True),
    ("17-sessoes", DIAG + ",DOWN,DOWN,A,DOWN", True),
]
SHOTS += [
    ("18-capas", "DOWN,DOWN,DOWN,A,DOWN", True),
    ("19-capas-opcoes", "UP,A" + ",DOWN" * 7 + ",A" + ",DOWN" * 5 + ",A", True),
]
SHOTS += [
    ("22-aplicativos", "UP,UP,A", True),
    ("23-data-hora", "UP,A" + ",DOWN" * 10 + ",A" + ",DOWN" * 3 + ",A,DOWN", True),
    ("20-atualizacao", "UP,A" + ",DOWN" * 10 + ",A" + ",DOWN" * 4 + ",A,DOWN,DOWN", True),
]


def illustrative_cover(w=320, h=450):
    """Generated art (a gradient with a frame), not a real cover."""
    import struct
    import zlib
    rows = []
    for y in range(h):
        row = bytearray(b"\x00")
        for x in range(w):
            edge = x < 10 or y < 10 or x >= w - 10 or y >= h - 10
            row += bytes((240, 240, 240)) if edge else bytes((40 + y * 150 // h, 70, 160 - y * 100 // h))
        rows.append(bytes(row))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))


# example sessions for the screenshots (simulated numbers, not measurements)
EXAMPLE_SESSIONS = (
    "inicio;plataforma;emulador;jogo;perfil;duracao_s;cpu_media_mhz;cpu_max_mhz;temp_inicio_c;temp_max_c;"
    "temp_fim_c;bateria_inicio;bateria_fim;carregando;protecao_termica;saida\n"
    "2026-10-02 13:00;GBA;gpsp;Celeste Classic.gba;economy;1800;1104;1200;44;55;54;90;83;0;0;0\n"
    "2026-10-02 13:40;GBA;gpsp;Celeste Classic.gba;balanced;1800;1390;1608;45;61;58;83;74;0;0;0\n"
    "2026-10-02 14:20;PS;pcsx_rearmed;Demo.cue;boost;2400;1880;2000;47;74;71;74;58;0;1;0\n")


def main():
    try:
        from PIL import Image
    except ImportError:
        sys.exit("pip install pillow")
    out = os.path.join(ROOT, "docs", "img")
    os.makedirs(out, exist_ok=True)
    only = set(sys.argv[1:])   # optional: names of the shots to redo
    for name, script, wizard_done in SHOTS:
        if only and name not in only:
            continue
        with tempfile.TemporaryDirectory() as t:
            dev = make_device(os.path.join(t, "dev"))
            sd = make_card(os.path.join(t, "sd"))
            for rel in ("Roms/SFC/Super Boss Gaiden (World).sfc", "Roms/MD/Old Towers (World).bin",
                        "Roms/GBA/Anguna (World).gba", "Roms/FC/Alter Ego (World).nes",
                        "Roms/GB/uCity (World).gb", "Roms/NGP/Homebrew (World).ngp",
                        "Roms/DOOM/freedoom1.wad", "Roms/PORTS/OpenTyrian.sh", "Roms/PORTS/SuperTux.sh"):
                write(os.path.join(sd, rel), "")
            if name == "15-senha-wifi":   # Wi-Fi on, nothing saved yet
                write(os.path.join(dev, "run/wpa_supplicant"), "")
            elif name in NET_SHOTS:
                write(os.path.join(dev, "run/wpa_supplicant"), "")
                write(os.path.join(dev, "netstate/status"), "wpa_state=COMPLETED\nssid=Casa\nip_address=192.168.0.23\n")
                write(os.path.join(dev, "netstate/networks"),
                      "network id / ssid / bssid / flags\n0\tCasa\tany\t[CURRENT]\n")
            if name in ("18-capas", "19-capas-opcoes"):
                write(os.path.join(sd, "Imgs/GBA/Celeste Classic (World).png"), illustrative_cover())
                write(os.path.join(sd, "Imgs/GBA/Anguna (World).png"), illustrative_cover())
            if name == "19-capas-opcoes":
                write(os.path.join(dev, "../tmp/scrape.status"),
                      "state=done\ndone=14\ntotal=14\nfound=11\nmissing=3\n")
            if name == "22-aplicativos":   # illustrative apps in the TrimUI format
                for d, label, desc in (("Apps/Notas", "Notas", "Bloco de notas simples."),
                                       ("../dev/usr/trimui/apps/musicplayer", "Music", "TRIMUI Music Player"),
                                       ("../dev/usr/trimui/apps/photoviewer", "Picture", "TRIMUI Picture Viewer")):
                    write(os.path.join(sd, d, "config.json"),
                          '{"label":"%s","launch":"launch.sh","description":"%s"}' % (label, desc))
                    write(os.path.join(sd, d, "launch.sh"), "#!/bin/sh\n")
            if name == "23-data-hora":
                write(os.path.join(dev, "usr/share/zoneinfo/America/Sao_Paulo"), "TZif")
            if name == "20-atualizacao":   # illustrative: a release newer than the card
                write(os.path.join(sd, "TriMux/VERSION"), "0.4.0\n")
                os.makedirs(os.path.join(sd, "TriMux.old"))
                os.makedirs(os.path.join(sd, "trimui.old"))
                write(os.path.join(dev, "../tmp/update.status"), "state=available\nversion=0.5.0\nerror=\npercent=0\n")
                write(os.path.join(dev, "../tmp/update.ini"),
                      "[release]\ntag = v0.5.0\nversion = 0.5.0\nprerelease = 1\n"
                      "pkg_name = TriMux-0.5.0-update.tar.gz\npkg_url = https://example.invalid/p.tar.gz\n"
                      "pkg_size = 30000000\nsha_url = https://example.invalid/p.sha256\n")
                write(os.path.join(dev, "../tmp/update-notes.txt"),
                      "TriMux 0.5.0 (exemplo)\n\nNotas da versão aparecem aqui, tiradas da página de lançamentos.")
            if name in ("16-registros", "17-sessoes"):
                write(os.path.join(sd, "TriMuxData/logs/perf/sessions.csv"), EXAMPLE_SESSIONS)
            if wizard_done:
                write(os.path.join(sd, "TriMuxData/config/trimux.ini"), "[general]\nwizard_done = 1\n" +
                      ("[time]\nzone = America/Sao_Paulo\n" if name == "23-data-hora" else ""))
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
