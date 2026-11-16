#!/bin/sh
# LOCKED ACCEPTANCE TEST (linuxbeectl tags write keeps the file's ID3v2 version). Written by the reviewer.
# Usage: cli_tagwrite_id3keep.sh /path/to/linuxbeectl   (LINUXBEE_TEST_DATA must be set). W-ID3VER in docs/spec/tag-writing.md.
ctl=$1; data=${LINUXBEE_TEST_DATA:?}
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-id3keep.XXXXXX") || exit 2; trap 'rm -rf "$work"' EXIT
bad=0
pass() { echo "ok   $1"; }
fail() { echo "FAIL $1"; bad=1; }
major() { head -c 4 "$1" | od -An -c | tr -d ' '; }
run() { name=$1; want=$2; shift 2; got=$("$ctl" "$@" 2>&1); rc=$?; [ "$rc" = "$want" ] && pass "$name" || { fail "$name (exit $rc, wanted $want): $got"; }; }

cp "$data/tags/basic.mp3" "$work/a.mp3"   # ID3v2.4
[ "$(major "$work/a.mp3")" = 'ID3004' ] && pass a-starts-v24 || fail a-starts-v24
run a-save 0 tags write --set "Mood=Wistful" --set "Original Year=1998-11-02" "$work/a.mp3"
[ "$(major "$work/a.mp3")" = 'ID3004' ] && pass a-kept-v24 || fail a-kept-v24
[ "$("$ctl" tags read --field Mood "$work/a.mp3")" = "Wistful" ] && pass a-mood || fail a-mood
[ "$("$ctl" tags read --field "Original Year" "$work/a.mp3")" = "1998-11-02" ] && pass a-original-date || fail a-original-date

cp "$data/tags/v23.mp3" "$work/b.mp3"     # ID3v2.3
run b-save 0 tags write --set "Title=Kept" "$work/b.mp3"
[ "$(major "$work/b.mp3")" = 'ID3003' ] && pass b-kept-v23 || fail b-kept-v23

# Asked for ID3v2.3: it is written, it says "saved", and what had no place is named on stderr.
out=$("$ctl" tags write --id3v2.3 --set "Title=Three" "$work/a.mp3" 2>"$work/err"); rc=$?
[ "$rc" = 0 ] && [ "$out" = "saved" ] && pass a-to-v23 || fail "a-to-v23: rc=$rc stdout=[$out]"
[ "$(major "$work/a.mp3")" = 'ID3003' ] && pass a-now-v23 || fail a-now-v23
want='not kept in ID3v2.3: Mood
not kept in ID3v2.3: Original Year: 1998-11-02 became 1998'
[ "$(cat "$work/err")" = "$want" ] && pass a-named || fail "a-named: [$(cat "$work/err")]"
# A value asked for that ID3v2.3 cannot hold is named too, not dropped in silence.
out=$("$ctl" tags write --set "Mood=Again" "$work/a.mp3" 2>"$work/err"); rc=$?
[ "$rc" = 0 ] && [ "$out" = "saved" ] && [ "$(cat "$work/err")" = "not kept in ID3v2.3: Mood" ] && pass a-mood-refused-aloud || fail "a-mood-refused-aloud: rc=$rc [$out] [$(cat "$work/err")]"
# An ordinary save of an ID3v2.3 file names nothing.
out=$("$ctl" tags write --set "Title=Plain" --set "Year=2001-02-03" --set "Genre=Rock" --set "Genre=Folk" --set "Artist=A One" --set "Artist=B Two" "$work/a.mp3" 2>"$work/err"); rc=$?
[ "$rc" = 0 ] && [ ! -s "$work/err" ] && pass a-plain-quiet || fail "a-plain-quiet: [$(cat "$work/err")]"
run b-to-v24 0 tags write --id3v2.4 --set "Title=Four" "$work/b.mp3"
[ "$(major "$work/b.mp3")" = 'ID3004' ] && pass b-now-v24 || fail b-now-v24

# Both at once is a mistake of the caller: usage, exit 2, the file untouched.
before=$(sha256sum < "$work/a.mp3")
run both 2 tags write --id3v2.3 --id3v2.4 --set "Title=No" "$work/a.mp3"
[ "$(sha256sum < "$work/a.mp3")" = "$before" ] && pass both-untouched || fail both-untouched
exit $bad
