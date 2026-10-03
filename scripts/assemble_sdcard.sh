#!/bin/sh
# Assembles the card tree in build/sdcard from the repository skeleton and the
# build outputs. Nothing proprietary is included: no firmware files, ROMs or
# BIOS. Run inside the toolchain container (make docker-sdcard).
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT=$ROOT/build/sdcard
VERSION=$(cat VERSION)
for f in build/aarch64/trimuxctl build/aarch64/trimux-ui build/retroarch/retroarch; do
    [ -f "$f" ] || { echo "missing $f (run make docker-cross / docker-retroarch)" >&2; exit 1; }
done
ls build/cores/*.so >/dev/null 2>&1 || { echo "no cores in build/cores (run make docker-cores)" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"
cp -R sdcard/. "$OUT/"
TM=$OUT/TriMux
mkdir -p "$TM/bin" "$TM/retroarch/cores" "$TM/share/fonts" "$TM/licenses"
install -m 755 build/aarch64/trimuxctl build/aarch64/trimux-ui "$TM/bin/"
install -m 755 build/retroarch/retroarch "$TM/retroarch/"
install -m 644 build/cores/*.so "$TM/retroarch/cores/"
echo "$VERSION" > "$TM/VERSION"

# Fonts: DejaVu Sans (Bitstream Vera / DejaVu license, redistributable).
FONT_DIR=/usr/share/fonts/truetype/dejavu
cp "$FONT_DIR/DejaVuSans.ttf" "$TM/share/fonts/"
[ -f /usr/share/doc/fonts-dejavu-core/copyright ] && cp /usr/share/doc/fonts-dejavu-core/copyright "$TM/licenses/DejaVu.txt"

# Licenses of everything shipped (sources pinned in sources/sources.lock).
cp LICENSE "$TM/licenses/TriMux-MIT.txt"
cp sources/sources.lock "$TM/licenses/sources.lock"
grep -v '^#' sources/sources.lock | grep -v '^$' | while IFS='|' read -r name url commit license; do
    d=build/src/$name
    for lf in COPYING LICENSE LICENSE.txt LICENSE.TXT license.txt COPYING.txt docs/COPYING src/license.txt copyright; do
        if [ -f "$d/$lf" ]; then cp "$d/$lf" "$TM/licenses/$name.txt"; break; fi
    done
    [ -f "$TM/licenses/$name.txt" ] || echo "$name: $license ($url @ $commit)" > "$TM/licenses/$name.txt"
done
# PPSSPP links FFmpeg statically (the prebuilt libraries of its ffmpeg submodule)
{ cat build/src/ppsspp/ffmpeg/LICENSE.md; echo; cat build/src/ppsspp/ffmpeg/COPYING.LGPLv2.1; } > "$TM/licenses/ppsspp-ffmpeg.txt"
cp src/ui/third_party/STB_COMMIT "$TM/licenses/stb_truetype-commit.txt"
{ cat src/core/third_party/JSMN_LICENSE; echo; echo "commit $(cat src/core/third_party/JSMN_COMMIT)"; } > "$TM/licenses/jsmn.txt"
# Libraries bundled inside RetroArch for RetroAchievements and HTTPS.
cp build/src/retroarch/deps/rcheevos/LICENSE "$TM/licenses/rcheevos.txt"
{
    echo "mbedTLS, bundled in RetroArch (deps/mbedtls), used for HTTPS (RetroAchievements)."
    echo "SPDX-License-Identifier: Apache-2.0 (per its source headers)."
    echo
    cat /usr/share/common-licenses/Apache-2.0
} > "$TM/licenses/mbedtls.txt"

# Image filters (Settings > Emulators > <platform> > Look): three light GLSL
# presets from libretro/glsl-shaders that run on GLES (PowerVR GE8300).
SH=$TM/retroarch/shaders
mkdir -p "$SH/shaders"
for p in interpolation/sharp-bilinear-simple crt/zfast-crt handheld/zfast-lcd; do
    src=build/src/glsl_shaders/$p.glslp
    cp "$src" "$SH/"
    glsl=$(sed -n 's/^shader0 *= *//p' "$src" | tr -d '"\r')
    cp "$(dirname "$src")/$glsl" "$SH/$glsl"
done
{
    echo "GLSL shaders from https://github.com/libretro/glsl-shaders (commit in sources.lock):"
    echo "  sharp-bilinear-simple: rsn8887 (based on TheMaister), public domain"
    echo "  zfast-crt, zfast-lcd: Copyright (C) 2017 Greg Hogan (SoltanGris42), GPL-2.0-or-later"
    echo "The license notice is also at the top of each .glsl file."
    echo
    cat /usr/share/common-licenses/GPL-2
} > "$TM/licenses/glsl_shaders.txt"

# Game covers: CA certificates for HTTPS (the firmware ships none) and the
# arcade zip name -> title list from the FinalBurn Neo DAT.
cp /etc/ssl/certs/ca-certificates.crt "$TM/share/cacert.pem"
cp /usr/share/doc/ca-certificates/copyright "$TM/licenses/ca-certificates.txt"
python3 scripts/arcade_names.py "build/src/fbneo/dats/FinalBurn Neo (ClrMame Pro XML, Arcade only).dat" \
    "$TM/share/arcade-names.tsv"

# One folder per platform (first name in systems.ini), so users see where
# games go (except platforms marked create_folder = 0).
python3 - "$TM/share/systems.ini" "$OUT/Roms" <<'EOF'
import configparser, os, sys
cp = configparser.ConfigParser(interpolation=None, strict=False)
cp.read(sys.argv[1], encoding="utf-8")
for sec in cp.sections():
    if cp.get(sec, "create_folder", fallback="1") == "0":
        continue
    first = cp.get(sec, "folders", fallback=sec).split(",")[0].strip()
    os.makedirs(os.path.join(sys.argv[2], first), exist_ok=True)
EOF
mkdir -p "$OUT/Bios"
# prboom.wad is PrBoom's own GPL resource file (not game data); the core
# looks for it in the system (Bios) folder.
cp build/src/prboom/prboom.wad "$OUT/Bios/prboom.wad"
# PPSSPP's own system files (PSP replacement fonts in flash0, compat.ini,
# VFPU tables, shaders), from the pinned PPSSPP source. They live inside
# TriMux/ (emulators.ini system_dir) so online updates refresh them.
PSPSYS="$TM/retroarch/system/PPSSPP"
mkdir -p "$PSPSYS"
for a in flash0 vfpu shaders lang compat.ini compatvr.ini knownfuncs.ini langregion.ini infra-dns.json \
         ppge_atlas.zim ppge_atlas.meta font_atlas.zim font_atlas.meta asciifont_atlas.zim asciifont_atlas.meta; do
    cp -R "build/src/ppsspp/assets/$a" "$PSPSYS/"
done
cp docs/card/LEIA-ME.txt "$OUT/LEIA-ME.txt"
cp docs/card/Bios-LEIA-ME.txt "$OUT/Bios/LEIA-ME.txt"
cp docs/card/Roms-LEIA-ME.txt "$OUT/Roms/LEIA-ME.txt"

# Build information for support requests.
{
    echo "TriMux $VERSION"
    echo "git: $(git -c safe.directory="*" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "sources.lock sha256: $(sha256sum sources/sources.lock | cut -d' ' -f1)"
    echo "toolchain: Debian bullseye snapshot 20260824, $(aarch64-linux-gnu-gcc --version | head -1)"
} > "$TM/BUILDINFO.txt"

# Shell scripts must keep LF endings and be executable on the card.
find "$OUT" -name '*.sh' -exec chmod 755 {} +
chmod 755 "$OUT/trimui/app/MainUI"
du -sh "$OUT"
