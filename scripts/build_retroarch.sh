#!/bin/sh
# Cross-compiles RetroArch (pinned in sources/sources.lock) for the Brick Pro.
# Video: GLES2/3 through SDL2 (the firmware's SDL2 provides the "mali" fbdev
# backend on top of the PowerVR EGL driver). Audio: ALSA. Input: linuxraw
# joypad (/dev/input/js*, created by the firmware's trimui_inputd).
# Networking, online updater, Qt, X11, Wayland, KMS and Vulkan are disabled.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
SRC=$ROOT/build/src/retroarch
OUT=$ROOT/build/retroarch
LOG=$ROOT/build/logs/retroarch.log
mkdir -p "$OUT" "$(dirname "$LOG")"
[ -d "$SRC" ] || { echo "missing RetroArch source (scripts/fetch_sources.sh retroarch)" >&2; exit 1; }
cd "$SRC"
git clean -qfdx 2>/dev/null || true
export CFLAGS="-O2 -mcpu=cortex-a53"
export CXXFLAGS="-O2 -mcpu=cortex-a53"
export PKG_CONFIG_PATH=/opt/aarch64/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig
{
./configure --host=aarch64-linux-gnu --prefix=/usr \
    --enable-sdl2 --enable-opengles --enable-opengles3 --enable-egl \
    --disable-x11 --disable-wayland --disable-kms --disable-vulkan --disable-mali_fbdev \
    --enable-alsa --disable-pulse --disable-jack --disable-oss --disable-tinyalsa \
    --disable-udev --disable-libusb --disable-dbus --disable-systemd --disable-hid \
    --enable-networking --disable-netplaydiscovery --disable-online_updater --disable-update_cores \
    --disable-update_core_info --disable-update_assets --enable-ssl \
    --enable-builtinmbedtls --disable-builtinbearssl --disable-discord --enable-cheevos \
    --disable-qt --disable-ffmpeg --disable-v4l2 --disable-freetype --disable-microphone \
    --disable-cdrom --disable-bluetooth --disable-materialui --disable-xmb --disable-ozone \
    --enable-rgui --enable-7zip --enable-zlib --enable-builtinzlib --enable-threads
make -j "${JOBS:-$(nproc)}"
} >"$LOG" 2>&1 || { echo "RetroArch build failed, see $LOG" >&2; tail -30 "$LOG" >&2; exit 1; }
aarch64-linux-gnu-strip -o "$OUT/retroarch" retroarch
grep -E 'Checking presence of package (sdl2|egl|glesv2)|HAVE_(SDL2|OPENGLES|EGL|ALSA)' config.log 2>/dev/null | head -20 >>"$LOG" || true
python3 "$ROOT/scripts/check_abi.py" "$OUT/retroarch"
echo "ok retroarch -> $(du -h "$OUT/retroarch" | cut -f1)"
