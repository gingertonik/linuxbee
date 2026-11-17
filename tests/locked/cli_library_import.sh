#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_library_import.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# `linuxbeectl library --db FILE import EXPORT [--map OLD=NEW]... [--apply] [--list]`: what a library exported by
# another program knows and a music file does not carry is brought into the library; ratings and Love are
# checked against what the library read from the files, and never written. Rules: docs/spec/import.md, I-CLI.
# The export here is made up, in the shape MusicBee writes. Works in a temporary folder. Needs sqlite3, to look
# into the library and to plant a refusal from outside the program.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
has() { grep -qxF -- "$2" "$1" && pass "$3" || fail "$3: no line [$2] in: $(tr '\n' '|' < "$1" | cut -c1-600)"; }

music="$work/My Music"
mkdir -p "$music"
cp "$data/basic.mp3" "$music/one.mp3"
cp "$data/love.flac" "$music/two.flac"
cp "$data/love.m4a" "$music/three.m4a"
cp "$data/basic.ogg" "$music/four.ogg"
cp "$data/ban.flac" "$music/five.flac" # banned, and four stars in its own tag, which the export does not have
"$bin" tags write "$music/one.mp3" --set Rating=3.5 > /dev/null || fail "setup: rating one"
"$bin" tags write "$music/two.flac" --set Rating=2 > /dev/null || fail "setup: rating two"
db="$work/library.db"
out=$("$bin" library --db "$db" scan --now 2026-11-13T12:00:00Z "$music" 2> /dev/null)
[ "$out" = "added 5, updated 0, unchanged 0, missing 0, found again 0, not music 0" ] && pass setup-scan || fail "setup-scan [$out]"
sums() { (cd "$music" && sha256sum ./* | sha256sum); }
rows() { sqlite3 "$db" "SELECT path, date_added_secs, play_count, skip_count, last_played_secs FROM tracks ORDER BY path" | sha256sum; }
music_before=$(sums)

# The export: paths as Wine shows them (the whole machine is drive Z), a space as %20.
loc="file://localhost/Z:$(echo "$music" | sed 's/ /%20/g')"
t() { printf '\t\t<key>%s</key>\n\t\t<dict>\n\t\t\t<key>Track ID</key><integer>%s</integer>\n%s\t\t\t<key>Location</key><string>%s</string>\n\t\t</dict>\n' "$1" "$1" "$2" "$3"; }
k() { printf '\t\t\t<key>%s</key><%s>%s</%s>\n' "$1" "$2" "$3" "$2"; }
{
    printf '<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0">\n<dict>\n\t<key>Application Version</key><string>3.7.0.0</string>\n\t<key>Tracks</key>\n\t<dict>\n'
    t 1 "$(k Rating integer 70)
$(k 'Play Count' integer 12)
$(k 'Skip Count' integer 2)
$(k 'Play Date UTC' date 2025-12-31T23:59:59Z)
$(k 'Date Added' date 2019-03-04T07:00:00Z)
" "$loc/one.mp3"
    t 2 "$(k Love string L)
$(k Rating integer 100)
$(k 'Date Added' date 2020-06-07T08:09:10Z)
" "$loc/two.flac"
    t 3 "$(k Rating integer 0)
$(k 'Date Added' date 2026-11-13T12:00:00Z)
" "$loc/three.m4a"
    t 4 "$(k Love string L)
$(k Rating integer 50)
$(k 'Date Added' date 2021-01-01T00:00:00Z)
" "$loc/four.ogg"
    t 5 "$(k Love string L)
$(k 'Date Added' date 2022-02-02T00:00:00Z)
" "$loc/five.flac"
    i=1; while [ $i -le 12 ]; do t $((100 + i)) "$(k 'Play Count' integer 5)
" "$loc/gone/$(printf '%02d' $i).mp3"; i=$((i + 1)); done
    printf '\t</dict>\n</dict>\n</plist>\n'
} > "$work/export.xml"

# I-CLI: without --apply nothing changes; it says what it would do.
before=$(rows)
"$bin" library --db "$db" import "$work/export.xml" --map Z:=/ > "$work/dry" 2> "$work/err"; rc=$?
[ "$rc" = 0 ] && pass dry-exit || fail "dry-exit rc=$rc [$(cat "$work/err")]"
[ "$(rows)" = "$before" ] && pass dry-changes-nothing || fail dry-changes-nothing
has "$work/dry" "tracks in the file: 17" dry-tracks
has "$work/dry" "in the library: 5" dry-matched
has "$work/dry" "not in the library: 12" dry-unmatched
has "$work/dry" "would change: 4" dry-would-change
has "$work/dry" "ratings: 1 the same, 1 differ, 1 only in the file given, 1 only in the library" dry-ratings
has "$work/dry" "love: 1 the same, 1 differ, 1 only in the file given, 1 only in the library" dry-love
has "$work/dry" "ratings that differ:" dry-ratings-differ-heading
has "$work/dry" "  $music/two.flac: 5 in the file given, 2 in the library" dry-ratings-differ
has "$work/dry" "ratings only in the file given:" dry-ratings-only-heading
has "$work/dry" "  $music/four.ogg: 2.5" dry-ratings-only
has "$work/dry" "love that differs:" dry-love-differs-heading
has "$work/dry" "  $music/five.flac: L in the file given, B in the library" dry-love-differs
has "$work/dry" "love only in the file given:" dry-love-only-heading
has "$work/dry" "  $music/four.ogg: L" dry-love-only
has "$work/dry" "not in the library:" dry-unmatched-heading
has "$work/dry" "  $music/gone/01.mp3" dry-unmatched-first
has "$work/dry" "  $music/gone/10.mp3" dry-unmatched-tenth
has "$work/dry" "  and 2 more (--list shows all)" dry-unmatched-more
grep -qF "gone/11.mp3" "$work/dry" && fail "dry: more than ten of a list without --list" || pass dry-ten-only
grep -q '^changed:' "$work/dry" && fail "dry: says changed" || pass dry-not-changed-line
[ "$(sed -n 1p "$work/dry")" = "tracks in the file: 17" ] && [ "$(sed -n 6p "$work/dry")" = "love: 1 the same, 1 differ, 1 only in the file given, 1 only in the library" ] \
    && pass dry-order || fail "dry-order: $(head -n 6 "$work/dry" | tr '\n' '|')"
"$bin" library --db "$db" import "$work/export.xml" --map Z:=/ --list > "$work/all" 2> /dev/null
has "$work/all" "  $music/gone/12.mp3" list-all
grep -q 'more (--list' "$work/all" && fail "list: still says more" || pass list-no-more-line

# A refusal by the database: nothing stored, the reason on standard error, exit 3, no "changed" line.
sqlite3 "$db" "CREATE TRIGGER refuse BEFORE UPDATE ON tracks WHEN NEW.path LIKE '%/two.flac' BEGIN SELECT RAISE(ABORT, 'refused for the test'); END" || fail "the trigger could not be planted"
"$bin" library --db "$db" import "$work/export.xml" --map Z:=/ --apply > "$work/out" 2> "$work/err"; rc=$?
[ "$rc" = 3 ] && pass refused-exit || fail "refused-exit rc=$rc (want 3)"
grep -q 'not stored' "$work/err" && grep -q 'refused for the test' "$work/err" && pass refused-says-why || fail "refused-says-why [$(cat "$work/err")]"
grep -q '^changed:\|^would change:' "$work/out" && fail "refused: a result line [$(grep 'change' "$work/out")]" || pass refused-no-result-line
[ "$(rows)" = "$before" ] && pass refused-stored-nothing || fail "refused-stored-nothing: the first tracks were left changed"
sqlite3 "$db" "DROP TRIGGER refuse"

# --apply.
"$bin" library --db "$db" import "$work/export.xml" --map Z:=/ --apply > "$work/out" 2> "$work/err"; rc=$?
[ "$rc" = 0 ] && pass apply-exit || fail "apply-exit rc=$rc [$(cat "$work/err")]"
has "$work/out" "changed: 4" apply-changed
grep -q '^would change:' "$work/out" && fail "apply: says would" || pass apply-not-would
has "$work/out" "love: 1 the same, 1 differ, 1 only in the file given, 1 only in the library" apply-love
row() { sqlite3 "$db" "SELECT date_added_secs, play_count, skip_count, last_played_secs FROM tracks WHERE path LIKE '%/$1'"; }
secs() { date -u -d "$1" +%s; }
[ "$(row one.mp3)" = "$(secs 2019-03-04T07:00:00Z)|12|2|$(secs 2025-12-31T23:59:59Z)" ] && pass apply-one || fail "apply-one [$(row one.mp3)]"
[ "$(row two.flac)" = "$(secs 2020-06-07T08:09:10Z)|0|0|-1" ] && pass apply-two || fail "apply-two [$(row two.flac)]"
[ "$(row three.m4a)" = "$(secs 2026-11-13T12:00:00Z)|0|0|-1" ] && pass apply-three-untouched || fail "apply-three [$(row three.m4a)]"
out=$("$bin" library --db "$db" count 2> /dev/null); [ "$out" = 5 ] && pass apply-adds-no-track || fail "apply-adds-no-track [$out]"
# Ratings and Love are checked and never written: the library says of them what it said.
[ "$("$bin" tags read --field Rating "$music/two.flac")" = 2 ] && [ "$("$bin" tags read --field Love "$music/five.flac")" = B ] && pass tags-not-written || fail tags-not-written
[ "$(sums)" = "$music_before" ] && pass no-music-file-touched || fail "no-music-file-touched"

# Twice is once.
after=$(rows)
"$bin" library --db "$db" import "$work/export.xml" --map Z:=/ --apply > "$work/out" 2> /dev/null; rc=$?
[ "$rc" = 0 ] && pass twice-exit || fail "twice-exit rc=$rc"
has "$work/out" "changed: 0" twice-changed-none
[ "$(rows)" = "$after" ] && pass twice-is-once || fail twice-is-once
"$bin" library --db "$db" import "$work/export.xml" --map Z:=/ > "$work/out" 2> /dev/null
has "$work/out" "would change: 0" twice-dry
# A play counted here since is not lost to a later import of the old export.
sqlite3 "$db" "UPDATE tracks SET play_count = 20, last_played_secs = $(secs 2026-11-16T10:00:00Z) WHERE path LIKE '%/one.mp3'"
"$bin" library --db "$db" import "$work/export.xml" --map Z:=/ --apply > "$work/out" 2> /dev/null
[ "$(row one.mp3)" = "$(secs 2019-03-04T07:00:00Z)|20|2|$(secs 2026-11-16T10:00:00Z)" ] && pass later-plays-kept || fail "later-plays-kept [$(row one.mp3)]"

# The mapping: several, tried in order; one that fits no track is said; none at all finds nothing and says how.
"$bin" library --db "$db" import "$work/export.xml" --map Q:=/nowhere --map Z:=/ > "$work/out" 2> "$work/err"; rc=$?
[ "$rc" = 0 ] && pass map-two-exit || fail "map-two-exit rc=$rc"
has "$work/out" "in the library: 5" map-two-matched
grep -qF -- '--map Q:=/nowhere fits no track' "$work/err" && pass map-unused-said || fail "map-unused-said [$(cat "$work/err")]"
grep -qF -- 'Z:=/ fits no track' "$work/err" && fail "map: the one that fits is called unused" || pass map-used-not-said
"$bin" library --db "$db" import "$work/export.xml" --map "Z:$work=$work" > "$work/out" 2> /dev/null
has "$work/out" "in the library: 5" map-folder
"$bin" library --db "$db" import "$work/export.xml" > "$work/out" 2> "$work/err"; rc=$?
[ "$rc" = 0 ] && pass map-none-exit || fail "map-none-exit rc=$rc"
has "$work/out" "in the library: 0" map-none-matched
grep -qF -- '--map' "$work/err" && grep -qF 'Z:/' "$work/err" && pass map-none-hint || fail "map-none-hint [$(cat "$work/err")]"

# What is wrong with the line is 2 and does nothing; what is wrong with a file is 3.
check() { name="$1"; want="$2"; shift 2; "$bin" "$@" > "$work/out" 2> "$work/err"; rc=$?; [ "$rc" = "$want" ] && [ -s "$work/err" ] && [ ! -s "$work/out" ] && pass "$name" || fail "$name rc=$rc (want $want) out=[$(head -c 200 "$work/out")] err=[$(head -c 200 "$work/err")]"; }
check usage-no-file 2 library --db "$db" import
check usage-two-files 2 library --db "$db" import "$work/export.xml" "$work/export.xml"
check usage-map-no-equals 2 library --db "$db" import "$work/export.xml" --map Z:
check usage-map-empty-old 2 library --db "$db" import "$work/export.xml" --map =/x
check usage-no-db 2 library import "$work/export.xml"
check file-missing 3 library --db "$db" import "$work/none.xml"
printf 'not xml at all' > "$work/bad.xml"
check file-not-an-export 3 library --db "$db" import "$work/bad.xml" --apply
[ "$(rows)" = "$(sqlite3 "$db" "SELECT path, date_added_secs, play_count, skip_count, last_played_secs FROM tracks ORDER BY path" | sha256sum)" ] && pass still-whole || fail still-whole
[ "$(sums)" = "$music_before" ] && pass no-music-file-touched-at-all || fail no-music-file-touched-at-all

echo "cli_library_import: $fails failed"
[ "$fails" -eq 0 ]
