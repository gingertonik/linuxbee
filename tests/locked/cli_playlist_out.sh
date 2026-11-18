#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_playlist_out.sh /path/to/linuxbeectl
# docs/spec/playlists.md, Y-CLI: `playlist convert` never writes onto the file it reads, and writes onto a
# file that is there only with --force. The fault (the write-path audit, part 4, F4): it wrote over OUT
# without a word, IN included, and a playlist lost its comments and its relative paths while it said "N
# entries".
# Everything happens in a temporary folder, with folders of its own for the program.
set -u
case "$1" in /*) bin="$1" ;; *) bin="$(pwd)/$1" ;; esac
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-plout.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/xdg-data" XDG_CONFIG_HOME="$work/xdg-config" XDG_CACHE_HOME="$work/xdg-cache"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
sum() { cksum < "$1"; }
mine() { printf '\357\273\277#EXTM3U\r\n#PLAYLIST:Mine\r\n# keep me\r\n\r\nsub/a.mp3\r\n/m/b.mp3\r\n' > "$1"; }

# refused LABEL WHY IN OUT [--force]: exit 3, nothing on standard output, the reason said, OUT and IN as they were.
refused() {
    lbl=$1; why=$2; in=$3; out=$4; force=${5:-}
    before=$(sum "$out" 2> /dev/null); inbefore=$(sum "$in")
    # shellcheck disable=SC2086
    said=$("$bin" playlist convert $force "$in" "$out" 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && pass "$lbl: exit 3" || fail "$lbl: rc $rc"
    [ -z "$said" ] && pass "$lbl: nothing on stdout" || fail "$lbl: stdout [$said]"
    [ "$(cat "$work/err")" = "failed: $out: $why" ] && pass "$lbl: says why" || fail "$lbl: stderr [$(cat "$work/err")]"
    [ "$before" = "$(sum "$out" 2> /dev/null)" ] && pass "$lbl: OUT is as it was" || fail "$lbl: OUT was written"
    [ "$inbefore" = "$(sum "$in")" ] && pass "$lbl: IN is as it was" || fail "$lbl: IN was written"
}
SELF="this is the file being read"
THERE="the file is there already (--force writes over it)"

mkdir -p "$work/p/deep" "$work/q"
for force in "" "--force"; do
    mine "$work/p/same.m3u"
    refused "onto itself ($force)" "$SELF" "$work/p/same.m3u" "$work/p/same.m3u" $force
    refused "onto itself, spelled another way ($force)" "$SELF" "$work/p/same.m3u" "$work/p/deep/../same.m3u" $force
    rm -f "$work/l"; ln -s "$work/p" "$work/l"
    refused "onto itself, through a linked folder ($force)" "$SELF" "$work/p/same.m3u" "$work/l/same.m3u" $force
    rm -f "$work/p/link.m3u"; ln -s same.m3u "$work/p/link.m3u"
    refused "onto a link to itself ($force)" "$SELF" "$work/p/same.m3u" "$work/p/link.m3u" $force
    rm -f "$work/p/second.m3u8"; ln "$work/p/same.m3u" "$work/p/second.m3u8"
    refused "onto its second name ($force)" "$SELF" "$work/p/same.m3u" "$work/p/second.m3u8" $force
    rm -f "$work/p/link.m3u" "$work/p/second.m3u8"
done

# Another playlist of the user's is at OUT.
mine "$work/p/in.m3u"
printf '/m/precious.mp3\n' > "$work/q/precious.m3u8"
refused "onto another playlist" "$THERE" "$work/p/in.m3u" "$work/q/precious.m3u8"
: > "$work/q/empty.pls"
refused "onto an empty file" "$THERE" "$work/p/in.m3u" "$work/q/empty.pls"
ln -s "$work/q/nowhere.m3u8" "$work/q/dangling.m3u8"
refused "onto a link to nothing" "$THERE" "$work/p/in.m3u" "$work/q/dangling.m3u8"
[ -L "$work/q/dangling.m3u8" ] && [ ! -e "$work/q/nowhere.m3u8" ] && pass "the link to nothing is as it was" || fail "the link to nothing is as it was"

# With --force it is written over, and only then.
said=$("$bin" playlist convert --force "$work/p/in.m3u" "$work/q/precious.m3u8" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ "$said" = "2 entries" ] && pass "forced: written" || fail "forced: rc $rc [$said]"
grep -q '^/m/b.mp3$' "$work/q/precious.m3u8" && ! grep -q precious.mp3 "$work/q/precious.m3u8" && pass "forced: holds the new list" || fail "forced: holds the new list"

# The other side: an OUT that is not there is written as before, without --force, and with it.
said=$("$bin" playlist convert "$work/p/in.m3u" "$work/q/new.pls" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ "$said" = "2 entries" ] && [ -s "$work/q/new.pls" ] && pass "a new file: written" || fail "a new file: rc $rc [$said]"
said=$("$bin" playlist convert --force "$work/p/in.m3u" "$work/q/new2.pls" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ -s "$work/q/new2.pls" ] && pass "a new file, forced: written" || fail "a new file, forced: rc $rc"
[ "$(ls -A "$work/q" | LC_ALL=C sort | tr '\n' ' ')" = "dangling.m3u8 empty.pls new.pls new2.pls precious.m3u8 " ] && pass "nothing else was left" || fail "nothing else was left: $(ls -A "$work/q" | tr '\n' ' ')"

echo "cli_playlist_out: $fails failed"
[ "$fails" = 0 ]
