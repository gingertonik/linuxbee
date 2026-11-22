#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# A command that only reads, on a disk mounted read-only (L-READ "Its words", fix-d2): with the side
# files beside the library it reads; without them it says the disk, not "unable to open database file".
# Usage: cli_library_read_rodisk.sh /path/to/linuxbeectl   (LINUXBEE_TEST_DATA set)
# Everything happens in folders of this test's own; the mount is in a namespace of its own (no root).
set -u
bin=$1
data="${LINUXBEE_TEST_DATA:?}"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-rodisk.XXXXXX") || exit 2
trap 'chmod -R u+w "$work" 2> /dev/null; rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
command -v sqlite3 > /dev/null || { echo "FAIL this test needs the sqlite3 program"; exit 1; }
export QT_QPA_PLATFORM=offscreen
export XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$work/music" "$work/bare" "$work/sides"
cp "$data/tags/basic.mp3" "$work/music/one.mp3"
now=2026-11-13T12:00:00Z
"$bin" library --db "$work/made.db" scan --now "$now" "$work/music" > /dev/null 2>&1
sqlite3 "$work/made.db" "PRAGMA wal_checkpoint(TRUNCATE);" > /dev/null 2>&1
# bare/: the library file alone. sides/: the file with its two side files, copied while it is open.
cp "$work/made.db" "$work/bare/library.db"
printf 'SELECT 1;\n.shell cp "%s" "%s-wal" "%s-shm" "%s/"\n' "$work/made.db" "$work/made.db" "$work/made.db" "$work/sides" \
    | sqlite3 "$work/made.db" > /dev/null 2>&1
for f in made.db made.db-wal made.db-shm; do mv "$work/sides/$f" "$work/sides/library.db${f#made.db}"; done
[ -f "$work/sides/library.db-shm" ] || { echo "FAIL the side files could not be made"; exit 1; }
before=$(sha256sum < "$work/bare/library.db")

if ! command -v unshare > /dev/null || ! unshare -rm true 2> /dev/null; then
    echo "SKIPPED: this system lets no user make a mount namespace (unshare -rm)"
    echo "cli_library_read_rodisk: 0 failed"
    exit 0
fi
cat > "$work/ro.sh" << 'EOF'
set -u
for d in "$1/bare" "$1/sides"; do
    mount --bind "$d" "$d" && mount -o remount,ro,bind "$d" || { echo "NOMOUNT"; exit 0; }
done
"$2" library --db "$1/bare/library.db" count > "$1/bare.out" 2> "$1/bare.err"; echo "bare-count rc=$?"
"$2" library --db "$1/bare/library.db" list > "$1/list.out" 2> "$1/list.err"; echo "bare-list rc=$?"
"$2" library --db "$1/sides/library.db" count > "$1/sides.out" 2> "$1/sides.err"; echo "sides-count rc=$?"
EOF
res=$(unshare -rm sh "$work/ro.sh" "$work" "$bin" 2> /dev/null)
if [ "$res" = NOMOUNT ]; then
    echo "SKIPPED: a read-only bind mount could not be made here"
    echo "cli_library_read_rodisk: 0 failed"
    exit 0
fi
lib="$work/bare/library.db"
want="linuxbeectl: cannot open the library file \"$lib\": the library cannot be read here: the disk it is on is mounted read-only, and the database needs its side files (-wal, -shm) beside it"
printf '%s\n' "$res" | grep -qx 'bare-count rc=3' && pass bare-count-refused || fail "bare-count-refused [$res]"
[ "$(head -n 1 "$work/bare.err")" = "$want" ] && pass bare-count-says-the-disk || fail "bare-count-says-the-disk [$(head -n 1 "$work/bare.err")]"
[ ! -s "$work/bare.out" ] && pass bare-count-prints-nothing || fail "bare-count-prints-nothing [$(cat "$work/bare.out")]"
printf '%s\n' "$res" | grep -qx 'bare-list rc=3' && pass bare-list-refused || fail "bare-list-refused [$res]"
[ "$(head -n 1 "$work/list.err")" = "$want" ] && pass bare-list-says-the-disk || fail "bare-list-says-the-disk [$(head -n 1 "$work/list.err")]"
# Guard: with its side files beside it, a library on a read-only disk is read.
printf '%s\n' "$res" | grep -qx 'sides-count rc=0' && pass sides-read || fail "sides-read [$res] [$(head -n 1 "$work/sides.err")]"
[ "$(cat "$work/sides.out")" = 1 ] && pass sides-count || fail "sides-count [$(cat "$work/sides.out")]"
[ "$(sha256sum < "$work/bare/library.db")" = "$before" ] && pass bare-file-unchanged || fail bare-file-unchanged

echo "cli_library_read_rodisk: $fails failed"
[ "$fails" = 0 ]
