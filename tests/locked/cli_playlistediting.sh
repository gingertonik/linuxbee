#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_playlistediting.sh /path/to/linuxbeectl   (LINUXBEE_APP is the application)
# Expected values: docs/spec/playlist-editing.md, PE-MAIN.
set -u
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-bus"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

out=$(timeout 8 "$app" --commands 2> /dev/null); rc=$?
[ "$rc" = 0 ] && pass commands-exit || fail "commands-exit rc=$rc"
printf '%s\n' "$out" | grep -qx 'file.playlists.new' && pass new-playlist-works || fail "new-playlist-works"
printf '%s\n' "$out" | grep -qx 'edit.preferences' && pass the-others-still-work || fail the-others-still-work
timeout 8 "$app" --parts 2> /dev/null | grep -qx 'artist-browser=shown' && pass parts || fail parts

echo "cli_playlistediting: $fails failed"
[ "$fails" = 0 ]
