#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# L-WAIT, BG-MONITOR: the folder monitor's scans are scans nobody is waiting for; the user's own are not.
# Usage: cli_scan_unasked.sh /path/to/linuxbeectl   (the program is not run: this reads the source)
set -u
SRC=$(cd "$(dirname "$0")/../.." && pwd)
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
code() { grep -v '^[[:space:]]*//' "$1"; }
bg="$SRC/src/ui/shell/backgroundshell.cpp"
n=$(code "$bg" | grep -c 'runner->scan(')
m=$(code "$bg" | grep -c 'nobodyWaits = true')
[ "$n" -ge 1 ] && [ "$m" = "$n" ] && pass monitor-scans-do-not-wait || fail "monitor-scans-do-not-wait: $n scans asked of the runner in backgroundshell.cpp, $m marked nobodyWaits"
for f in "$SRC"/src/ui/shell/libraryshell*.cpp "$SRC"/src/cli/*.cpp; do
    code "$f" | grep -q 'nobodyWaits' && fail "asked-for-scans-wait: $(basename "$f") marks a scan the user asked for as one nobody waits for"
done
[ "$fails" = 0 ] && pass asked-for-scans-wait
echo "cli_scan_unasked: $fails failed"
[ "$fails" -eq 0 ]
