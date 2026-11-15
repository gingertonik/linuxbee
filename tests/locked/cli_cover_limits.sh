#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_cover_limits.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/covers.md, CV-FILE (only regular files) and
# CV-CLI (extract writes nothing on failure; a link at OUT exists).
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() {
    label=$1; expected=$2; got=$3
    if [ "$got" = "$expected" ]; then pass "$label"; else fail "$label: got [$got] want [$expected]"; fi
}

# CV-FILE: a name that leads to a device or a FIFO is not a picture file.
mkdir -p "$work/dev" "$work/fifo" "$work/big"
cp "$data/v23.mp3" "$work/dev/t.mp3"
cp "$data/v23.mp3" "$work/fifo/t.mp3"
ln -s /dev/zero "$work/dev/cover.jpg"
mkfifo "$work/pipe"
ln -s "$work/pipe" "$work/fifo/cover.jpg"
is "a link to a device is not a cover" "source=none" "$(timeout 10 "$bin" cover show "$work/dev/t.mp3" 2> /dev/null)"
is "a link to a FIFO is not a cover" "source=none" "$(timeout 10 "$bin" cover show "$work/fifo/t.mp3" 2> /dev/null)"
mkdir -p "$work/devlone"
cp "$data/v23.mp3" "$work/devlone/t.mp3"
ln -s /dev/zero "$work/devlone/cover.jpg"
printf 'eight by' > "$work/devlone/back.png"
is "a link to a device does not hide the lone picture" "file=$work/devlone/back.png" "$(timeout 10 "$bin" cover show "$work/devlone/t.mp3" 2> /dev/null | sed -n 2p)"
timeout 10 "$bin" cover extract "$work/fifo/t.mp3" "$work/fifo.out" > /dev/null 2> "$work/err"; rc=$?
is "extract beside a link to a FIFO: no hang, no cover" "3" "$rc"
grep -q '^failed: .*no cover' "$work/err" && pass "extract beside a FIFO: no cover said" || fail "extract beside a FIFO: no cover said [$(cat "$work/err")]"

# CV-CLI: a symbolic link at OUT exists, even when it leads nowhere.
cp "$data/basic.mp3" "$work/one.mp3"
ln -s "$work/elsewhere.png" "$work/link.png"
out=$(timeout 10 "$bin" cover extract "$work/one.mp3" "$work/link.png" 2> "$work/err"); rc=$?
is "extract onto a dangling link: exit" "3 " "$rc $out"
grep -q '^failed: .*exists' "$work/err" && pass "extract onto a dangling link: exists said" || fail "extract onto a dangling link: exists said [$(cat "$work/err")]"
[ ! -e "$work/elsewhere.png" ] && pass "extract onto a dangling link: nothing written through it" || fail "extract onto a dangling link: nothing written through it"

# CV-CLI: OUT cannot be written (a file size limit): exit 3, a failed: line and no
# wrote line, no OUT left. Standard error goes to the pipe, which the limit does not bound.
both=$( (trap '' XFSZ; ulimit -f 0; exec timeout 10 "$bin" cover extract "$work/one.mp3" "$work/limit0.png" 2>&1) ); rc=$?
is "extract, nothing can be written: exit" "3" "$rc"
is "extract, nothing can be written: what is said" "failed: " "$(printf '%s' "$both" | head -n 1 | cut -c1-8)$(printf '%s' "$both" | grep -c '^wrote' | sed 's/^0$//')"
[ ! -e "$work/limit0.png" ] && pass "extract, nothing can be written: no OUT left" || fail "extract, nothing can be written: no OUT left"

cp "$data/v23.mp3" "$work/big/t.mp3"
head -c 200000 /dev/zero > "$work/big/cover.jpg"
both=$( (trap '' XFSZ; ulimit -f 16; exec timeout 10 "$bin" cover extract "$work/big/t.mp3" "$work/part.png" 2>&1) ); rc=$?
is "extract, the write fails part-way: exit" "3" "$rc"
is "extract, the write fails part-way: what is said" "failed: " "$(printf '%s' "$both" | head -n 1 | cut -c1-8)$(printf '%s' "$both" | grep -c '^wrote' | sed 's/^0$//')"
[ ! -e "$work/part.png" ] && pass "extract, the write fails part-way: no OUT left" || fail "extract, the write fails part-way: no OUT left [$(wc -c < "$work/part.png" | tr -d ' ') bytes]"

echo "cli_cover_limits: $fails failed"
[ "$fails" = 0 ]
