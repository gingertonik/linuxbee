#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_library_reread.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# A library written before this program read Love: the next scan reads its files again, once, and only the
# tracks whose reading differs are called updated. And a library of schema 1 opened by several programs at once
# is brought up by one of them. Rules: docs/spec/library.md, L-SCHEMA and L-SCAN.
# The old library is made with sqlite3, with the table as schema 1 had it. Works in a temporary folder.
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

music="$work/music"
mkdir -p "$music"
for f in basic.mp3 basic.ogg basic.m4a love.flac love.m4a ban.flac; do cp "$data/$f" "$music/$f"; done
sums() { (cd "$music" && sha256sum ./* | sha256sum); }
music_before=$(sums)
out=$("$bin" library --db "$work/now.db" scan --now 2026-11-13T12:00:00Z "$music" 2> /dev/null)
[ "$out" = "added 6, updated 0, unchanged 0, missing 0, found again 0, not music 0" ] && pass setup-scan || fail "setup-scan [$out]"
loved() { sqlite3 "$1" "SELECT count(*) FROM tracks WHERE json_extract(tags_json, '\$.fields.Love') IS NOT NULL"; }
[ "$(loved "$work/now.db")" = 3 ] && pass setup-loved || fail "setup-loved [$(loved "$work/now.db")]"

# The same library as a program before Love left it: schema 1, and no Love in what it read.
old() {
    rm -f "$1" "$1-wal" "$1-shm"
    sqlite3 "$1" "
CREATE TABLE tracks (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL UNIQUE, category TEXT NOT NULL, size_bytes INTEGER NOT NULL, modified_secs INTEGER NOT NULL, date_added_secs INTEGER NOT NULL, play_count INTEGER NOT NULL DEFAULT 0, skip_count INTEGER NOT NULL DEFAULT 0, last_played_secs INTEGER NOT NULL DEFAULT (-1), missing INTEGER NOT NULL DEFAULT 0, picture_count INTEGER NOT NULL DEFAULT 0, tags_json TEXT NOT NULL);
CREATE INDEX tracks_by_category ON tracks (category);
ATTACH '$work/now.db' AS now;
INSERT INTO tracks (id, path, category, size_bytes, modified_secs, date_added_secs, play_count, skip_count, last_played_secs, missing, picture_count, tags_json)
  SELECT id, path, category, size_bytes, modified_secs, date_added_secs, 3, skip_count, last_played_secs, missing, picture_count, json_remove(tags_json, '\$.fields.Love') FROM now.tracks;
DETACH now;
PRAGMA user_version = 1;" || fail "the old library could not be made"
}
old "$work/old.db"
[ "$(sqlite3 "$work/old.db" 'PRAGMA user_version')" = 1 ] && [ "$(loved "$work/old.db")" = 0 ] && pass setup-old || fail setup-old

out=$("$bin" library --db "$work/old.db" count 2> "$work/err"); rc=$?
[ "$rc" = 0 ] && [ "$out" = 6 ] && pass old-opens || fail "old-opens rc=$rc [$out] [$(cat "$work/err")]"
[ "$(sqlite3 "$work/old.db" 'PRAGMA user_version')" -ge 2 ] && pass old-brought-up || fail "old-brought-up [$(sqlite3 "$work/old.db" 'PRAGMA user_version')]"
[ "$(loved "$work/old.db")" = 0 ] && pass opening-reads-no-file || fail opening-reads-no-file

# The scan: every file is as stored, every file is read again, and three read differently.
out=$("$bin" library --db "$work/old.db" scan --now 2026-11-17T12:00:00Z "$music" 2> /dev/null)
[ "$out" = "added 0, updated 3, unchanged 3, missing 0, found again 0, not music 0" ] && pass reread-counts || fail "reread-counts [$out]"
[ "$(loved "$work/old.db")" = 3 ] && pass reread-love-is-there || fail "reread-love-is-there [$(loved "$work/old.db")]"
[ "$(sqlite3 "$work/old.db" 'SELECT count(*) FROM tracks WHERE play_count = 3 AND date_added_secs = (SELECT date_added_secs FROM tracks LIMIT 1)')" = 6 ] \
    && pass reread-keeps-what-the-library-knows || fail reread-keeps-what-the-library-knows
same=$(sqlite3 "$work/old.db" "ATTACH '$work/now.db' AS now; SELECT count(*) FROM tracks t JOIN now.tracks n ON n.path = t.path AND n.tags_json = t.tags_json AND n.id = t.id")
[ "$same" = 6 ] && pass reread-is-as-a-fresh-scan || fail "reread-is-as-a-fresh-scan [$same]"
out=$("$bin" library --db "$work/old.db" scan --now 2026-11-17T12:00:00Z "$music" 2> /dev/null)
[ "$out" = "added 0, updated 0, unchanged 6, missing 0, found again 0, not music 0" ] && pass reread-once || fail "reread-once [$out]"
[ "$(sums)" = "$music_before" ] && pass no-music-file-touched || fail no-music-file-touched

# Eight programs open a schema 1 library at the same moment: one brings it up, all of them open it.
round=1; bad=0
while [ $round -le 12 ]; do
    old "$work/race.db"
    i=1; while [ $i -le 8 ]; do ("$bin" library --db "$work/race.db" count > "$work/r$i.out" 2> "$work/r$i.err"; echo $? > "$work/r$i.rc") & i=$((i + 1)); done
    wait
    i=1; while [ $i -le 8 ]; do
        [ "$(cat "$work/r$i.rc")" = 0 ] && [ "$(cat "$work/r$i.out")" = 6 ] || { bad=$((bad + 1)); [ $bad -le 3 ] && echo "  round $round, program $i: rc=$(cat "$work/r$i.rc") [$(head -c 200 "$work/r$i.err")]"; }
        i=$((i + 1))
    done
    [ "$(sqlite3 "$work/race.db" 'SELECT count(*) FROM tracks')" = 6 ] || bad=$((bad + 1))
    round=$((round + 1))
done
[ "$bad" = 0 ] && pass opened-together || fail "opened-together: $bad of 96 did not open it"

echo "cli_library_reread: $fails failed"
[ "$fails" -eq 0 ]
