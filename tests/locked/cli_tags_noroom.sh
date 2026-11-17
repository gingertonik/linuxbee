#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# linuxbeectl does not say "saved" for a tag save that could not be written in full.
# Usage: cli_tags_noroom.sh /path/to/linuxbeectl
# Rules: docs/spec/tag-safe-save.md (WS-WRITTEN, Command line). The room is the shell's own limit on
# the size of a file (ulimit -f, in 512-byte blocks), with its signal ignored, in a folder of this test's own.
set -u
ctl=$1
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-noroom.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
{ printf '\377\330\377\340\000\020JFIF\000'; head -c 300000 /dev/urandom; } > "$work/big.jpg"
for name in basic.mp3 basic.flac basic.m4a basic.ogg basic.wav; do
    f="$work/$name"; cp "$data/$name" "$f"; chmod u+w "$f"
    before=$(sha256sum < "$f")
    blocks=$(( $(wc -c < "$f") / 512 + 16 ))
    out=$(sh -c "trap '' XFSZ; ulimit -f $blocks; exec \"\$0\" tags write --picture \"\$1\" --set 'Title=No Room' \"\$2\"" "$ctl" "$work/big.jpg" "$f" 2> "$work/err"); rc=$?
    [ "$rc" != 0 ] && pass "$name-status" || fail "$name-status: exit 0, said [$out]"
    echo "$out" | grep -qi 'saved' && fail "$name-says-saved: [$out]" || pass "$name-not-saved"
    grep -q "could not save: $f" "$work/err" && pass "$name-says-why" || fail "$name-says-why: [$(head -n 2 "$work/err" | tr '\n' ' ')]"
    [ "$(sha256sum < "$f")" = "$before" ] && pass "$name-untouched" || fail "$name-untouched: $(wc -c < "$f") bytes now"
    [ "$(ls -A "$work" | grep -c lbsave)" = 0 ] && pass "$name-nothing-left" || fail "$name-nothing-left: $(ls -A "$work" | tr '\n' ' ')"
    # with room, the same command saves
    out=$("$ctl" tags write --picture "$work/big.jpg" --set 'Title=No Room' "$f" 2>&1); rc=$?
    [ "$rc" = 0 ] && [ "$(wc -c < "$f")" -gt 300000 ] && pass "$name-with-room" || fail "$name-with-room: rc=$rc [$out]"
done
echo "cli_tags_noroom: $fails failed"
[ "$fails" -eq 0 ]
