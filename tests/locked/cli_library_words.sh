#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# The library's refusals and failed reads are said in a sentence a person reads, by the database's
# code, followed by the database's own words (L-WORDS); a library file that cannot be written to does
# not open for writing. Usage: cli_library_words.sh /path/to/linuxbeectl  (LINUXBEE_TEST_DATA set)
# Rules: docs/spec/library.md, L-WORDS, L-OPEN, L-CLI. Everything happens in folders of this test's own.
set -u
bin=$1
data="${LINUXBEE_TEST_DATA:?}"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-words2.XXXXXX") || exit 2
trap 'kill $(jobs -p) 2> /dev/null; chmod -R u+w "$work" 2> /dev/null; rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
sqlite3 :memory: "SELECT count(*) FROM dbstat;" > /dev/null 2>&1 \
    || { echo "FAIL this test needs a sqlite3 program with the dbstat table"; exit 1; }
export QT_QPA_PLATFORM=offscreen
export XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$work/music" "$work/nothing"
said() { head -n 1 "$work/err"; }
is() { [ "$(said)" = "$2" ] && pass "$1" || fail "$1 [$(said)]"; }
cp "$data/tags/basic.mp3" "$work/music/one.mp3"
now=2026-11-13T12:00:00Z

# --- A library file that cannot be written to, in a folder that can: it does not open for writing,
# and says why; nothing is left beside it. Reading it still works.
ro="$work/ro.db"
"$bin" library --db "$ro" scan --now "$now" "$work/music" > /dev/null 2>&1
sqlite3 "$ro" "PRAGMA wal_checkpoint(TRUNCATE);" > /dev/null 2>&1
rm -f "$ro-wal" "$ro-shm"
chmod a-w "$ro"
if [ -w "$ro" ]; then
    echo "SKIPPED: this user may write to a read-only file"
else
    before=$(sha256sum < "$ro")
    out=$("$bin" library --db "$ro" scan --now "$now" "$work/music" 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && [ -z "$out" ] && pass ro-file-refused || fail "ro-file-refused rc=$rc [$out]"
    is ro-file-says-why "linuxbeectl: cannot open the library file \"$ro\": The library cannot be written to: $ro (the library file is read-only; chmod u+w on it makes it writable)"
    [ ! -e "$ro-wal" ] && [ ! -e "$ro-shm" ] && pass ro-file-left-nothing || fail ro-file-left-nothing
    [ "$(sha256sum < "$ro")" = "$before" ] && pass ro-file-unchanged || fail ro-file-unchanged
    out=$("$bin" library --db "$ro" count 2> "$work/err"); rc=$?
    [ "$rc" = 0 ] && [ "$out" = 1 ] && pass ro-file-reads || fail "ro-file-reads rc=$rc [$out] [$(said)]"
fi
chmod u+w "$ro"

# --- A library another program is writing: the scan waits, then says who holds it, in words.
held="$work/held.db"
"$bin" library --db "$held" scan --now "$now" "$work/nothing" > /dev/null 2>&1
( echo "BEGIN IMMEDIATE;"; sleep 20; echo "ROLLBACK;" ) | sqlite3 "$held" > /dev/null 2>&1 &
sleep 0.5
out=$(timeout 40 "$bin" library --db "$held" scan --now "$now" "$work/music" 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && [ -z "$out" ] && pass held-refused || fail "held-refused rc=$rc [$out]"
is held-says-who "linuxbeectl: the scan was not stored: another program is using the library: database is locked"
wait

# --- A damaged library: the words name the damage first.
good="$work/good.db"
"$bin" library --db "$good" scan --now "$now" "$work/music" > /dev/null 2>&1
sqlite3 "$good" "PRAGMA wal_checkpoint(TRUNCATE);" > /dev/null 2>&1
rm -f "$good-wal" "$good-shm"
size=$(sqlite3 "$good" "PRAGMA page_size;")
pages=$(sqlite3 "$good" "SELECT d.pageno FROM dbstat d JOIN sqlite_master m ON m.name = d.name WHERE m.tbl_name = 'tracks';")
bad="$work/bad.db"
cp "$good" "$bad"
for p in $pages; do
    head -c "$size" /dev/zero | tr '\000' '\377' | dd of="$bad" bs="$size" seek=$((p - 1)) conv=notrunc status=none
done
out=$("$bin" library --db "$bad" count 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && [ -z "$out" ] && pass damaged-refused || fail "damaged-refused rc=$rc [$out]"
is damaged-says-so "linuxbeectl: the library could not be read: the library file is damaged: database disk image is malformed"

# --- A file that is no database at all: no library, in the database's words alone.
text="$work/text.db"
i=0
while [ "$i" -lt 200 ]; do printf 'this is not a database at all\n'; i=$((i + 1)); done > "$text"
out=$("$bin" library --db "$text" count 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && [ -z "$out" ] && pass text-read-refused || fail "text-read-refused rc=$rc [$out]"
is text-read-says "linuxbeectl: cannot open the library file \"$text\": That file is not a linuxBee library: file is not a database"
out=$("$bin" library --db "$text" scan --now "$now" "$work/music" 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && [ -z "$out" ] && pass text-write-refused || fail "text-write-refused rc=$rc [$out]"
is text-write-says "linuxbeectl: cannot open the library file \"$text\": That file is not a linuxBee library: file is not a database"

# --- The window on a read-only library file (L-WORDS, SH-MAIN): it does not start. It says why, names
# the file and the fix, and touches nothing. (Opening it read-only instead is not in 1.0: the folder
# monitor's scans and every play would each be refused.)
app="${LINUXBEE_APP:-}"
if [ -z "$app" ]; then
    fail "app: LINUXBEE_APP is not set"
else
    box="$work/box"
    mkdir -p "$box/config" "$box/data/linuxbee" "$box/cache" "$box/tmp"
    lib="$box/data/linuxbee/library.db"
    "$bin" library --db "$lib" scan --now "$now" "$work/music" > /dev/null 2>&1
    sqlite3 "$lib" "PRAGMA wal_checkpoint(TRUNCATE);" > /dev/null 2>&1
    rm -f "$lib-wal" "$lib-shm"
    chmod a-w "$lib"
    if [ -w "$lib" ]; then
        echo "SKIPPED: this user may write to a read-only file"
    else
        before=$(sha256sum < "$lib")
        XDG_CONFIG_HOME="$box/config" XDG_DATA_HOME="$box/data" XDG_CACHE_HOME="$box/cache" TMPDIR="$box/tmp" \
            timeout 20 "$app" --screenshot "$box/w.png" > /dev/null 2> "$work/err"; rc=$?
        [ "$rc" = 4 ] && pass app-does-not-start || fail "app-does-not-start rc=$rc"
        [ ! -e "$box/w.png" ] && pass app-shows-no-window || fail app-shows-no-window
        is app-says-why "linuxbee: library cannot be opened: The library cannot be written to: $lib (the library file is read-only; chmod u+w on it makes it writable)"
        [ "$(sha256sum < "$lib")" = "$before" ] && [ ! -e "$lib-wal" ] && pass app-touches-nothing || fail app-touches-nothing
    fi
    chmod u+w "$lib"
fi

echo "cli_library_words: $fails failed"
[ "$fails" = 0 ]
