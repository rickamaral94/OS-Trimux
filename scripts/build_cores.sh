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
mgba|cmake|-|-|mgba_libretro.so'

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
