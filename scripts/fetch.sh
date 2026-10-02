#!/bin/sh
# Download a pinned source archive into the local cache and verify its SHA-256.
# usage: fetch.sh <url> <sha256> <output-file>
set -eu
url=$1; sum=$2; out=$3
mkdir -p "$(dirname "$out")"
if [ -f "$out" ] && echo "$sum  $out" | sha256sum -c - >/dev/null 2>&1; then
    exit 0
fi
tmp="$out.part"
n=0
until curl -fL --retry 4 --retry-delay 2 -o "$tmp" "$url"; do
    n=$((n + 1)); [ $n -ge 3 ] && { echo "fetch failed: $url" >&2; exit 1; }
    sleep $((n * 4))
done
if ! echo "$sum  $tmp" | sha256sum -c - >/dev/null; then
    echo "SHA-256 mismatch for $url" >&2
    echo "expected $sum, got $(sha256sum "$tmp" | cut -d' ' -f1)" >&2
    rm -f "$tmp"; exit 1
fi
mv "$tmp" "$out"
