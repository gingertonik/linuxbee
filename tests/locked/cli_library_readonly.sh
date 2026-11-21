#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# A command that only reads the library never changes it: it does not create one, it does not bring
# an older one up to date, and it reads one that another program has open and is writing.
# Usage: cli_library_readonly.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Rules: docs/spec/library.md, L-READ, L-SCHEMA and L-CLI. The owner's ruling of 2026-11-20: read-only
# commands refuse an older library and say why; only writing commands step it. Everything happens in
# folders of this test's own, with copies of the test files.
set -u
bin=$1
data="${LINUXBEE_TEST_DATA:?}"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-read.XXXXXX") || exit 2
trap 'kill $(jobs -p) 2> /dev/null; rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
export QT_QPA_PLATFORM=offscreen
now="--now 2026-11-13T12:00:00Z"
mkdir -p "$work/nothing" "$work/music"
cp "$data/tags/basic.mp3" "$data/tags/basic.flac" "$data/tags/basic.ogg" "$work/music/"
version() { sqlite3 "$1" 'PRAGMA user_version' 2> /dev/null; }
sum() { sha256sum < "$1" | cut -c1-64; }
said() { head -n 1 "$work/err"; }

# --- A file that is not there: the reading commands make nothing (L-READ).
gone="$work/lib/none.db"
mkdir -p "$work/lib"
for c in "count" "list" "list --format <Title>" "import $data/import/library.xml"; do
    # shellcheck disable=SC2086
    out=$("$bin" library --db "$gone" $c 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && [ -z "$out" ] && pass "missing-refused [$c]" || fail "missing-refused [$c] rc=$rc [$out]"
    [ "$(said)" = "linuxbeectl: cannot open the library file \"$gone\": there is no such file" ] \
        && pass "missing-says-so [$c]" || fail "missing-says-so [$c] [$(said)]"
    [ -z "$(ls -A "$work/lib")" ] && pass "missing-made-nothing [$c]" || fail "missing-made-nothing [$c] [$(ls -A "$work/lib")]"
done
# A writing command still makes one.
# shellcheck disable=SC2086
out=$("$bin" library --db "$work/lib/made.db" scan $now "$work/music" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ -f "$work/lib/made.db" ] && pass scan-makes-one || fail "scan-makes-one rc=$rc [$out]"
cur=$(version "$work/lib/made.db")
[ "${cur:-0}" -ge 3 ] && pass setup-current || fail "setup-current [$cur]"
# A closed library of this schema: reading leaves the library file's bytes as they were. SQLite may
# leave its two side files (-wal, -shm) beside it; the next writer to close last takes them away.
before=$(sum "$work/lib/made.db")
for c in "count" "list" "list --format <Title>" "import $data/import/library.xml"; do
    # shellcheck disable=SC2086
    "$bin" library --db "$work/lib/made.db" $c > /dev/null 2>&1
done
[ "$(sum "$work/lib/made.db")" = "$before" ] && pass read-leaves-the-bytes || fail read-leaves-the-bytes
ls -A "$work/lib" | grep -qv '^made\.db\(-wal\|-shm\)\?$' && fail "read-made-other-files [$(ls -A "$work/lib")]" || pass read-made-no-other-file

# --- A library of an older schema: refused by every reading command, left exactly as it was.
old() {
    rm -f "$1" "$1-wal" "$1-shm"
    sqlite3 "$1" "
CREATE TABLE tracks (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL UNIQUE, category TEXT NOT NULL, size_bytes INTEGER NOT NULL, modified_secs INTEGER NOT NULL, date_added_secs INTEGER NOT NULL, play_count INTEGER NOT NULL DEFAULT 0, skip_count INTEGER NOT NULL DEFAULT 0, last_played_secs INTEGER NOT NULL DEFAULT (-1), missing INTEGER NOT NULL DEFAULT 0, picture_count INTEGER NOT NULL DEFAULT 0, tags_json TEXT NOT NULL);
CREATE INDEX tracks_by_category ON tracks (category);
INSERT INTO tracks (id, path, category, size_bytes, modified_secs, date_added_secs, play_count, tags_json) VALUES (1, '/music/a.flac', 'Music', 10, 1700000000, 1700000100, 4, '{}');
INSERT INTO tracks (id, path, category, size_bytes, modified_secs, date_added_secs, play_count, tags_json) VALUES (2, '/music/b.flac', 'Music', 11, 1700000000, 1700000100, 5, '{}');
PRAGMA user_version = 1;" || fail "the old library could not be made"
}
old "$work/old.db"
before=$(sum "$work/old.db")
want="linuxbeectl: cannot open the library file \"$work/old.db\": it is at schema 1 and this linuxBee reads schema $cur. A command that only reads does not bring it up to date; library scan, library import --apply or the window does, after a copy."
for c in "count" "list" "list --format <Title>" "import $data/import/library.xml" "import $data/import/library.xml --list"; do
    # shellcheck disable=SC2086
    out=$("$bin" library --db "$work/old.db" $c 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && [ -z "$out" ] && pass "old-refused [$c]" || fail "old-refused [$c] rc=$rc [$out]"
    [ "$(said)" = "$want" ] && pass "old-says-why [$c]" || fail "old-says-why [$c] [$(said)]"
    [ "$(sum "$work/old.db")" = "$before" ] && [ "$(version "$work/old.db")" = 1 ] && [ ! -e "$work/old.db.schema-1" ] \
        && pass "old-left-as-it-was [$c]" || fail "old-left-as-it-was [$c] [$(ls "$work")]"
done
# The writing commands bring it up to date, after the copy, as before (L-SCHEMA).
"$bin" library --db "$work/old.db" import "$data/import/library.xml" --apply > /dev/null 2> "$work/err"; rc=$?
[ "$rc" = 0 ] && [ "$(version "$work/old.db")" = "$cur" ] && [ -f "$work/old.db.schema-1" ] \
    && grep -qxF "linuxbeectl: The library was brought up to date (schema 1 to $cur). A copy of it as it was before: $work/old.db.schema-1" "$work/err" \
    && pass import-apply-steps || fail "import-apply-steps rc=$rc [$(cat "$work/err")]"
out=$("$bin" library --db "$work/old.db" count 2> "$work/err"); rc=$?
[ "$rc" = 0 ] && [ "$out" = 2 ] && [ ! -s "$work/err" ] && pass then-it-reads || fail "then-it-reads rc=$rc [$out] [$(cat "$work/err")]"

# --- A newer schema: the same exit as an older one, so a script sees one code.
old "$work/new.db"
sqlite3 "$work/new.db" "PRAGMA user_version = $((cur + 1))"
before=$(sum "$work/new.db")
out=$("$bin" library --db "$work/new.db" count 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && grep -q 'newer linuxBee' "$work/err" && [ "$(sum "$work/new.db")" = "$before" ] \
    && pass newer-refused-the-same || fail "newer-refused-the-same rc=$rc [$(said)]"

# --- A library that another program has open and is writing (L-READ, L-WAIT).
db="$work/lib/made.db"
n=$("$bin" library --db "$db" count 2> /dev/null)
[ "$n" = 3 ] && pass setup-three || fail "setup-three [$n]"
# A writer that keeps its connection: one track marked deleted and committed (it lies in the
# write-ahead log), then a second change held in an open transaction.
mkfifo "$work/pipe"
sqlite3 "$db" < "$work/pipe" > /dev/null 2>&1 &
exec 9> "$work/pipe"
echo "PRAGMA busy_timeout = 5000; UPDATE tracks SET deleted_secs = 1 WHERE id = (SELECT min(id) FROM tracks);" >&9
echo "BEGIN IMMEDIATE; UPDATE tracks SET deleted_secs = 1 WHERE id = (SELECT max(id) FROM tracks);" >&9
sleep 1
[ -s "$db-wal" ] && pass setup-writer || fail "setup-writer [$(ls "$work/lib")]"
for c in "count" "list"; do
    # shellcheck disable=SC2086
    out=$(timeout 20 "$bin" library --db "$db" $c 2> "$work/err"); rc=$?
    [ "$rc" = 0 ] && pass "beside-a-writer [$c]" || fail "beside-a-writer [$c] rc=$rc [$(said)]"
done
out=$(timeout 20 "$bin" library --db "$db" count 2> /dev/null)
[ "$out" = 2 ] && pass sees-what-was-committed || fail "sees-what-was-committed [$out] (want 2)"
echo "COMMIT;" >&9
exec 9>&-
wait
out=$("$bin" library --db "$db" count 2> /dev/null)
[ "$out" = 1 ] && pass then-the-commit || fail "then-the-commit [$out] (want 1)"
# A reading command expires no deleted row and writes nothing: the rows are as the writer left them.
[ "$(sqlite3 "$db" 'SELECT count(*) FROM tracks')" = 3 ] && pass read-expired-nothing || fail read-expired-nothing
# And the library is a writer's again afterwards.
# shellcheck disable=SC2086
out=$("$bin" library --db "$db" scan $now "$work/nothing" 2> "$work/err"); rc=$?
[ "$rc" = 0 ] && pass writable-after || fail "writable-after rc=$rc [$(said)]"

echo "cli_library_readonly: $fails failed"
[ "$fails" = 0 ]
