#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# A library of an older schema is copied before it is brought up to date, and the program says so
# in one line: the command line, and the application.
# Usage: cli_library_copy.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Rules: docs/spec/library.md, L-SCHEMA ("A copy before a step"). The old library is made with
# sqlite3, with the table as schema 1 had it. Everything happens in folders of this test's own.
set -u
bin=$1
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-copy.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
export QT_QPA_PLATFORM=offscreen

# A library as schema 1 had it, with three tracks.
old() {
    rm -f "$1" "$1-wal" "$1-shm"
    sqlite3 "$1" "
CREATE TABLE tracks (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL UNIQUE, category TEXT NOT NULL, size_bytes INTEGER NOT NULL, modified_secs INTEGER NOT NULL, date_added_secs INTEGER NOT NULL, play_count INTEGER NOT NULL DEFAULT 0, skip_count INTEGER NOT NULL DEFAULT 0, last_played_secs INTEGER NOT NULL DEFAULT (-1), missing INTEGER NOT NULL DEFAULT 0, picture_count INTEGER NOT NULL DEFAULT 0, tags_json TEXT NOT NULL);
CREATE INDEX tracks_by_category ON tracks (category);
INSERT INTO tracks (id, path, category, size_bytes, modified_secs, date_added_secs, play_count, tags_json) VALUES (1, '/music/a.flac', 'Music', 10, 1700000000, 1700000100, 4, '{}');
INSERT INTO tracks (id, path, category, size_bytes, modified_secs, date_added_secs, play_count, tags_json) VALUES (2, '/music/b.flac', 'Music', 11, 1700000000, 1700000100, 5, '{}');
INSERT INTO tracks (id, path, category, size_bytes, modified_secs, date_added_secs, play_count, tags_json) VALUES (3, '/music/c.flac', 'Music', 12, 1700000000, 1700000100, 6, '{}');
PRAGMA user_version = 1;" || fail "the old library could not be made"
}
rows() { sqlite3 "$1" "SELECT id, path, play_count FROM tracks ORDER BY id" 2> /dev/null | sha256sum | cut -c1-64; }
version() { sqlite3 "$1" 'PRAGMA user_version' 2> /dev/null; }

# The schema this program writes.
"$bin" library --db "$work/now.db" count > /dev/null 2>&1
now=$(version "$work/now.db")
[ "${now:-0}" -ge 2 ] && pass setup-now || fail "setup-now [$now]"

# --- The command line: the answer on standard output as ever, the line on standard error.
old "$work/old.db"
before=$(rows "$work/old.db")
out=$("$bin" library --db "$work/old.db" count 2> "$work/err"); rc=$?
[ "$rc" = 0 ] && [ "$out" = 3 ] && pass cli-answers || fail "cli-answers rc=$rc [$out]"
want="linuxbeectl: The library was brought up to date (schema 1 to $now). A copy of it as it was before: $work/old.db.schema-1"
[ "$(cat "$work/err")" = "$want" ] && pass cli-says-it || fail "cli-says-it [$(cat "$work/err")] want [$want]"
[ -f "$work/old.db.schema-1" ] && [ "$(version "$work/old.db.schema-1")" = 1 ] && [ "$(rows "$work/old.db.schema-1")" = "$before" ] \
    && pass cli-copy || fail "cli-copy [$(ls "$work")]"
[ "$(version "$work/old.db")" = "$now" ] && [ "$(rows "$work/old.db")" = "$before" ] && pass cli-brought-up || fail cli-brought-up

# --- Again: nothing is said, and no second copy.
out=$("$bin" library --db "$work/old.db" count 2> "$work/err"); rc=$?
[ "$rc" = 0 ] && [ "$out" = 3 ] && [ ! -s "$work/err" ] && pass cli-again-silent || fail "cli-again-silent rc=$rc [$out] [$(cat "$work/err")]"
[ "$(ls "$work" | grep -c 'old.db.schema')" = 1 ] && pass cli-again-one-copy || fail "cli-again-one-copy [$(ls "$work")]"

# --- The other command that opens a library says it too (a scan of a folder with nothing in it).
old "$work/old2.db"
mkdir -p "$work/nothing"
"$bin" library --db "$work/old2.db" scan --now 2026-11-13T12:00:00Z "$work/nothing" > /dev/null 2> "$work/err2"
grep -qxF "linuxbeectl: The library was brought up to date (schema 1 to $now). A copy of it as it was before: $work/old2.db.schema-1" "$work/err2" \
    && [ -f "$work/old2.db.schema-1" ] && pass cli-scan-says-it || fail "cli-scan-says-it [$(cat "$work/err2")]"

# --- The application: it asks nothing, makes the copy, and says the line.
b="$work/box"
mkdir -p "$b/config" "$b/data/linuxbee" "$b/cache" "$b/tmp"
lib="$b/data/linuxbee/library.db"
old "$lib"
before=$(rows "$lib")
XDG_CONFIG_HOME="$b/config" XDG_DATA_HOME="$b/data" XDG_CACHE_HOME="$b/cache" TMPDIR="$b/tmp" timeout 30 "$app" --screenshot "$b/w.png" > /dev/null 2> "$b/stderr"; rc=$?
[ "$rc" = 0 ] && [ -s "$b/w.png" ] && pass app-starts || fail "app-starts rc=$rc [$(head -c 300 "$b/stderr")]"
grep -qxF "linuxbee: The library was brought up to date (schema 1 to $now). A copy of it as it was before: $lib.schema-1" "$b/stderr" \
    && pass app-says-it || fail "app-says-it [$(head -c 400 "$b/stderr")]"
[ -f "$lib.schema-1" ] && [ "$(version "$lib.schema-1")" = 1 ] && [ "$(rows "$lib.schema-1")" = "$before" ] && pass app-copy || fail "app-copy [$(ls "$b/data/linuxbee")]"
[ "$(version "$lib")" = "$now" ] && [ "$(rows "$lib")" = "$before" ] && pass app-brought-up || fail "app-brought-up [$(version "$lib")]"
XDG_CONFIG_HOME="$b/config" XDG_DATA_HOME="$b/data" XDG_CACHE_HOME="$b/cache" TMPDIR="$b/tmp" timeout 30 "$app" --screenshot "$b/w2.png" > /dev/null 2> "$b/stderr2"
grep -q 'brought up to date' "$b/stderr2" && fail "app-again-silent [$(head -c 300 "$b/stderr2")]" || pass app-again-silent
[ "$(ls "$b/data/linuxbee" | grep -c 'library.db.schema')" = 1 ] && pass app-again-one-copy || fail "app-again-one-copy [$(ls "$b/data/linuxbee")]"

echo "cli_library_copy: $fails failed"
[ "$fails" = 0 ]
