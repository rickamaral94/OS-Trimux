#!/bin/sh
# Cross-compiles the pinned libretro cores for the Brick Pro (Cortex-A53).
# Runs inside the toolchain container (make docker-cores).
# Usage: build_cores.sh [name...]   (default: all)
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
SRC=$ROOT/build/src
OUT=$ROOT/build/cores
LOGS=$ROOT/build/logs
mkdir -p "$OUT" "$LOGS"
JOBS=${JOBS:-$(nproc)}
CROSS=aarch64-linux-gnu-
# Tuning only; each core's makefile adds its own optimisation flags.
export CFLAGS="-mcpu=cortex-a53 -fno-plt"
export CXXFLAGS="-mcpu=cortex-a53 -fno-plt"
MK="CC=${CROSS}gcc CXX=${CROSS}g++ AR=${CROSS}ar STRIP=${CROSS}strip"

# name|subdir|makefile|extra make arguments|output file
RECIPES='fceumm|.|Makefile.libretro|platform=unix|fceumm_libretro.so
nestopia|libretro|Makefile|platform=unix|nestopia_libretro.so
snes9x2005|.|Makefile|platform=unix USE_BLARGG_APU=1|snes9x2005_plus_libretro.so
supafaust|.|Makefile|platform=unix|mednafen_supafaust_libretro.so
gambatte|.|Makefile|platform=unix|gambatte_libretro.so
gpsp|.|Makefile|platform=unix UNAME=aarch64 CPU_ARCH=arm64 HAVE_DYNAREC=1 MMAP_JIT_CACHE=1|gpsp_libretro.so
picodrive|.|Makefile.libretro|platform=aarch64|picodrive_libretro.so
genesis_plus_gx|.|Makefile.libretro|platform=unix|genesis_plus_gx_libretro.so
pce_fast|.|Makefile|platform=unix|mednafen_pce_fast_libretro.so
pcsx_rearmed|.|Makefile.libretro|platform=h5|pcsx_rearmed_libretro.so
race|.|Makefile|platform=unix|race_libretro.so
wswan|.|Makefile|platform=unix|mednafen_wswan_libretro.so
handy|.|Makefile|platform=unix|handy_libretro.so
stella2014|.|Makefile|platform=unix|stella2014_libretro.so
prosystem|.|Makefile|platform=unix|prosystem_libretro.so
fbneo|src/burner/libretro|Makefile|platform=unix|fbneo_libretro.so
mgba|cmake|-|-|mgba_libretro.so
prboom|.|Makefile|platform=unix|prboom_libretro.so
tyrquake|.|Makefile|platform=unix|tyrquake_libretro.so
nxengine|.|Makefile|platform=unix|nxengine_libretro.so
mupen64plus_next|.|Makefile|platform=unix ARCH=aarch64 WITH_DYNAREC=aarch64 FORCE_GLES3=1|mupen64plus_next_libretro.so
flycast|cmake-new|-|-|flycast_libretro.so
ppsspp|cmake-ppsspp|-|-|ppsspp_libretro.so'

want="$*"
failed=""
echo "$RECIPES" | while IFS='|' read -r name sub mkfile extra out; do
    if [ -n "$want" ]; then
        case " $want " in *" $name "*) ;; *) continue ;; esac
    fi
    [ -d "$SRC/$name" ] || { echo "missing source $name (run scripts/fetch_sources.sh)" >&2; exit 1; }
    log="$LOGS/core-$name.log"
    echo "building $name ..."
    if [ "$sub" = "cmake" ]; then
        b=$ROOT/build/obj/$name
        rm -rf "$b"
        if ! { cmake -S "$SRC/$name" -B "$b" -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/cmake-aarch64.cmake \
                 -DCMAKE_BUILD_TYPE=Release -DBUILD_LIBRETRO=ON -DBUILD_QT=OFF -DBUILD_SDL=OFF \
                 -DBUILD_SHARED=OFF -DBUILD_STATIC=OFF -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF \
                 -DUSE_FFMPEG=OFF -DUSE_ZLIB=OFF -DUSE_MINIZIP=OFF -DUSE_PNG=OFF -DUSE_LIBZIP=OFF \
                 -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_LUA=OFF -DUSE_EDITLINE=OFF -DUSE_EPOXY=OFF \
                 -DUSE_DISCORD_RPC=OFF -DENABLE_SCRIPTING=OFF -DSKIP_GIT=ON \
              && cmake --build "$b" -j "$JOBS"; } >"$log" 2>&1; then
            echo "FAILED $name (see $log)"; continue
        fi
        cp "$b/$out" "$OUT/$out"
    elif [ "$sub" = "cmake-new" ]; then
        # Flycast needs CMake 3.22+ (the toolchain's /opt/cmake-3.28); GLES 3, no Vulkan
        b=$ROOT/build/obj/$name
        rm -rf "$b"
        if ! { /opt/cmake-3.28/bin/cmake -S "$SRC/$name" -B "$b" -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/cmake-aarch64.cmake \
                 -DCMAKE_BUILD_TYPE=Release -DLIBRETRO=ON -DUSE_GLES=ON -DUSE_VULKAN=OFF -DUSE_OPENMP=OFF \
              && /opt/cmake-3.28/bin/cmake --build "$b" -j "$JOBS"; } >"$log" 2>&1; then
            echo "FAILED $name (see $log)"; continue
        fi
        cp "$b/$out" "$OUT/$out"
    elif [ "$sub" = "cmake-ppsspp" ]; then
        # PPSSPP: libretro core, OpenGL ES (no Vulkan), FFmpeg from its own
        # pinned submodule (prebuilt static libraries for linux/aarch64)
        b=$ROOT/build/obj/$name
        rm -rf "$b"
        if ! { /opt/cmake-3.28/bin/cmake -S "$SRC/$name" -B "$b" -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/cmake-aarch64.cmake \
                 -DCMAKE_BUILD_TYPE=Release -DLIBRETRO=ON -DUSING_GLES2=ON -DUSING_EGL=OFF -DUSING_X11_VULKAN=OFF \
                 -DVULKAN=OFF -DUSE_FFMPEG=ON -DUSE_SYSTEM_FFMPEG=OFF -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF \
                 -DUSE_SYSTEM_LIBZIP=OFF -DUSE_SYSTEM_SNAPPY=OFF -DUSE_SYSTEM_ZSTD=OFF -DUSE_SYSTEM_LIBPNG=OFF \
                 -DHEADLESS=OFF -DUNITTEST=OFF -DSIMULATOR=OFF \
              && /opt/cmake-3.28/bin/cmake --build "$b" -j "$JOBS"; } >"$log" 2>&1; then
            echo "FAILED $name (see $log)"; continue
        fi
        cp "$(find "$b" -name "$out" | head -n 1)" "$OUT/$out"
    else
        d="$SRC/$name/$sub"
        # shellcheck disable=SC2086
        make -C "$d" -f "$mkfile" $MK $extra clean >/dev/null 2>&1 || true
        # shellcheck disable=SC2086
        if ! make -C "$d" -f "$mkfile" $MK $extra -j "$JOBS" >"$log" 2>&1; then
            echo "FAILED $name (see $log)"; continue
        fi
        cp "$d/$out" "$OUT/$out"
    fi
    ${CROSS}strip --strip-unneeded "$OUT/$out"
    echo "ok $name -> $(du -h "$OUT/$out" | cut -f1)"
done
# Every core must load on glibc 2.33 / GCC 10 runtime (firmware v1.1.1).
python3 "$ROOT/scripts/check_abi.py" "$OUT"/*.so
