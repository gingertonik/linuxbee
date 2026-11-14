#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_pages.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Expected values: docs/spec/library-preferences.md, LP-MAIN. Runs with no
# display and with no message bus (the address given leads nowhere), so the
# desktop's own bus is not touched.
set -u
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-bus"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

out=$(timeout 8 "$app" --pages 2> /dev/null); rc=$?
[ "$rc" = 0 ] && pass pages-exit || fail "pages-exit rc=$rc"
[ "$out" = "Hotkeys
Library" ] && pass pages || fail "pages [$out]"

# It is an option like the others.
timeout 8 "$app" --pages --no-such-option > /dev/null 2> "$work/err"; rc=$?
[ "$rc" = 2 ] && pass mistake-still-a-mistake || fail "mistake-still-a-mistake rc=$rc"
grep -q -- '--pages' "$work/err" && pass usage-names-it || fail "usage-names-it [$(cat "$work/err")]"

# The others still work.
out=$(timeout 8 "$app" --services 2> /dev/null)
[ "$(printf '%s\n' "$out" | sed -n 2p)" = "monitored=0" ] && pass services || fail "services [$out]"
timeout 8 "$app" --commands 2> /dev/null | grep -qx 'edit.preferences' && pass commands || fail commands

echo "cli_pages: $fails failed"
[ "$fails" = 0 ]
