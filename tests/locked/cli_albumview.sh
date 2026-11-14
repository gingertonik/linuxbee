#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_albumview.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Expected values: docs/spec/album-view.md, AV-MAIN. Runs with no display
# and with no message bus (the address given leads nowhere).
set -u
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-bus"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

out=$(timeout 8 "$app" --parts 2> /dev/null); rc=$?
[ "$rc" = 0 ] && pass parts-exit || fail "parts-exit rc=$rc"
printf '%s\n' "$out" | grep -qx 'main-view=albums' && pass albums-by-default || fail "albums-by-default [$out]"
# After the lines of the earlier plans (later plans add theirs below it).
[ "$(printf '%s\n' "$out" | grep -x -A 1 'auto-playlists=[0-9]*' | tail -n 1)" = 'main-view=albums' ] && pass after-the-others || fail "after-the-others [$out]"
printf '%s\n' "$out" | grep -qx 'artist-browser=shown' && pass earlier-lines-stay || fail "earlier-lines-stay [$out]"

# The setting is read.
ini="$work/config/linuxbee/linuxbee/linuxbee.ini"
mkdir -p "$(dirname "$ini")"
printf '\n[view]\nmainPanel=tracks\n' >> "$ini"
out=$(timeout 8 "$app" --parts 2> /dev/null)
printf '%s\n' "$out" | grep -qx 'main-view=tracks' && pass tracks-when-told || fail "tracks-when-told [$out]"

# The window still comes up and can be pictured in both modes.
timeout 20 "$app" --screenshot "$work/tracks.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && [ -s "$work/tracks.png" ] && pass picture-tracks || fail "picture-tracks rc=$rc"
sed -i 's/^mainPanel=tracks$/mainPanel=albums/' "$ini"
timeout 20 "$app" --screenshot "$work/albums.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && [ -s "$work/albums.png" ] && pass picture-albums || fail "picture-albums rc=$rc"

echo "cli_albumview: $fails failed"
[ "$fails" = 0 ]
