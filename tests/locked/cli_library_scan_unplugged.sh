#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# A scan says which library folder looks unplugged and leaves its tracks as they are; a folder
# emptied on purpose is marked with --empty-means-gone (L-CLI, L-UNPLUGGED, fix-m1).
# Usage: cli_library_scan_unplugged.sh /path/to/linuxbeectl   (LINUXBEE_TEST_DATA set)
# Everything happens in folders of this test's own.
set -u
bin=$1
data="${LINUXBEE_TEST_DATA:?}"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-unplugged.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
export QT_QPA_PLATFORM=offscreen
export XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache"
: > "$work/fstab"
export LINUXBEE_FSTAB="$work/fstab"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$work/a" "$work/b"
cp "$data/tags/basic.mp3" "$work/a/one.mp3"
cp "$data/tags/basic.flac" "$work/b/two.flac"
cp "$data/tags/basic.ogg" "$work/b/three.ogg"
now=2026-11-13T12:00:00Z
lib="$work/library.db"
"$bin" library --db "$lib" scan --now "$now" "$work/a" "$work/b" > /dev/null 2>&1
rm "$work/b/two.flac" "$work/b/three.ogg"

out=$("$bin" library --db "$lib" scan --now "$now" "$work/a" "$work/b" 2> "$work/err"); rc=$?
[ "$rc" = 0 ] && pass unplugged-rc || fail "unplugged-rc rc=$rc [$(cat "$work/err")]"
line=$(printf '%s\n' "$out" | head -n 1)
[ "$line" = "added 0, updated 0, unchanged 1, missing 0, found again 0, not music 0" ] \
    && pass unplugged-counts || fail "unplugged-counts [$line]"
want=$(printf '1 folder looks unplugged (not mounted, or empty where it held tracks); its tracks are left as they are:\n  %s\na folder emptied on purpose: scan it again with --empty-means-gone' "$work/b")
said=$(printf '%s\n' "$out" | sed -n '2,4p')
[ "$said" = "$want" ] && pass unplugged-named || fail "unplugged-named [$said]"
missing=$(sqlite3 "$lib" "SELECT count(*) FROM tracks WHERE missing <> 0;")
[ "$missing" = 0 ] && pass unplugged-none-missing || fail "unplugged-none-missing [$missing]"

# Emptied on purpose: the option marks them, as a scan did before.
out=$("$bin" library --db "$lib" scan --empty-means-gone --now "$now" "$work/a" "$work/b" 2> "$work/err"); rc=$?
[ "$rc" = 0 ] && pass emptied-rc || fail "emptied-rc rc=$rc [$(cat "$work/err")]"
line=$(printf '%s\n' "$out" | head -n 1)
[ "$line" = "added 0, updated 0, unchanged 1, missing 2, found again 0, not music 0" ] \
    && pass emptied-counts || fail "emptied-counts [$line]"
[ "$(printf '%s\n' "$out" | wc -l)" = 1 ] && pass emptied-one-line || fail "emptied-one-line [$out]"
missing=$(sqlite3 "$lib" "SELECT count(*) FROM tracks WHERE missing <> 0;")
[ "$missing" = 2 ] && pass emptied-marked || fail "emptied-marked [$missing]"

echo "cli_library_scan_unplugged: $fails failed"
[ "$fails" = 0 ]
