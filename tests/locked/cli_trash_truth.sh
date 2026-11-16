#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_trash_truth.sh /path/to/linuxbeectl
# Expected values: docs/spec/delete-files.md, FD-SEND and FD-CLI. The trash is a folder of this test's own.
set -u
bin="$1"
fails=0
work=$(mktemp -d)
trap 'chmod -R u+w "$work" 2> /dev/null; rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/xdg-data"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() {
    label=$1; expected=$2; got=$3
    if [ "$got" = "$expected" ]; then pass "$label"; else fail "$label: got [$got] want [$expected]"; fi
}

if [ "$(id -u)" = 0 ]; then
    echo "SKIP: this user may write anywhere"
    echo "cli_trash_truth: 0 failed"
    exit 0
fi
mkdir -p "$work/locked"
printf 'stays' > "$work/locked/a.flac"
chmod 555 "$work/locked"
out=$("$bin" trash "$work/locked/a.flac" 2> /dev/null); rc=$?
is "a folder that cannot be written" "3 failed: $work/locked/a.flac: its folder cannot be written" "$rc $out"
is "the file is where it was" "stays" "$(cat "$work/locked/a.flac")"
is "and has one name" "1" "$(stat -c %h "$work/locked/a.flac")"
is "nothing is in the trash" "0" "$(ls -A "$XDG_DATA_HOME/Trash/files" 2> /dev/null | wc -l | tr -d ' ')"

echo "cli_trash_truth: $fails failed"
[ "$fails" = 0 ]
