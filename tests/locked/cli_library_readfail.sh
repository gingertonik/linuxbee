#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# A library that opens but whose tracks cannot be read: `count`, `list` and `import` say so and exit 3,
# never an empty or a shorter answer. Usage: cli_library_readfail.sh /path/to/linuxbeectl
# (LINUXBEE_TEST_DATA must be set). Rules: docs/spec/library.md, L-READFAIL 3 and L-CLI.
# The reads are made to fail by overwriting pages of a library made here, while no program has it open.
# Everything happens in folders of this test's own.
set -u
bin=$1
data="${LINUXBEE_TEST_DATA:?}"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-readfail.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
sqlite3 :memory: "SELECT count(*) FROM dbstat;" > /dev/null 2>&1 \
    || { echo "FAIL this test needs a sqlite3 program with the dbstat table"; exit 1; }
export QT_QPA_PLATFORM=offscreen
export XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$work/music"
said() { head -n 1 "$work/err"; }
says() { # the first line on standard error is the reading's words
    case "$(said)" in
        "linuxbeectl: the library could not be read: "*malformed*) pass "$1" ;;
        *) fail "$1 [$(said)]" ;;
    esac
}

# --- A library of 300 tracks, made by the program, then copies of it with pages overwritten.
i=1
while [ "$i" -le 300 ]; do
    cp "$data/tags/basic.mp3" "$work/music/t$(printf %03d "$i").mp3"
    i=$((i + 1))
done
good="$work/good.db"
out=$("$bin" library --db "$good" scan --now 2026-11-13T12:00:00Z "$work/music" 2> /dev/null)
[ "$out" = "added 300, updated 0, unchanged 0, missing 0, found again 0, not music 0" ] \
    && pass setup-scan || fail "setup-scan [$out]"
sqlite3 "$good" "PRAGMA wal_checkpoint(TRUNCATE);" > /dev/null 2>&1
rm -f "$good-wal" "$good-shm"
size=$(sqlite3 "$good" "PRAGMA page_size;")
leaf() { sqlite3 "$good" "SELECT pageno FROM dbstat WHERE name = 'tracks' AND pagetype = 'leaf' ORDER BY path $1 LIMIT 1;"; }
first=$(leaf ASC)
last=$(leaf DESC)
indexes=$(sqlite3 "$good" "SELECT d.pageno FROM dbstat d JOIN sqlite_master m ON m.name = d.name WHERE m.type = 'index' AND m.tbl_name = 'tracks';")
[ -n "$first" ] && [ -n "$last" ] && [ "$first" != "$last" ] && [ -n "$indexes" ] \
    && pass setup-pages || fail "setup-pages [$first $last $indexes]"
damage() { # FILE PAGE...: each page overwritten by bytes no page has
    f=$1
    shift
    cp "$good" "$f"
    for p in "$@"; do
        head -c "$size" /dev/zero | tr '\000' '\377' | dd of="$f" bs="$size" seek=$((p - 1)) conv=notrunc status=none
    done
}
# Every read of the tracks fails, by whatever way the database takes: the first leaf and every index.
all="$work/all.db"
# shellcheck disable=SC2086
damage "$all" "$first" $indexes
# A list read in order fails part way: only the last leaf.
partway="$work/partway.db"
damage "$partway" "$last"
sqlite3 "$all" "SELECT count(*) FROM sqlite_master;" > /dev/null 2>&1 && pass fixture-opens || fail fixture-opens
sqlite3 "$all" "SELECT path FROM tracks;" > /dev/null 2>&1 && fail fixture-damaged || pass fixture-damaged
sum_all=$(sha256sum < "$all")
sum_partway=$(sha256sum < "$partway")

# --- The library that can be read answers in full (passes before the change too).
out=$("$bin" library --db "$good" count 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ "$out" = 300 ] && pass good-count || fail "good-count rc=$rc [$out]"
n=$("$bin" library --db "$good" list 2> /dev/null | wc -l)
[ "$n" = 300 ] && pass good-list || fail "good-list [$n]"

# --- count, list and list --category: nothing on standard output, the words, exit 3.
out=$("$bin" library --db "$all" count 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && [ -z "$out" ] && pass count-refused || fail "count-refused rc=$rc [$out]"
says count-says
out=$("$bin" library --db "$all" list 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && [ -z "$out" ] && pass list-refused || fail "list-refused rc=$rc [$(echo "$out" | wc -l) lines]"
says list-says
out=$("$bin" library --db "$all" list --category Music 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && [ -z "$out" ] && pass list-category-refused || fail "list-category-refused rc=$rc [$(echo "$out" | wc -l) lines]"
says list-category-says
# A list that fails part way prints no line of it: never a shorter list.
out=$("$bin" library --db "$partway" list 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && [ -z "$out" ] && pass list-partway-refused || fail "list-partway-refused rc=$rc [$(echo "$out" | wc -l) lines]"
says list-partway-says

# --- import: plans nothing, prints nothing, writes nothing, with or without --apply.
export_file="$data/import/library.xml"
out=$("$bin" library --db "$all" import "$export_file" 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && [ -z "$out" ] && pass import-refused || fail "import-refused rc=$rc [$(echo "$out" | head -n 2 | tr '\n' '|')]"
says import-says
out=$("$bin" library --db "$all" import "$export_file" --apply 2> "$work/err"); rc=$?
[ "$rc" = 3 ] && [ -z "$out" ] && pass import-apply-refused || fail "import-apply-refused rc=$rc [$(echo "$out" | head -n 2 | tr '\n' '|')]"
says import-apply-says

# --- The damaged files are as they were: reading wrote nothing.
rm -f "$all-wal" "$all-shm" "$partway-wal" "$partway-shm"
[ "$(sha256sum < "$all")" = "$sum_all" ] && pass all-left || fail all-left
[ "$(sha256sum < "$partway")" = "$sum_partway" ] && pass partway-left || fail partway-left

echo "cli_library_readfail: $fails failed"
[ "$fails" = 0 ]
