#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_parts.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Expected values: docs/spec/artist-browser.md, AB-MAIN. Runs with no display
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
printf '%s\n' "$out" | grep -qx 'artist-browser=shown' && pass shown-by-default || fail "shown-by-default [$out]"

# The setting is read.
ini="$work/config/linuxbee/linuxbee/linuxbee.ini"
mkdir -p "$(dirname "$ini")"
printf '\n[browser]\nartistsShown=false\n' >> "$ini"
out=$(timeout 8 "$app" --parts 2> /dev/null)
printf '%s\n' "$out" | grep -qx 'artist-browser=hidden' && pass hidden-when-told || fail "hidden-when-told [$out]"

# It is an option like the others.
timeout 8 "$app" --parts --no-such-option > /dev/null 2> "$work/err"; rc=$?
[ "$rc" = 2 ] && pass mistake-still-a-mistake || fail "mistake-still-a-mistake rc=$rc"
grep -q -- '--parts' "$work/err" && pass usage-names-it || fail "usage-names-it [$(cat "$work/err")]"

# The others still work.
timeout 8 "$app" --pages 2> /dev/null | grep -qx 'Library' && pass pages || fail pages
timeout 8 "$app" --commands 2> /dev/null | grep -qx 'edit.preferences' && pass commands || fail commands

echo "cli_parts: $fails failed"
[ "$fails" = 0 ]
