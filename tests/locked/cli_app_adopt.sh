#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# An older build's library is taken along whole, or not at all: a database never goes without its log,
# a log never goes without its database, and neither is ever put beside another database.
# Usage: cli_app_adopt.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Rules: docs/spec/main-window.md, W-ADOPT ("A database and its companions"). Found by the review of
# fix 65, which lost a library this way. Everything happens in folders of this test's own. Needs sqlite3,
# to leave a database as a program that died leaves it.
set -u
ctl=$1
app="${LINUXBEE_APP:?}"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-adopt.XXXXXX") || exit 2
pids=""
trap 'for p in $pids; do kill -9 "$p" 2> /dev/null; done; rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen
box() {
    b="$work/$1"; mkdir -p "$b/config" "$b/data/linuxbee" "$b/cache" "$b/tmp" "$b/music"
    new="$b/data/linuxbee"; old="$new/linuxbee"
    cp "$data/basic.mp3" "$data/basic.flac" "$data/basic.ogg" "$b/music/"
}
run() { XDG_CONFIG_HOME="$b/config" XDG_DATA_HOME="$b/data" XDG_CACHE_HOME="$b/cache" TMPDIR="$b/tmp" timeout 20 "$app" --screenshot "$b/w.png" > /dev/null 2> "$b/stderr"; }
library() { mkdir -p "$(dirname "$1")"; "$ctl" library --db "$1" scan --now 2026-11-13T12:00:00Z "$2" > /dev/null 2>&1; }
sum() { sha256sum < "$1" 2> /dev/null | cut -c1-64; }
rows() { sqlite3 "$1" "SELECT id, path, play_count FROM tracks ORDER BY id" 2> /dev/null | sha256sum | cut -c1-64; }
sound() { [ "$(sqlite3 "$1" 'PRAGMA integrity_check' 2> /dev/null)" = ok ]; }
# Commits $2 in $1 through a program that is then killed, as a crash leaves it: the change is in the
# write-ahead log beside the database, and not in the database's own file.
dies_after() {
    # The log is switched on first and by itself: nothing else may have the file open while that is done.
    [ "$(sqlite3 "$1" 'PRAGMA journal_mode=wal' 2> /dev/null)" = wal ] || fail "the test could not give $1 a write-ahead log"
    try=0
    while [ $try -lt 3 ] && [ ! -s "$1-wal" ]; do
        try=$((try + 1))
        rm -f "$work/pipe"; mkfifo "$work/pipe"
        sqlite3 "$1" < "$work/pipe" > "$work/writer.out" 2>&1 &
        writer=$!; pids="$pids $writer"
        exec 8> "$work/pipe"
        printf '%s\n' "$2" >&8
        n=0; seen=""
        while [ $n -lt 400 ]; do
            seen=$(sqlite3 "$1" 'SELECT max(play_count) FROM tracks' 2> /dev/null)
            [ "$seen" = "$3" ] && break
            kill -0 "$writer" 2> /dev/null || break
            sleep 0.05; n=$((n + 1))
        done
        kill -9 "$writer" 2> /dev/null; wait "$writer" 2> /dev/null
        exec 8>&-; rm -f "$work/pipe"
    done
    [ -s "$1-wal" ] || fail "the test could not leave a log beside $1 (tries $try, saw [$seen], the writer said [$(head -c 200 "$work/writer.out" 2> /dev/null)])"
}

# --- The log comes along with its database: what was committed last is not lost.
box log-comes-along
library "$old/library.db" "$b/music"
dies_after "$old/library.db" "UPDATE tracks SET play_count = 41 WHERE id = 1;" 41
run; rc=$?
[ "$rc" = 0 ] && pass along-start || fail "along-start rc=$rc: $(head -n 2 "$b/stderr")"
[ "$(sqlite3 "$new/library.db" 'SELECT count(*), max(play_count) FROM tracks' 2> /dev/null)" = "3|41" ] && pass along-nothing-lost \
    || fail "along-nothing-lost: [$(sqlite3 "$new/library.db" 'SELECT count(*), max(play_count) FROM tracks' 2>&1)]"
sound "$new/library.db" && pass along-sound || fail along-sound
[ ! -e "$old" ] && pass along-old-gone || fail "along-old-gone: $(ls -A "$old" 2> /dev/null | tr '\n' ' ')"
[ -z "$(find "$new" -name 'library.db-*')" ] && pass along-one-file || fail "along-one-file: $(find "$new" -name 'library.db-*' | tr '\n' ' ')"

# --- Both places hold a library, and the older one has a log with a commit in it. (The fault: the log
# was moved beside the library in use, and SQLite applied it.)
box both-with-a-log
cp "$b/music/basic.flac" "$b/one.flac"; mkdir "$b/single"; mv "$b/one.flac" "$b/single/"
library "$new/library.db" "$b/single"
library "$old/library.db" "$b/music"
dies_after "$old/library.db" "UPDATE tracks SET play_count = 77;" 77
before=$(rows "$new/library.db"); olddb=$(sum "$old/library.db"); oldlog=$(sum "$old/library.db-wal")
run; rc=$?
[ "$rc" = 0 ] && pass both-start || fail "both-start rc=$rc: $(head -n 2 "$b/stderr")"
[ "$(rows "$new/library.db")" = "$before" ] && pass both-library-in-use-unchanged || fail "both-library-in-use-unchanged: $(sqlite3 "$new/library.db" 'SELECT count(*), max(play_count) FROM tracks' 2>&1)"
sound "$new/library.db" && pass both-sound || fail "both-sound: $(sqlite3 "$new/library.db" 'PRAGMA integrity_check' 2>&1 | head -n 2 | tr '\n' ' ')"
[ "$(sum "$old/library.db")" = "$olddb" ] && [ "$(sum "$old/library.db-wal")" = "$oldlog" ] && [ -e "$old/library.db-shm" ] \
    && pass both-older-left-whole || fail "both-older-left-whole: $(ls -A "$old" | tr '\n' ' ')"
[ -z "$(find "$new" -maxdepth 1 -name 'library.db-*')" ] && pass both-no-stray-log || fail "both-no-stray-log: $(find "$new" -maxdepth 1 -name 'library.db-*' | tr '\n' ' ')"
grep -q "$old/library.db-wal" "$b/stderr" && grep -q "$old/library.db[^-]" "$b/stderr" && pass both-named || fail "both-named: $(head -n 4 "$b/stderr" | tr '\n' ' ')"

# --- A log with no database beside it goes nowhere.
box log-alone
library "$new/library.db" "$b/music"
library "$b/else/library.db" "$b/music"
dies_after "$b/else/library.db" "UPDATE tracks SET play_count = 55;" 55
mkdir -p "$old"; cp "$b/else/library.db-wal" "$b/else/library.db-shm" "$old/"
before=$(rows "$new/library.db"); oldlog=$(sum "$old/library.db-wal")
run; rc=$?
[ "$rc" = 0 ] && pass alone-start || fail "alone-start rc=$rc"
[ "$(rows "$new/library.db")" = "$before" ] && sound "$new/library.db" && pass alone-library-unchanged || fail alone-library-unchanged
[ "$(sum "$old/library.db-wal")" = "$oldlog" ] && pass alone-log-left || fail "alone-log-left: $(ls -A "$old" 2> /dev/null | tr '\n' ' ')"

# --- The same for a rollback journal.
box journal
library "$new/library.db" "$b/music"
library "$old/library.db" "$b/music"
head -c 4096 /dev/urandom > "$old/library.db-journal"
before=$(rows "$new/library.db"); j=$(sum "$old/library.db-journal")
run; rc=$?
[ "$rc" = 0 ] && pass journal-start || fail "journal-start rc=$rc"
[ ! -e "$new/library.db-journal" ] && [ "$(sum "$old/library.db-journal")" = "$j" ] && pass journal-left || fail "journal-left: $(ls -A "$new" "$old" | tr '\n' ' ')"
[ "$(rows "$new/library.db")" = "$before" ] && sound "$new/library.db" && pass journal-library-unchanged || fail journal-library-unchanged

# --- A stray log is in the new place and no library: the older library is not put beside it, and the
# program does not start on an empty library while that one waits.
box stray-log-in-the-new-place
library "$b/else/library.db" "$b/music"
dies_after "$b/else/library.db" "UPDATE tracks SET play_count = 66;" 66
cp "$b/else/library.db-wal" "$new/library.db-wal"
library "$old/library.db" "$b/music"
olddb=$(sum "$old/library.db")
run; rc=$?
[ "$rc" = 6 ] && pass stray-refused || fail "stray-refused rc=$rc (want 6): $(head -n 2 "$b/stderr")"
grep -q 'was not taken over' "$b/stderr" && pass stray-says-so || fail "stray-says-so: $(head -n 3 "$b/stderr" | tr '\n' ' ')"
[ ! -e "$new/library.db" ] && [ "$(sum "$old/library.db")" = "$olddb" ] && pass stray-nothing-made || fail "stray-nothing-made: $(ls -A "$new" | tr '\n' ' ')"

# --- The older folder is a link: not followed, not touched, and not started past.
box a-link
library "$b/else/library.db" "$b/music"
ln -s "$b/else" "$old"
olddb=$(sum "$b/else/library.db")
run; rc=$?
[ "$rc" = 6 ] && pass link-refused || fail "link-refused rc=$rc (want 6)"
grep -q 'was not taken over' "$b/stderr" && pass link-says-so || fail "link-says-so: $(head -n 3 "$b/stderr" | tr '\n' ' ')"
[ -L "$old" ] && [ ! -e "$new/library.db" ] && [ "$(sum "$b/else/library.db")" = "$olddb" ] && pass link-untouched || fail "link-untouched: $(ls -A "$new" | tr '\n' ' ')"

# --- What calls itself a library and is not one, with a log beside it: left whole, and not started past.
box not-a-database
mkdir -p "$old"; echo "not a database" > "$old/library.db"; echo "nor a log" > "$old/library.db-wal"
a=$(sum "$old/library.db"); c=$(sum "$old/library.db-wal")
run; rc=$?
[ "$rc" = 6 ] && pass notdb-refused || fail "notdb-refused rc=$rc (want 6)"
[ "$(sum "$old/library.db")" = "$a" ] && [ "$(sum "$old/library.db-wal")" = "$c" ] && [ ! -e "$new/library.db" ] && [ ! -e "$new/library.db-wal" ] \
    && pass notdb-left-whole || fail "notdb-left-whole: $(ls -A "$new" "$old" | tr '\n' ' ')"

# --- Another program has the older library open (an older build, still running): nothing is moved
# under it, and this start ends as any second start does.
box open-elsewhere
library "$old/library.db" "$b/music"
mkdir -p "$old/playlists"; echo x > "$old/playlists/a.m3u8"
sleep 60 < "$old/library.db" &
holder=$!; pids="$pids $holder"
listing=$(cd "$new" && find . | sort | sha256sum)
run; rc=$?
[ "$rc" = 5 ] && pass elsewhere-refused || fail "elsewhere-refused rc=$rc (want 5): $(head -n 2 "$b/stderr")"
grep -q 'already running' "$b/stderr" && pass elsewhere-says-so || fail "elsewhere-says-so: $(head -n 3 "$b/stderr" | tr '\n' ' ')"
[ "$(cd "$new" && find . | grep -v 'profile.lock' | sort | sha256sum)" = "$listing" ] && pass elsewhere-nothing-moved || fail "elsewhere-nothing-moved: $(cd "$new" && find . | tr '\n' ' ')"
kill -9 "$holder" 2> /dev/null; wait "$holder" 2> /dev/null
run; rc=$?
[ "$rc" = 0 ] && [ "$("$ctl" library --db "$new/library.db" count 2> /dev/null)" = 3 ] && [ -f "$new/playlists/a.m3u8" ] && pass elsewhere-then-taken || fail "elsewhere-then-taken rc=$rc"

echo "cli_app_adopt: $fails failed"
[ "$fails" -eq 0 ]
