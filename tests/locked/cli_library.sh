#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_library.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/library.md, L-CLI. Works in a temporary folder.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
check() {
    label=$1; want_rc=$2; want_out=$3
    shift 3
    out=$("$bin" "$@" 2>/dev/null)
    rc=$?
    if [ "$rc" != "$want_rc" ] || [ "$out" != "$want_out" ]; then
        fail "$label: rc=$rc (want $want_rc) stdout=[$out] (want [$want_out])"
    else
        pass "$label"
    fi
}

mkdir -p "$work/music/a" "$work/music/b"
cp "$data/basic.mp3" "$data/basic.flac" "$work/music/a/"
cp "$data/basic.m4a" "$data/notaudio.mp3" "$work/music/b/"
db="$work/library.db"
NOW=2026-11-13T12:00:00Z

check count-new 0 "0" library --db "$db" count
[ -f "$db" ] && pass db-created || fail db-created
check scan 0 "added 3, updated 0, unchanged 0, missing 0, found again 0, not music 1" \
    library --db "$db" scan --now "$NOW" "$work/music"
check count 0 "3" library --db "$db" count
check list 0 "$work/music/a/basic.flac
$work/music/a/basic.mp3
$work/music/b/basic.m4a" library --db "$db" list
check list-format 0 "Flac Title | 2026-11-13T12:00:00 | Music
Someone like You | 2026-11-13T12:00:00 | Music
M4A Title | 2026-11-13T12:00:00 | Music" library --db "$db" list --format "<Title> | <Date Added> | <Category>"

# a second scan: one new file for another category, one gone
cp "$data/basic.opus" "$work/music/b/new.opus"
rm "$work/music/b/basic.m4a"
check rescan 0 "added 1, updated 0, unchanged 2, missing 1, found again 0, not music 1" \
    library --db "$db" scan --now 2026-12-01T00:00:00Z --category Inbox "$work/music"
check count-keeps-missing 0 "4" library --db "$db" count
check list-category 0 "$work/music/b/new.opus" library --db "$db" list --category Inbox
check list-empty-category 0 "" library --db "$db" list --category Podcasts
check two-folders 0 "added 0, updated 0, unchanged 3, missing 0, found again 0, not music 1" \
    library --db "$db" scan --now "$NOW" "$work/music/a" "$work/music/b"

# refusals
check no-db 2 "" library count
check no-subcommand 2 "" library --db "$db"
check bad-subcommand 2 "" library --db "$db" frob
check scan-no-folder 2 "" library --db "$db" scan
check bad-category 2 "" library --db "$db" list --category Stuff
check bad-now 2 "" library --db "$db" scan --now soon "$work/music"
check bad-format 2 "" library --db "$db" list --format '$If(<Title>'
check count-extra 2 "" library --db "$db" count extra
check unopenable 3 "" library --db "$work/none/library.db" count
head -c 4096 /dev/zero | tr '\0' 'x' > "$work/junk.db"
check not-a-library 3 "" library --db "$work/junk.db" count
check count-after-refusals 0 "4" library --db "$db" count

case "$("$bin" --help 2>/dev/null)" in
    *"  library "*) pass help-lists-library ;;
    *) fail help-lists-library ;;
esac

echo "cli_library: $fails failed"
[ "$fails" -eq 0 ]
