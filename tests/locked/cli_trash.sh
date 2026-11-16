#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_trash.sh /path/to/linuxbeectl
# Expected values: docs/spec/delete-files.md, FD-CLI. The trash is a folder of this test's own.
set -u
bin="$1"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/xdg-data"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() {
    label=$1; expected=$2; got=$3
    if [ "$got" = "$expected" ]; then pass "$label"; else fail "$label: got [$got] want [$expected]"; fi
}

mkdir -p "$work/m/folder"
printf 'one' > "$work/m/one.flac"
printf 'two' > "$work/m/two.flac"
ln -s "$work/m/two.flac" "$work/m/link.flac"

out=$("$bin" trash "$work/m/one.flac" 2> "$work/err"); rc=$?
is "one file" "0 trashed: $work/m/one.flac -> $XDG_DATA_HOME/Trash/files/one.flac" "$rc $out"
is "one file: gone from its place" "no" "$([ -e "$work/m/one.flac" ] && echo yes || echo no)"
is "one file: in the trash" "one" "$(cat "$XDG_DATA_HOME/Trash/files/one.flac" 2> /dev/null)"
is "one file: nothing on standard error" "" "$(cat "$work/err")"

out=$("$bin" trash "$work/m/gone.flac" "$work/m/folder" "$work/m/link.flac" "$work/m/two.flac" 2> /dev/null); rc=$?
is "mixed: exit" 3 "$rc"
is "mixed: lines" "not there: $work/m/gone.flac|refused: $work/m/folder: a folder|refused: $work/m/link.flac: a link|trashed: $work/m/two.flac -> $XDG_DATA_HOME/Trash/files/two.flac|" "$(printf '%s\n' "$out" | tr '\n' '|')"
is "mixed: the folder stays" "yes" "$([ -d "$work/m/folder" ] && echo yes || echo no)"
is "mixed: the link stays" "yes" "$([ -L "$work/m/link.flac" ] && echo yes || echo no)"

"$bin" trash > /dev/null 2> "$work/err"; rc=$?
is "no file: usage" 2 "$rc"
grep -q '^usage: linuxbeectl trash FILE\.\.\.$' "$work/err" && pass usage-line || fail usage-line
"$bin" trash --permanent "$work/m/folder" > /dev/null 2>&1; rc=$?
is "there is no permanent delete" 2 "$rc"
is "and the folder is still there" "yes" "$([ -d "$work/m/folder" ] && echo yes || echo no)"

echo "cli_trash: $fails failed"
[ "$fails" = 0 ]
