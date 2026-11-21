#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_library_refused.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# A scan the database refuses, and a library that cannot be written to, as linuxbeectl says them.
# Expected values: docs/spec/library.md, L-CLI and L-OPEN. Works in a temporary folder. Needs sqlite3,
# to plant the refusal from outside the program.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d)
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }

mkdir -p "$work/music" "$work/lib"
cp "$data/basic.mp3" "$data/basic.flac" "$data/basic.ogg" "$work/music/"
db="$work/lib/library.db"
now="--now 2026-11-13T12:00:00Z"

# (fix-ro1) L-READ: count makes no library, so a scan of an empty folder makes it.
mkdir -p "$work/nothing"
# shellcheck disable=SC2086
"$bin" library --db "$db" scan $now "$work/nothing" > /dev/null 2>&1
out=$("$bin" library --db "$db" count 2> /dev/null); [ "$out" = 0 ] && pass made || fail "made [$out]"
sqlite3 "$db" "CREATE TRIGGER refuse_one BEFORE INSERT ON tracks BEGIN SELECT RAISE(ROLLBACK, 'refused for the test'); END" \
    || fail "the trigger could not be planted"

# L-CLI: a scan that could not be stored. No result line (it would say "added 0", which is not what
# happened), the reason on standard error, exit 3.
# shellcheck disable=SC2086
out=$("$bin" library --db "$db" scan $now "$work/music" 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && pass refused-exit || fail "refused-exit rc=$rc (want 3)"
[ -z "$out" ] && pass refused-no-result-line || fail "refused-no-result-line stdout=[$out]"
grep -q 'not stored' "$work/err" && pass refused-says-so || fail "refused-says-so [$(cat "$work/err")]"
grep -q 'refused for the test' "$work/err" && pass refused-says-why || fail "refused-says-why [$(cat "$work/err")]"
out=$("$bin" library --db "$db" count 2> /dev/null); [ "$out" = 0 ] && pass refused-stored-nothing || fail "refused-stored-nothing [$out]"

# The same scan with nothing in its way.
sqlite3 "$db" "DROP TRIGGER refuse_one"
# shellcheck disable=SC2086
out=$("$bin" library --db "$db" scan $now "$work/music" 2> "$work/err"); rc=$?
[ "$rc" = 0 ] && [ "$out" = "added 3, updated 0, unchanged 0, missing 0, found again 0, not music 0" ] \
    && pass again || fail "again rc=$rc [$out] [$(cat "$work/err")]"
[ -s "$work/err" ] && fail "again: standard error is not empty [$(cat "$work/err")]" || pass again-quiet

# L-OPEN: the library's folder cannot be written to. (Root may write anywhere: then there is no case.)
chmod a-w "$work/lib"
if touch "$work/lib/probe" 2> /dev/null; then
    rm -f "$work/lib/probe"
    echo "SKIPPED: this user may write to a read-only folder"
else
    out=$("$bin" library --db "$db" count 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && pass readonly-exit || fail "readonly-exit rc=$rc (want 3) [$out]"
    grep -q 'cannot be written' "$work/err" && pass readonly-says-so || fail "readonly-says-so [$(cat "$work/err")]"
    grep -q 'not a linuxBee library' "$work/err" && fail "readonly: it is a linuxBee library [$(cat "$work/err")]" || pass readonly-right-words
    [ "$(find "$work/lib" -type f | wc -l)" = 1 ] && pass readonly-left-nothing || fail "readonly-left-nothing: $(ls "$work/lib")"
fi
chmod u+w "$work/lib"
out=$("$bin" library --db "$db" count 2> /dev/null); [ "$out" = 3 ] && pass writable-again || fail "writable-again [$out]"

echo "cli_library_refused: $fails failed"
[ "$fails" = 0 ]
