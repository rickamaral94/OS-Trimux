#!/bin/sh
# Fetches every source in sources/sources.lock at its pinned commit into
# build/src/<name>. Re-running is cheap: an existing checkout at the right
# commit is kept. Usage: fetch_sources.sh [name...]
set -eu
cd "$(dirname "$0")/.."
LOCK=sources/sources.lock
DEST=build/src
mkdir -p "$DEST"
want="$*"
grep -v '^#' "$LOCK" | grep -v '^$' | while IFS='|' read -r name url commit license; do
    if [ -n "$want" ]; then
        case " $want " in *" $name "*) ;; *) continue ;; esac
    fi
    dir="$DEST/$name"
    if [ -d "$dir/.git" ] && [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" = "$commit" ]; then
        echo "ok      $name"
        continue
    fi
    rm -rf "$dir"
    git init -q "$dir"
    git -C "$dir" remote add origin "$url"
    n=0
    until git -C "$dir" fetch -q --depth 1 origin "$commit"; do
        n=$((n + 1)); [ $n -ge 4 ] && { echo "fetch failed: $name" >&2; exit 1; }
        sleep $((n * 2))
    done
    git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
    [ "$(git -C "$dir" rev-parse HEAD)" = "$commit" ] || { echo "commit mismatch: $name" >&2; exit 1; }
    if [ -f "$dir/.gitmodules" ]; then
        git -C "$dir" submodule update -q --init --recursive --depth 1
    fi
    echo "fetched $name"
done
