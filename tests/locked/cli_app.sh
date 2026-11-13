#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer.
# Usage: cli_app.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# The application starts without a display and says which version it is.
set -u
ctl="$1"
app="${LINUXBEE_APP:?}"
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

out=$("$app" --version 2>/dev/null); rc=$?
want="linuxBee $("$ctl" --version 2>/dev/null | awk '{print $NF}')"
if [ "$rc" = 0 ] && [ "$out" = "$want" ]; then pass version; else fail "version: rc=$rc [$out] want [$want]"; fi

# It starts, shows its window and keeps running until told to stop.
QT_QPA_PLATFORM=offscreen "$app" >/dev/null 2>&1 &
pid=$!
sleep 1
if kill -0 "$pid" 2>/dev/null; then pass starts-and-stays-up; else fail starts-and-stays-up; fi
kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null

echo "cli_app: $fails failed"
[ "$fails" -eq 0 ]
