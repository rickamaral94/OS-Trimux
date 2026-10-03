#!/bin/sh
# TriMux supervisor: prepares the device, runs the menu and the games, and
# returns to the stock firmware on request or after repeated failures.
# Started by /mnt/SDCARD/trimui/app/MainUI (see the comments there).

SD=${TRIMUX_SDCARD:-/mnt/SDCARD}
TM=$SD/TriMux
DATA=$SD/TriMuxData
TMP=${TRIMUX_TMP:-/tmp/trimux}
CTL=$TM/bin/trimuxctl
UI=$TM/bin/trimux-ui
LOG=$DATA/logs/trimux.log

export TRIMUX_SDCARD=$SD TRIMUX_TMP=$TMP
# Firmware libraries: SDL2 lives in /usr/trimui/lib, EGL/GLES and ALSA in /usr/lib.
export LD_LIBRARY_PATH=/usr/trimui/lib:/usr/lib:/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PATH=${TRIMUX_EXTRA_PATH:+$TRIMUX_EXTRA_PATH:}/usr/trimui/bin:/usr/bin:/bin:/usr/sbin:/sbin
export HOME=$DATA/retroarch

mkdir -p "$TMP" "$DATA/logs" "$DATA/state"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null) boot[I] $*" >> "$LOG"
}

# Before the card can go away (reboot, power off, stock launcher): stop the
# cover downloader after the current image and flush everything to the card,
# so the firmware's disk check at the next boot finds nothing half written.
settle_card() {
    "$CTL" scrape --stop >/dev/null 2>&1
    n=0
    while [ -f "$TMP/scrape.pid" ] && [ $n -lt 10 ]; do sleep 0.3; n=$((n + 1)); done
    sync
}

to_stock() {
    settle_card
    log "handing over to the stock launcher: $1"
    echo "$1" > "$TMP/to_stock"
    "$CTL" power default >/dev/null 2>&1
    exit 0
}

# --- safety checks -----------------------------------------------------------
[ -x "$CTL" ] || chmod +x "$CTL" "$UI" "$TM/retroarch/retroarch" 2>/dev/null
"$CTL" device || to_stock "not a TrimUI Brick Pro firmware"
"$CTL" boot begin
[ $? -eq 10 ] && to_stock "menu failed to start on 3 consecutive boots (safe mode)"
if [ -f "$DATA/state/in_game" ]; then
    log "previous session ended inside a game ($(cat "$DATA/state/in_game")); defaults restored"
    rm -f "$DATA/state/in_game"
fi
log "TriMux $(cat "$TM/VERSION" 2>/dev/null) starting, firmware $(cat /etc/version 2>/dev/null)"

# First boot of a freshly flashed card: the image is 1 GiB, the card is bigger.
# Grow the partition to the whole card (metadata only, nothing is moved), once;
# the marker TriMuxData/state/autogrow exists only in the image. On success the
# card is left read-only and the device reboots; the menu reports the new size.
if [ -f "$DATA/state/autogrow" ]; then
    "$CTL" card-grow --auto >/dev/null 2>&1
    case $? in 0|2) sync; reboot; sleep 30 ;; esac
fi

# --- firmware services, started exactly like the stock launcher does ---------
# keymon: volume/brightness keys, power button, suspend.
# trimui_inputd: creates the "TRIMUI Player1" gamepad from the controller MCU.
# hardwareservice: battery LED warnings, rumble.
# Not started on purpose: trimui_scened (its scene scripts rewrite CPU limits
# up to 2.0 GHz), trimui_osdd (in-game overlay that competes for buttons),
# musicserver (memory) and trimui_btmanager (started by "trimuxctl net apply"
# only when the user turns "TrimUI Bluetooth" on).
start_service() {
    name=$1; dir=$2; bin=$3
    if ! pgrep "$name" >/dev/null 2>&1; then
        (cd "$dir" && LD_LIBRARY_PATH=/usr/trimui/lib "./$bin" >/dev/null 2>&1 &)
    fi
}
if [ -d /usr/trimui/bin ]; then
    start_service keymon /usr/trimui/bin keymon
    start_service inputd /usr/trimui/bin trimui_inputd
    start_service hardwareservice /usr/trimui/bin hardwareservice
    # Audio route used by the stock runtrimui.sh for the TG4040 speaker path.
    tinymix set 9 1 >/dev/null 2>&1
    tinymix set 1 0 >/dev/null 2>&1
fi

# Conservative CPU policy for the menu, and the user's LED choice (if any).
"$CTL" power default >/dev/null 2>&1
"$CTL" leds apply >/dev/null 2>&1
# hardwareservice applies the stock LED settings when it starts, which can be
# after the line above: apply the user's choice again a few seconds later.
(sleep 5; "$CTL" leds apply) >/dev/null 2>&1 &
# keymon rewrites the stock settings file on every volume change and then
# re-applies the stock LED settings; this keeps the user's choice (only when
# "Controlar LEDs" is on) for as long as this supervisor runs.
"$CTL" leds keep $$ >/dev/null 2>&1 &
# Side switch: LEDs off / speaker mute, if the user assigned one of those.
"$CTL" switch >/dev/null 2>&1
# Network choices (Wi-Fi on/off, Bluetooth service, firmware SSH off unless
# enabled). In the background: starting wpa_supplicant must not delay the menu.
("$CTL" net apply >/dev/null 2>&1 &)
# Automatic covers (only if turned on): waits up to 90 s for Wi-Fi, pauses while
# a game runs, and stops on its own when everything is downloaded.
("$CTL" scrape --auto --wait 90 >/dev/null 2>&1 &)
# Internet time (only if "Ajustar pela internet" is on), once Wi-Fi connects.
("$CTL" time sync --auto --wait 90 >/dev/null 2>&1 &)
# Update check (only if "Verificar ao ligar" is on): asks GitHub for a newer
# release and tells the menu. Nothing is downloaded or installed by itself.
("$CTL" update check --auto --wait 60 >/dev/null 2>&1 &)

# --- main loop ---------------------------------------------------------------
crash_first=0
crash_count=0
while true; do
    # Time zone chosen in TriMux (Configurações › Sistema › Data e hora) for
    # the menu, games and apps; empty: the firmware's zone (/etc/localtime).
    zone=$("$CTL" time tz 2>/dev/null)
    if [ -n "$zone" ]; then export TZ="$zone"; else unset TZ; fi
    sh "$TM/scripts/premenu.sh" 2>/dev/null
    "$UI"
    rc=$?
    case $rc in
        10) # play: the launcher re-validates the request and applies limits first
            "$CTL" launch
            lrc=$?
            [ $lrc -ne 0 ] && log "game ended with status $lrc"
            ;;
        11) # app (TrimUI format): the request is re-validated by trimuxctl
            "$CTL" app || log "app request refused or failed"
            ;;
        20) to_stock "requested from the menu" ;;
        30) log "power off"; settle_card; touch /tmp/poweroff_flag; poweroff; sleep 30 ;;
        31) log "reboot"; settle_card; reboot; sleep 30 ;;
        40) log "card partition grow requested"
            # no redirection to the card here: an open log file would make the
            # read-only remount fail (trimuxctl logs the details itself)
            "$CTL" card-grow > "$TMP/card-grow.out" 2>&1
            case $? in 0|2) sync; reboot; sleep 30 ;; esac
            log "card grow not done: $(tail -1 "$TMP/card-grow.out" 2>/dev/null)"
            ;;
        0) ;;
        *)
            now=$(cut -d. -f1 /proc/uptime)
            if [ $((now - crash_first)) -gt 60 ]; then crash_first=$now; crash_count=0; fi
            crash_count=$((crash_count + 1))
            log "menu exited unexpectedly with status $rc ($crash_count in the last minute)"
            [ $crash_count -ge 3 ] && to_stock "menu crashed $crash_count times in a minute"
            sleep 1
            ;;
    esac
done
