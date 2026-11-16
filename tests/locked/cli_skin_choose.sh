#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_skin_choose.sh /path/to/linuxbeectl   (LINUXBEE_APP is the application)
# Expected values: docs/spec/skins.md, SC-MAIN.
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
for id in view.skins.choose view.skins.add view.skins.none; do
    printf '%s\n' "$out" | grep -qx "$id" && pass "$id" || fail "$id"
done
printf '%s\n' "$out" | grep -qx 'edit.preferences' && pass the-others-still-work || fail the-others-still-work
[ "$(printf '%s\n' "$out" | sort | uniq -d | wc -l)" = 0 ] && pass no-duplicates || fail no-duplicates
timeout 8 "$app" --parts 2> /dev/null | grep -qx 'skin=none' && pass parts || fail parts
# Starting makes no skins folder: it is made when a skin is added.
[ -z "$(find "$work" -type d -name skins 2> /dev/null)" ] && pass no-folder-yet || fail no-folder-yet

echo "cli_skin_choose: $fails failed"
[ "$fails" = 0 ]
