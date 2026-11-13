#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_window_usage.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Expected values: docs/spec/main-window.md, W-MAIN, "Options". A mistake on
# the command line must end the program at once: a window that opens and
# waits would hang a gate that has no display and nobody to close it.
set -u
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

# mistake NAME ARGS...: exit 2 well within the time limit, nothing on standard
# output, a message on standard error, and nothing written anywhere.
mistake() {
    label=$1
    shift
    out=$(timeout 8 "$app" "$@" 2> "$work/err"); rc=$?
    if [ "$rc" = 124 ]; then
        fail "$label: still running after 8 seconds"
    elif [ "$rc" != 2 ] || [ -n "$out" ]; then
        fail "$label: rc=$rc (want 2) stdout=[$out] (want nothing)"
    elif [ ! -s "$work/err" ]; then
        fail "$label: nothing on standard error"
    elif [ -e "$work/config" ] || [ -e "$work/data" ]; then
        fail "$label: a file was written"
    else
        pass "$label"
    fi
}

mistake screenshot-without-file --screenshot
mistake screenshot-then-option --screenshot --version
mistake unknown-option --no-such-option
mistake unknown-before-known --no-such-option --screenshot "$work/a.png"
mistake screenshot-twice --screenshot "$work/a.png" --screenshot "$work/b.png"
[ ! -e "$work/a.png" ] && [ ! -e "$work/b.png" ] && pass no-picture-on-mistake || fail no-picture-on-mistake

# The message says how the program is used.
timeout 8 "$app" --screenshot > /dev/null 2> "$work/err"
grep -q -- '--screenshot FILE' "$work/err" && pass message-shows-usage || fail "message-shows-usage [$(cat "$work/err")]"

# What is not a mistake still works.
out=$(timeout 8 "$app" --version 2> /dev/null); rc=$?
case "$out" in "linuxBee "[0-9]*) [ "$rc" = 0 ] && pass version || fail "version rc=$rc" ;; *) fail "version [$out]" ;; esac
timeout 20 "$app" --screenshot "$work/ok.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && [ -s "$work/ok.png" ] && pass screenshot || fail "screenshot rc=$rc"
# Words that are not options are files handed over by the desktop (Exec=linuxbee %F): not a mistake.
timeout 20 "$app" "$work/some song.flac" --screenshot "$work/ok2.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && [ -s "$work/ok2.png" ] && pass file-argument || fail "file-argument rc=$rc"
timeout 20 "$app" --screenshot "$work/ok3.png" "$work/some song.flac" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && [ -s "$work/ok3.png" ] && pass file-argument-after || fail "file-argument-after rc=$rc"

echo "cli_window_usage: $fails failed"
[ "$fails" = 0 ]
