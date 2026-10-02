#!/bin/sh
# Called by the stock runtrimui.sh right after trimui/app/MainUI returns.
# Exit 1 hands control to the stock TrimUI launcher; exit 0 re-runs TriMux.
[ -f /tmp/trimux/to_stock ] && exit 1
[ -f /mnt/SDCARD/TriMux/VERSION ] || exit 1
exit 0
