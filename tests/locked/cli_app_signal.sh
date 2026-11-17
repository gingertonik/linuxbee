#!/bin/sh
# LOCKED ACCEPTANCE TEST (the application ends cleanly when it is told to by a signal). Written by the reviewer.
# Usage: cli_app_signal.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Expected values: docs/spec/main-window.md (W-SIGNAL). Everything happens in a folder of this test's own.
set -u
app="${LINUXBEE_APP:?}"
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-signal.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT

# one NAME SIGNAL: start the application in a box of its own, send the signal, see how it ended.
one() {
    name=$1; sig=$2
    box="$work/$name"; mkdir -p "$box/config" "$box/data" "$box/cache" "$box/tmp"
    XDG_CONFIG_HOME="$box/config" XDG_DATA_HOME="$box/data" XDG_CACHE_HOME="$box/cache" TMPDIR="$box/tmp" \
        QT_QPA_PLATFORM=offscreen "$app" >/dev/null 2>"$box/stderr" &
    pid=$!
    # Up when its library file is there; at most five seconds.
    n=0; while [ $n -lt 50 ] && [ -z "$(find "$box/data" -name library.db 2>/dev/null)" ]; do sleep 0.1; n=$((n + 1)); done
    sleep 0.5
    if kill -0 "$pid" 2>/dev/null; then pass "$name-up"; else fail "$name-up"; return; fi
    kill "-$sig" "$pid"
    # Gone within five seconds, by itself.
    n=0; while [ $n -lt 50 ] && kill -0 "$pid" 2>/dev/null; do sleep 0.1; n=$((n + 1)); done
    if kill -0 "$pid" 2>/dev/null; then fail "$name-ended-in-time"; kill -9 "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; return; fi
    wait "$pid"; rc=$?
    # W-SIGNAL: an ordinary exit, status 0: not "killed by the signal" (128 + its number).
    [ "$rc" = 0 ] && pass "$name-exit-0" || fail "$name-exit-0: status $rc"
    # W-SIGNAL: what an ordinary exit saves was saved: the settings file is there and holds something.
    ini=$(find "$box/config" -name linuxbee.ini -size +0 2>/dev/null | head -1)
    [ -n "$ini" ] && pass "$name-settings-saved" || fail "$name-settings-saved"
    # W-SIGNAL: and what an ordinary exit removes was removed: no folder of this run is left behind.
    left=$( { ls -A "$box/tmp"; find "$box/cache" -name 'run-*'; } 2>/dev/null | tr '\n' ' ')
    [ -z "$left" ] && pass "$name-nothing-left" || fail "$name-nothing-left: $left"
}

one term TERM
one int INT
one hup HUP

echo "cli_app_signal: $fails failed"
[ "$fails" -eq 0 ]
