#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_library_names.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/library.md, L-NAMES and L-CLI.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() {
    label=$1; want=$2; got=$3
    if [ "$got" = "$want" ]; then pass "$label"; else fail "$label: got [$got] want [$want]"; fi
}
db="$work/lib.db"
mkdir -p "$work/music/a" "$work/music/b"
cp "$data/basic.flac" "$work/music/a/one.flac"

# Nothing odd: the one line, as before.
out=$("$bin" library --db "$db" scan --now 2026-11-13T12:00:00Z "$work/music" 2>/dev/null); rc=$?
is plain-rc 0 "$rc"
is plain "added 1, updated 0, unchanged 0, missing 0, found again 0, not music 0" "$out"

bad1=$(printf 'caf\351.flac'); bad2=$(printf '\377.mp3')
if ! : > "$work/music/a/$bad1" 2>/dev/null; then
    echo "SKIP this file system refuses names that are not valid UTF-8"
    echo "cli_library_names: $fails failed"
    [ "$fails" = 0 ]; exit
fi
: > "$work/music/a/$bad2"
: > "$work/music/b/$bad1"
out=$("$bin" library --db "$db" scan --now 2026-11-13T12:00:00Z "$work/music" 2> "$work/err"); rc=$?
is names-rc 0 "$rc"
is names-stdout "added 0, updated 0, unchanged 1, missing 0, found again 0, not music 0
passed over 3 names that are not valid UTF-8, in:
  $work/music/a
  $work/music/b" "$out"
is names-stderr "" "$(cat "$work/err")"
is still-one-track 1 "$("$bin" library --db "$db" count 2>/dev/null)"

rm "$work/music/a/$bad2" "$work/music/b/$bad1"
out=$("$bin" library --db "$db" scan --now 2026-11-13T12:00:00Z "$work/music" 2>/dev/null)
is one-name "added 0, updated 0, unchanged 1, missing 0, found again 0, not music 0
passed over 1 name that is not valid UTF-8, in:
  $work/music/a" "$out"

echo "cli_library_names: $fails failed"
[ "$fails" = 0 ]
