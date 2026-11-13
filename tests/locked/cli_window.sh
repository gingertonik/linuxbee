#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_window.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Expected values: docs/spec/main-window.md, W-MAIN. Runs with no display, in
# a temporary configuration folder.
set -u
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

out=$("$app" --version 2>/dev/null); rc=$?
case "$out" in "linuxBee "[0-9]*) [ "$rc" = 0 ] && pass version || fail "version rc=$rc" ;; *) fail "version [$out]" ;; esac
[ ! -e "$work/config" ] && pass version-writes-nothing || fail version-writes-nothing

# --screenshot FILE: the window as a picture, then exit
timeout 20 "$app" --screenshot "$work/window.png" >/dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && pass screenshot-exit || fail "screenshot-exit rc=$rc"
if [ "$(head -c 4 "$work/window.png" 2>/dev/null | od -An -c | tr -d ' ')" = '211PNG' ]; then
    pass screenshot-is-png; else fail screenshot-is-png; fi
size=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "$work/window.png" 2>/dev/null)
[ "$size" = "1200,760" ] && pass screenshot-size || fail "screenshot-size [$size]"
# not one flat colour: something was drawn
colours=$(ffmpeg -nostdin -v error -i "$work/window.png" -vf scale=64:64 -f rawvideo -pix_fmt gray - 2>/dev/null | od -An -v -tu1 | tr -s ' ' '\n' | sort -u | grep -c .)
[ "${colours:-0}" -ge 3 ] && pass screenshot-has-content || fail "screenshot-has-content ($colours shades)"
timeout 20 "$app" --screenshot "$work/no-such-folder/window.png" >/dev/null 2>&1; rc=$?
[ "$rc" = 3 ] && pass screenshot-unwritable || fail "screenshot-unwritable rc=$rc"

# the settings file is made in the configuration folder, and the layout is in it after a run
ini=$(find "$work/config" -name linuxbee.ini 2>/dev/null | head -n 1)
[ -n "$ini" ] && pass settings-file || fail settings-file
grep -q '^\[window\]' "${ini:-/dev/null}" 2>/dev/null && pass layout-saved || fail layout-saved
# a hotkey changed by hand in the file is honoured: it starts and stays up
printf '\n[hotkeys]\ncontrols.stop=Ctrl+J\n' >> "${ini:-$work/none.ini}"
timeout 20 "$app" --screenshot "$work/again.png" >/dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && pass starts-with-custom-hotkeys || fail "starts-with-custom-hotkeys rc=$rc"
grep -q '^controls.stop=Ctrl+J' "${ini:-/dev/null}" 2>/dev/null && pass custom-hotkey-kept || fail custom-hotkey-kept

echo "cli_window: $fails failed"
[ "$fails" -eq 0 ]
