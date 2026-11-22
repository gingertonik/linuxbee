#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# A command that only reads the library waits for a busy one, and says in a reader's words why it
# cannot read: a file that is no library, a library another program holds, a folder it cannot use.
# Usage: cli_library_readonly_words.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Rules: docs/spec/library.md, L-READ ("The wait", "Its words"). Everything happens in folders of
# this test's own.
set -u
bin=$1
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-words.XXXXXX") || exit 2
trap 'kill $(jobs -p) 2> /dev/null; chmod -R u+w "$work" 2> /dev/null; rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
export QT_QPA_PLATFORM=offscreen
said() { head -n 1 "$work/err"; }
mkdir -p "$work/nothing"

# --- Not a library: an empty file, and a database of another program at any version.
: > "$work/empty.db"
sqlite3 "$work/other0.db" "CREATE TABLE notes (x TEXT);" || fail "the other database could not be made"
sqlite3 "$work/other3.db" "CREATE TABLE notes (x TEXT); PRAGMA user_version = 3;" || fail "the other database could not be made"
for f in empty other0 other3; do
    before=$(sha256sum < "$work/$f.db")
    out=$("$bin" library --db "$work/$f.db" count 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && [ -z "$out" ] && pass "$f-refused" || fail "$f-refused rc=$rc [$out]"
    [ "$(said)" = "linuxbeectl: cannot open the library file \"$work/$f.db\": That file is not a linuxBee library: it holds no library" ] \
        && pass "$f-not-a-library" || fail "$f-not-a-library [$(said)]"
    [ "$(sha256sum < "$work/$f.db")" = "$before" ] && pass "$f-left" || fail "$f-left"
done

# --- A library another program holds: the read waits (L-WAIT's five seconds), and reads.
db="$work/lib.db"
"$bin" library --db "$db" scan --now 2026-11-13T12:00:00Z "$work/nothing" > /dev/null 2>&1
[ "$("$bin" library --db "$db" count 2> /dev/null)" = 0 ] && pass setup-library || fail setup-library
hold() { ( echo "PRAGMA locking_mode=EXCLUSIVE; BEGIN EXCLUSIVE; UPDATE tracks SET play_count = play_count;"; sleep "$1"; echo "COMMIT;" ) | sqlite3 "$db" > /dev/null 2>&1 & }
hold 2
sleep 0.5
start=$(date +%s)
out=$(timeout 30 "$bin" library --db "$db" count 2> "$work/err"); rc=$?
took=$(( $(date +%s) - start ))
[ "$rc" = 0 ] && [ "$out" = 0 ] && pass busy-waits-and-reads || fail "busy-waits-and-reads rc=$rc [$out] [$(said)]"
[ "$took" -ge 1 ] && pass busy-did-wait || fail "busy-did-wait [${took}s]"
wait
# Held longer than the wait: it gives up, says the library is busy, and does not hang.
hold 12
sleep 0.5
start=$(date +%s)
out=$(timeout 30 "$bin" library --db "$db" count 2> "$work/err"); rc=$?
took=$(( $(date +%s) - start ))
[ "$rc" = 3 ] && [ -z "$out" ] && pass busy-gives-up || fail "busy-gives-up rc=$rc [$out]"
[ "$(said)" = "linuxbeectl: cannot open the library file \"$db\": the library is busy: another program holds it; try again" ] \
    && pass busy-says-so || fail "busy-says-so [$(said)]"
[ "$took" -ge 4 ] && [ "$took" -le 9 ] && pass busy-waited-five-seconds || fail "busy-waited-five-seconds [${took}s]"
wait

# --- A folder it cannot use: no -shm beside the library, and the folder cannot be written to.
mkdir -p "$work/ro"
"$bin" library --db "$work/ro/lib.db" scan --now 2026-11-13T12:00:00Z "$work/nothing" > /dev/null 2>&1
rm -f "$work/ro/lib.db-wal" "$work/ro/lib.db-shm"
chmod a-w "$work/ro"
if touch "$work/ro/probe" 2> /dev/null; then
    rm -f "$work/ro/probe"
    echo "SKIPPED: this user may write to a read-only folder"
else
    out=$("$bin" library --db "$work/ro/lib.db" count 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && [ -z "$out" ] && pass folder-refused || fail "folder-refused rc=$rc [$out]"
    [ "$(said)" = "linuxbeectl: cannot open the library file \"$work/ro/lib.db\": the library cannot be read here: its folder cannot be written to, and the database needs its side files (-wal, -shm) beside it" ] \
        && pass folder-says-why || fail "folder-says-why [$(said)]"
fi
chmod u+w "$work/ro"

echo "cli_library_readonly_words: $fails failed"
[ "$fails" = 0 ]
