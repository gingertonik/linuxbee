#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# A library that cannot be written to says where the fault is (L-WORDS, fix-70d): a writable file in
# a folder that cannot be written to names the folder; a disk mounted read-only says so, and no chmod.
# Usage: cli_library_words_where.sh /path/to/linuxbeectl   (LINUXBEE_TEST_DATA set)
# Everything happens in folders of this test's own.
set -u
bin=$1
data="${LINUXBEE_TEST_DATA:?}"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-where.XXXXXX") || exit 2
trap 'chmod -R u+w "$work" 2> /dev/null; rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
export QT_QPA_PLATFORM=offscreen
export XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$work/music" "$work/lib"
cp "$data/tags/basic.mp3" "$work/music/one.mp3"
now=2026-11-13T12:00:00Z
lib="$work/lib/library.db"
"$bin" library --db "$lib" scan --now "$now" "$work/music" > /dev/null 2>&1
sqlite3 "$lib" "PRAGMA wal_checkpoint(TRUNCATE);" > /dev/null 2>&1
rm -f "$lib-wal" "$lib-shm"
cp "$data/tags/basic.flac" "$work/music/two.flac"

# --- A writable library file in a folder that cannot be written to: the folder is named.
chmod a-w "$work/lib"
if [ -w "$work/lib" ]; then
    echo "SKIPPED: this user may write to a read-only folder"
else
    before=$(sha256sum < "$lib")
    out=$("$bin" library --db "$lib" scan --now "$now" "$work/music" 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && [ -z "$out" ] && pass folder-refused || fail "folder-refused rc=$rc [$out]"
    said=$(head -n 1 "$work/err")
    want="linuxbeectl: cannot open the library file \"$lib\": The library cannot be written to: $lib (its folder cannot be written to, and the database needs its side files (-wal, -shm) beside it)"
    [ "$said" = "$want" ] && pass folder-says-the-folder || fail "folder-says-the-folder [$said]"
    [ "$(sha256sum < "$lib")" = "$before" ] && pass folder-file-unchanged || fail folder-file-unchanged
fi
chmod u+w "$work/lib"

# --- A disk mounted read-only (a bind mount in a namespace of this test's own): it says the disk,
# and gives no chmod.
if ! command -v unshare > /dev/null || ! unshare -rm true 2> /dev/null; then
    echo "SKIPPED: this system lets no user make a mount namespace (unshare -rm)"
else
    cat > "$work/ro.sh" << 'EOF'
set -u
mount --bind "$1" "$1" && mount -o remount,ro,bind "$1" || { echo "NOMOUNT"; exit 0; }
"$2" library --db "$1/library.db" scan --now "$3" "$4" > /dev/null 2> "$5"
echo "rc=$?"
EOF
    res=$(unshare -rm sh "$work/ro.sh" "$work/lib" "$bin" "$now" "$work/music" "$work/err" 2> /dev/null)
    if [ "$res" = NOMOUNT ]; then
        echo "SKIPPED: a read-only bind mount could not be made here"
    else
        [ "$res" = "rc=3" ] && pass mount-refused || fail "mount-refused [$res]"
        said=$(head -n 1 "$work/err")
        want="linuxbeectl: cannot open the library file \"$lib\": The library cannot be written to: $lib (the disk it is on is mounted read-only)"
        [ "$said" = "$want" ] && pass mount-says-the-disk || fail "mount-says-the-disk [$said]"
    fi
fi

echo "cli_library_words_where: $fails failed"
[ "$fails" = 0 ]
