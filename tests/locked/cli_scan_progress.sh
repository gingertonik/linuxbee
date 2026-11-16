#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_scan_progress.sh /path/to/linuxbeectl   (LINUXBEE_APP is the application)
# Expected values: docs/spec/scan-runner.md, SP-MAIN.
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
printf '%s\n' "$out" | grep -qx 'file.scanStop' && pass stop-is-there || fail stop-is-there
printf '%s\n' "$out" | grep -qx 'file.scanFolders' && pass the-others-still-work || fail the-others-still-work
[ "$(printf '%s\n' "$out" | sort | uniq -d | wc -l)" = 0 ] && pass no-duplicates || fail no-duplicates

echo "cli_scan_progress: $fails failed"
[ "$fails" = 0 ]
