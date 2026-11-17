#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_background.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Expected values: docs/spec/background-shell.md, BG-MAIN. Runs with no
# display. Where a session bus is needed it is a private one
# (dbus-run-session, a required tool here); the desktop's own is not touched.
set -u
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v dbus-run-session > /dev/null 2>&1 || { echo "FAIL dbus-run-session is required for this test"; exit 1; }

# With a session bus: on it.
out=$(timeout 8 dbus-run-session -- "$app" --services 2> /dev/null); rc=$?
[ "$rc" = 0 ] && pass services-exit || fail "services-exit rc=$rc"
[ "$(printf '%s\n' "$out" | sed -n 1p)" = "mpris=on" ] && pass mpris-on || fail "mpris-on [$out]"
[ "$(printf '%s\n' "$out" | sed -n 2p)" = "monitored=0" ] && pass monitored-none || fail "monitored-none [$out]"
[ "$(printf '%s\n' "$out" | grep -c .)" = 2 ] && pass two-lines || fail "two-lines [$out]"

# Without one: off, and that is not a failure.
out=$(DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-bus" timeout 8 "$app" --services 2> /dev/null); rc=$?
[ "$rc" = 0 ] && pass no-bus-exit || fail "no-bus-exit rc=$rc"
[ "$(printf '%s\n' "$out" | sed -n 1p)" = "mpris=off" ] && pass mpris-off || fail "mpris-off [$out]"
DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-bus" timeout 8 "$app" --screenshot "$work/window.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && [ -s "$work/window.png" ] && pass no-bus-window || fail "no-bus-window rc=$rc"

# The folders chosen are read from the settings: one, and several.
ini="$work/config/linuxbee/linuxbee.ini"
mkdir -p "$work/in1" "$work/in2"
cp "$ini" "$work/ini.before"
printf '\n[library]\nmonitored=%s\n' "$work/in1" >> "$ini"
out=$(timeout 8 dbus-run-session -- "$app" --services 2> /dev/null)
[ "$(printf '%s\n' "$out" | sed -n 2p)" = "monitored=1" ] && pass monitored-one || fail "monitored-one [$out]"
cp "$work/ini.before" "$ini"
printf '\n[library]\nmonitored=%s, %s\n' "$work/in1" "$work/in2" >> "$ini"
out=$(timeout 8 dbus-run-session -- "$app" --services 2> /dev/null)
[ "$(printf '%s\n' "$out" | sed -n 2p)" = "monitored=2" ] && pass monitored-two || fail "monitored-two [$out]"

# Two at once, each with a profile of its own (main-window.md, W-ONE): the second starts, with or without
# the bus name. (Two with one profile: the second is refused; cli_app_profile.sh.)
mkdir -p "$work/other"
timeout 30 dbus-run-session -- sh -c '
    timeout 8 "$1" --screenshot "$2/hold.png" > /dev/null 2>&1 &
    first=$!
    sleep 0.15
    XDG_CONFIG_HOME="$2/other/config" XDG_DATA_HOME="$2/other/data" XDG_CACHE_HOME="$2/other/cache" timeout 8 "$1" --services 2> /dev/null
    wait $first
' sh "$app" "$work" > "$work/second.txt" 2> /dev/null
grep -q '^mpris=' "$work/second.txt" && pass second-runs || fail "second-runs [$(cat "$work/second.txt")]"

echo "cli_background: $fails failed"
[ "$fails" = 0 ]
