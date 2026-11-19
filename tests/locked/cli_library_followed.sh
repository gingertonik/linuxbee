#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_library_followed.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/library.md, L-FOLLOW and L-CLI.
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
none=", missing 0, found again 0, not music 0"
mkdir -p "$work/music/a" "$work/music/b"
cp "$data/basic.flac" "$work/music/a/one.flac"
cp "$data/basic.mp3" "$work/music/a/two.mp3"
scan() { "$bin" library --db "$db" scan --now "$1" "$work/music" 2>/dev/null; }

out=$(scan 2026-11-13T12:00:00Z); rc=$?
is first-rc 0 "$rc"
is first "added 2, updated 0, unchanged 0$none" "$out"

# Another program renames one file. The scan follows it, and says so on a line of its own.
mv "$work/music/a/one.flac" "$work/music/b/uno.flac"
out=$(scan 2026-12-01T00:00:00Z); rc=$?
is one-rc 0 "$rc"
is one "added 0, updated 0, unchanged 1$none
followed 1 file that was moved" "$out"
is one-count 2 "$("$bin" library --db "$db" count)"
is one-list "$work/music/a/two.mp3 | 2026-11-13T12:00:00
$work/music/b/uno.flac | 2026-11-13T12:00:00" "$("$bin" library --db "$db" list --format '<URL> | <Date Added>')"

# Nothing moved: the one line, as before.
out=$(scan 2026-12-02T00:00:00Z)
is quiet "added 0, updated 0, unchanged 2$none" "$out"

# Two files moved.
mv "$work/music/a/two.mp3" "$work/music/b/zwei.mp3"
mv "$work/music/b/uno.flac" "$work/music/a/eins.flac"
out=$(scan 2026-12-03T00:00:00Z)
is two "added 0, updated 0, unchanged 0$none
followed 2 files that were moved" "$out"
is two-list "$work/music/a/eins.flac | 2026-11-13T12:00:00
$work/music/b/zwei.mp3 | 2026-11-13T12:00:00" "$("$bin" library --db "$db" list --format '<URL> | <Date Added>')"

echo "cli_library_followed: $fails failed"
[ "$fails" = 0 ]
