#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_cover.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/covers.md, CV-CLI.
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

mkdir -p "$work/a" "$work/b"
cp "$data/basic.mp3" "$work/a/one.mp3"
cp "$data/v23.mp3" "$work/a/two.mp3"
cp "$data/v23.mp3" "$work/b/three.mp3"
printf 'twelve bytes' > "$work/a/folder.jpg"

out=$("$bin" cover show "$work/a/one.mp3" 2> "$work/err"); rc=$?
is "show embedded: exit" 0 "$rc"
is "show embedded: lines" "source=embedded|file=$work/a/one.mp3|mime=image/png|type=Front Cover|bytes=69" "$(printf '%s' "$out" | tr '\n' '|')"
out=$("$bin" cover show "$work/a/one.mp3" --file-first 2> /dev/null); rc=$?
is "show file first" "0 source=file|file=$work/a/folder.jpg|mime=image/jpeg|type=|bytes=12" "$rc $(printf '%s' "$out" | tr '\n' '|')"
is "show: the folder's file when none is inside" "source=file" "$("$bin" cover show "$work/a/two.mp3" 2> /dev/null | head -n 1)"
out=$("$bin" cover show "$work/b/three.mp3" 2> /dev/null); rc=$?
is "show none" "0 source=none" "$rc $out"
out=$("$bin" cover show "$work/b/gone.mp3" 2> /dev/null); rc=$?
is "show: a file that is not there" "0 source=none" "$rc $out"

out=$("$bin" cover extract "$work/a/one.mp3" "$work/out.png" 2> /dev/null); rc=$?
is "extract embedded" "0 wrote 69 bytes to $work/out.png" "$rc $out"
is "extract embedded: the bytes" "69 PNG" "$(wc -c < "$work/out.png" | tr -d ' ') $(head -c 4 "$work/out.png" | tail -c 3)"
out=$("$bin" cover extract "$work/a/one.mp3" "$work/out.jpg" --file-first 2> /dev/null); rc=$?
is "extract the folder's file" "0 wrote 12 bytes to $work/out.jpg" "$rc $out"
is "extract the folder's file: the bytes" "twelve bytes" "$(cat "$work/out.jpg")"

out=$("$bin" cover extract "$work/a/one.mp3" "$work/out.png" 2> "$work/err"); rc=$?
is "extract: the target exists" "3 " "$rc $out"
grep -q '^failed: .*exists' "$work/err" && pass "extract: exists said" || fail "extract: exists said [$(cat "$work/err")]"
is "extract: the target is left alone" "69" "$(wc -c < "$work/out.png" | tr -d ' ')"
out=$("$bin" cover extract "$work/b/three.mp3" "$work/none.png" 2> "$work/err"); rc=$?
is "extract: no cover" "3 " "$rc $out"
grep -q '^failed: .*no cover' "$work/err" && pass "extract: no cover said" || fail "extract: no cover said [$(cat "$work/err")]"
[ ! -e "$work/none.png" ] && pass "extract: nothing written" || fail "extract: nothing written"
out=$("$bin" cover extract "$work/a/one.mp3" "$work/no/such/folder/x.png" 2> "$work/err"); rc=$?
is "extract: cannot write" "3 " "$rc $out"
grep -q '^failed: ' "$work/err" && pass "extract: cannot write said" || fail "extract: cannot write said [$(cat "$work/err")]"

"$bin" cover > "$work/out" 2> /dev/null; is "usage: nothing" "2 " "$? $(cat "$work/out")"
"$bin" cover hide "$work/a/one.mp3" > "$work/out" 2> /dev/null; is "usage: unknown" "2 " "$? $(cat "$work/out")"
"$bin" cover show > "$work/out" 2> /dev/null; is "usage: show without a file" "2 " "$? $(cat "$work/out")"
"$bin" cover show "$work/a/one.mp3" "$work/a/two.mp3" > "$work/out" 2> /dev/null; is "usage: show with two files" "2 " "$? $(cat "$work/out")"
"$bin" cover extract "$work/a/one.mp3" > "$work/out" 2> /dev/null; is "usage: extract without a target" "2 " "$? $(cat "$work/out")"
"$bin" cover show "$work/a/one.mp3" --nonsense > "$work/out" 2> /dev/null; is "usage: unknown option" "2 " "$? $(cat "$work/out")"

echo "cli_cover: $fails failed"
[ "$fails" = 0 ]
