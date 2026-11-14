#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_playback.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Expected values: docs/spec/playback-shell.md, PS-MAIN. Runs with no display
# and makes no sound: nothing is played.
set -u
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
ini="$work/config/linuxbee/linuxbee/linuxbee.ini"

timeout 20 "$app" --screenshot "$work/window.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && pass start-exit || fail "start-exit rc=$rc"
# The player's state is stored on the way out.
grep -q '^\[player\]$' "$ini" 2>/dev/null && pass player-group || fail player-group
grep -q '^volume=100$' "$ini" 2>/dev/null && pass volume-stored || fail volume-stored
grep -q '^repeat=off$' "$ini" 2>/dev/null && pass repeat-stored || fail repeat-stored

# What was stored is used and kept by the next start.
sed -i 's/^volume=100$/volume=40/' "$ini"
timeout 20 "$app" --screenshot "$work/window2.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && pass second-start || fail "second-start rc=$rc"
grep -q '^volume=40$' "$ini" 2>/dev/null && pass volume-kept || fail volume-kept

# A machine with no sound device still starts and leaves cleanly.
XDG_RUNTIME_DIR="$work/no-runtime" PULSE_SERVER="$work/no-pulse" PIPEWIRE_REMOTE="$work/no-pipewire" \
    timeout 20 "$app" --screenshot "$work/window3.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && pass no-device-start || fail "no-device-start rc=$rc"
[ -s "$work/window3.png" ] && pass no-device-window || fail no-device-window

echo "cli_playback: $fails failed"
[ "$fails" = 0 ]
