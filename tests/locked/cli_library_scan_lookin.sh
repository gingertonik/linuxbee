#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# A scan says which folder it could not look in, and leaves its tracks as they are (L-CLI, L-SCAN,
# fix-d).
# Usage: cli_library_scan_lookin.sh /path/to/linuxbeectl   (LINUXBEE_TEST_DATA set)
# Everything happens in folders of this test's own.
set -u
bin=$1
data="${LINUXBEE_TEST_DATA:?}"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-lookin.XXXXXX") || exit 2
trap 'chmod -R u+rwx "$work" 2> /dev/null; rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
export QT_QPA_PLATFORM=offscreen
export XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$work/music/a" "$work/music/b/deep"
cp "$data/tags/basic.mp3" "$work/music/a/one.mp3"
cp "$data/tags/basic.flac" "$work/music/b/two.flac"
cp "$data/tags/basic.ogg" "$work/music/b/deep/three.ogg"
now=2026-11-13T12:00:00Z
lib="$work/library.db"
"$bin" library --db "$lib" scan --now "$now" "$work/music" > /dev/null 2>&1

chmod 000 "$work/music/b"
if ls "$work/music/b" > /dev/null 2>&1; then
    echo "SKIPPED: this user may look in a folder of mode 000"
else
    out=$("$bin" library --db "$lib" scan --now "$now" "$work/music" 2> "$work/err"); rc=$?
    [ "$rc" = 0 ] && pass lookin-rc || fail "lookin-rc rc=$rc [$(cat "$work/err")]"
    line=$(printf '%s\n' "$out" | head -n 1)
    [ "$line" = "added 0, updated 0, unchanged 1, missing 0, found again 0, not music 0" ] \
        && pass lookin-counts || fail "lookin-counts [$line]"
    want=$(printf 'could not look in 1 folder; the tracks in it are left as they are:\n  %s' "$work/music/b")
    said=$(printf '%s\n' "$out" | sed -n '2,3p')
    [ "$said" = "$want" ] && pass lookin-named || fail "lookin-named [$said]"
    chmod 755 "$work/music/b"
    missing=$(sqlite3 "$lib" "SELECT count(*) FROM tracks WHERE missing <> 0;")
    [ "$missing" = 0 ] && pass lookin-none-missing || fail "lookin-none-missing [$missing]"
fi
[ "$fails" = 0 ]
