#!/bin/sh
# ABI smoke test in a SIMULATED environment: runs the cross-built TriMux
# binaries, RetroArch and every core under qemu-aarch64 using the dynamic
# libraries of the official firmware (build/firmware/rootfs). It proves the
# binaries load and link against firmware v1.1.1; it does not exercise the
# PowerVR GPU, audio or the real controller, which only the device can do.
# Run inside the toolchain container after fetch_firmware.sh.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
FW=$ROOT/build/firmware/rootfs
OUT=$ROOT/build/smoke
[ -d "$FW/usr/trimui/lib" ] || { echo "run scripts/fetch_firmware.sh first" >&2; exit 1; }
mkdir -p "$OUT"
# LD_BIND_NOW makes the loader resolve every symbol up front: a missing one fails here.
Q="qemu-aarch64-static -L $FW -E LD_LIBRARY_PATH=/usr/trimui/lib:/usr/lib:/lib -E LD_BIND_NOW=1"
pass=0; fail=0
ok() { echo "PASS $1"; pass=$((pass + 1)); }
ko() { echo "FAIL $1"; fail=$((fail + 1)); }

# 1. trimuxctl runs and identifies the firmware it is given
mkdir -p "$OUT/sysroot/usr/trimui/bin" "$OUT/sysroot/etc"
cp "$FW/usr/trimui/bin/MainUI" "$OUT/sysroot/usr/trimui/bin/MainUI"
cp "$FW/etc/version" "$OUT/sysroot/etc/version"
if TRIMUX_SYSFS_ROOT=$OUT/sysroot $Q build/aarch64/trimuxctl device; then ok "trimuxctl device (firmware MainUI says Brick Pro)"; else ko "trimuxctl device"; fi

# 2. the menu links against the firmware's SDL2 2.30.8. The firmware build of
#    SDL only has the "mali" fbdev video backend (its "dummy" driver is not
#    usable), so under qemu SDL_Init is expected to fail: the check is that the
#    loader resolved every symbol (LD_BIND_NOW) and the program reached SDL_Init.
SD=$OUT/sd
rm -rf "$SD"
mkdir -p "$SD/TriMux/share/fonts" "$SD/TriMux/retroarch/cores" "$SD/Roms/GBA" "$SD/TriMuxData/config"
cp -R sdcard/TriMux/share/. "$SD/TriMux/share/"
cp /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf "$SD/TriMux/share/fonts/"
cp build/cores/gpsp_libretro.so "$SD/TriMux/retroarch/cores/"
touch "$SD/Roms/GBA/Homebrew (World).gba"
printf '[general]\nwizard_done = 1\n' > "$SD/TriMuxData/config/trimux.ini"
TRIMUX_LOG_STDERR=1 TRIMUX_SDCARD=$SD TRIMUX_TMP=$OUT/tmp SDL_VIDEODRIVER=dummy \
    $Q build/aarch64/trimux-ui --window 1024 768 --script "DOWN" > "$OUT/ui.log" 2>&1 || true
if grep -q "SDL_Init" "$OUT/ui.log" && ! grep -qi "symbol lookup error\|error while loading" "$OUT/ui.log"; then
    ok "trimux-ui loads with firmware libSDL2/libc (all symbols bound; no display under qemu)"
else
    ko "trimux-ui linkage (see $OUT/ui.log)"
fi
strings "$FW/usr/trimui/lib/libSDL2-2.0.so.0" | grep -E '^(mali|dummy|offscreen|kmsdrm|x11|wayland|evdev)$' > "$OUT/firmware-sdl-drivers.txt" || true

# 3. RetroArch starts and reports its features with the firmware's libraries
if $Q build/retroarch/retroarch --features > "$OUT/retroarch-features.txt" 2>&1; then
    ok "retroarch --features ($(grep -c 'yes' "$OUT/retroarch-features.txt") features enabled)"
else
    ko "retroarch --features"
fi
$Q build/retroarch/retroarch --version > "$OUT/retroarch-version.txt" 2>&1 || true

# 4. every core resolves its symbols against firmware glibc/libstdc++
aarch64-linux-gnu-gcc -O2 -o "$OUT/coreprobe" tools/coreprobe.c -ldl
if $Q "$OUT/coreprobe" build/cores/*.so > "$OUT/cores.txt" 2>&1; then
    ok "all $(grep -c '^ok' "$OUT/cores.txt") cores load with firmware libraries"
else
    ko "core loading (see $OUT/cores.txt)"
fi
cat "$OUT/cores.txt"
if python3 scripts/check_catalog.py "$OUT/cores.txt"; then ok "catalog matches core names/extensions"; else ko "catalog vs cores"; fi

# 5. "Arquivos pelo navegador": the firmware's own BusyBox httpd (aarch64,
#    under qemu) serves the page and runs the CGI. Without binfmt the CGI
#    script runs on the build machine's shell with trimuxctl built for it: this
#    checks how the firmware httpd passes requests, POST bodies and responses.
W=$OUT/web
rm -rf "$W"
mkdir -p "$W/sd/TriMux/bin" "$W/sd/Roms/GBA" "$W/www/cgi-bin" "$W/tmp"
cc -std=c11 -O2 -D_FILE_OFFSET_BITS=64 -o "$W/sd/TriMux/bin/trimuxctl" src/tools/trimuxctl.c src/core/*.c -lm
cp sdcard/TriMux/share/web/index.html "$W/www/index.html"
printf "#!/bin/sh\nexec '%s' webcgi\n" "$W/sd/TriMux/bin/trimuxctl" > "$W/www/cgi-bin/files"
chmod 755 "$W/www/cgi-bin/files"
echo hello > "$W/sd/Roms/GBA/Homebrew (World).gba"
TRIMUX_SDCARD=$W/sd TRIMUX_TMP=$W/tmp TRIMUX_SYSFS_ROOT=$W/dev \
    qemu-aarch64-static -L "$FW" "$FW/bin/busybox" httpd -f -p 127.0.0.1:18080 -h "$W/www" &
HP=$!
sleep 2
U=http://127.0.0.1:18080
F="$U/cgi-bin/files"
head -c 20971520 /dev/urandom > "$W/big.bin"
if curl -fs "$U/" | grep -q cgi-bin/files &&
    curl -fs "$F?op=list&path=Roms%2FGBA" | grep -q '"Homebrew (World).gba"' &&
    [ "$(curl -fs "$F?op=get&path=Roms/GBA/Homebrew%20(World).gba")" = hello ] &&
    curl -fs -H "Referer: $U/" -X POST --data-binary @"$W/big.bin" "$F?op=put&path=Roms/GBA&name=big.gba" >/dev/null &&
    cmp -s "$W/big.bin" "$W/sd/Roms/GBA/big.gba" &&
    [ "$(curl -s -o /dev/null -w '%{http_code}' -X POST "$F?op=del&path=Roms/GBA/big.gba")" = 403 ] &&
    [ "$(curl -s -o /dev/null -w '%{http_code}' -H "Referer: $U/" -X POST "$F?op=put&path=TriMux&name=x")" = 403 ] &&
    curl -fs -H "Referer: $U/" -X POST "$F?op=del&path=Roms/GBA/big.gba" >/dev/null && [ ! -e "$W/sd/Roms/GBA/big.gba" ]; then
    ok "firmware httpd: page, list, download, 20 MB upload intact, protections, delete"
else
    ko "firmware httpd file server"
fi
kill "$HP" 2>/dev/null || true
echo "smoke: $pass passed, $fail failed"
[ $fail -eq 0 ]
