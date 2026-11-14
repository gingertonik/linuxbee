#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_autoplaylists.sh /path/to/linuxbeectl   (LINUXBEE_APP is the application)
# Expected values: docs/spec/auto-playlist-nodes.md, AN-MAIN.
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
printf '%s\n' "$out" | grep -qx 'auto-playlists=0' && pass none-at-first || fail "none-at-first [$out]"
printf '%s\n' "$out" | grep -qx 'artist-browser=shown' && pass the-other-line-stays || fail "the-other-line-stays [$out]"

# The folder is beside the playlists' folder.
lists=$(find "$work/data" -type d -name playlists | head -n 1)
[ -n "$lists" ] && pass playlists-folder || fail "playlists-folder: none under $work/data"
autos="$(dirname "${lists:-$work/data/x}")/auto-playlists"
mkdir -p "$autos"
json='{"version":1,"rules":{"match":"all","rules":[]},"limit":null,"selectedBy":"random","filterDuplicates":false}'
printf '%s' "$json" > "$autos/Everything.lbauto"
printf '%s' "$json" > "$autos/Again.lbauto"
printf 'x' > "$autos/notes.txt"
out=$(timeout 8 "$app" --parts 2> /dev/null)
printf '%s\n' "$out" | grep -qx 'auto-playlists=2' && pass two-found || fail "two-found [$out]"

timeout 8 "$app" --commands 2> /dev/null | grep -qx 'file.playlists.new' && pass commands || fail commands

echo "cli_autoplaylists: $fails failed"
[ "$fails" = 0 ]
