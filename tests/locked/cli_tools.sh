#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_tools.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Expected values: docs/spec/tools-shell.md, TS-MAIN. Runs with no display.
set -u
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

timeout 20 "$app" --commands > "$work/commands.txt" 2> /dev/null; rc=$?
[ "$rc" = 0 ] && pass commands-exit || fail "commands-exit rc=$rc"
# Every part of the window has given its commands their behaviour.
for id in file.quit help.about view.navigator \
    file.scanFolders file.addFiles file.remove view.refresh edit.find track.sendToInbox \
    controls.playPause controls.stop controls.next controls.mute track.playNow track.queueNext track.queueLast \
    file.editTags file.organise tools.convert edit.preferences; do
    grep -qx "$id" "$work/commands.txt" && pass "has-$id" || fail "has-$id"
done
# One id per line, in catalogue order, and only commands that do something.
grep -qvE '^[a-z][A-Za-z0-9.]*$' "$work/commands.txt" && fail line-shape || pass line-shape
[ "$(sort "$work/commands.txt" | uniq -d | wc -l)" = 0 ] && pass no-duplicates || fail no-duplicates
grep -qx 'view.miniPlayer' "$work/commands.txt" && fail lists-unimplemented || pass lists-unimplemented
first=$(grep -n -x 'file.scanFolders' "$work/commands.txt" | cut -d: -f1)
second=$(grep -n -x 'edit.preferences' "$work/commands.txt" | cut -d: -f1)
third=$(grep -n -x 'controls.playPause' "$work/commands.txt" | cut -d: -f1)
[ "${first:-9}" -lt "${second:-0}" ] && [ "${second:-9}" -lt "${third:-0}" ] && pass catalogue-order || fail catalogue-order
# It only reports: the library is opened as usual, and no picture is asked for.
[ -n "$(find "$work/data" -name library.db 2>/dev/null)" ] && pass library-opened || fail library-opened

# The window still starts.
timeout 20 "$app" --screenshot "$work/window.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && [ -s "$work/window.png" ] && pass window-starts || fail "window-starts rc=$rc"

echo "cli_tools: $fails failed"
[ "$fails" = 0 ]
