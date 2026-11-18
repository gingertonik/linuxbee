#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_playlist_out.sh /path/to/linuxbeectl
# Corrected after the review of fix-87 (the block marked so): a link whose own text goes through "..".
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

# --- The correction (the review of fix-87): the path written is the path checked. ---
NOTHING="it is a link to nothing"
# kept LABEL FILE SUM: the file has the bytes it had
kept() { [ "$(sum "$2" 2> /dev/null)" = "$3" ] && pass "$1" || fail "$1"; }

# A link at OUT whose own text goes through a folder that is not there, and comes out at IN. To the system
# it leads nowhere; read as text it is IN. With --force it was followed as text, and IN was written.
mine "$work/p/in.m3u"; insum=$(sum "$work/p/in.m3u")
ln -s ../p/gone/../in.m3u "$work/q/dotdot.m3u8"
refused "a link through a folder that is not there" "$THERE" "$work/p/in.m3u" "$work/q/dotdot.m3u8"
refused "a link through a folder that is not there (--force)" "$NOTHING" "$work/p/in.m3u" "$work/q/dotdot.m3u8" --force
[ -L "$work/q/dotdot.m3u8" ] && pass "that link is as it was" || fail "that link is as it was"
rm -f "$work/q/dotdot.m3u8"

# The same link, and the folder in it is a link elsewhere: to the system OUT is another playlist, in r/.
# Forced, that one is written, and IN is not.
mkdir -p "$work/r/x"; printf '/m/other.mp3\n' > "$work/r/in.m3u"
ln -s ../r/x "$work/p/hop"; ln -s ../p/hop/../in.m3u "$work/q/hop.m3u8"
said=$("$bin" playlist convert --force "$work/p/in.m3u" "$work/q/hop.m3u8" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ "$said" = "2 entries" ] && pass "a link through a linked folder, forced: written" || fail "a link through a linked folder, forced: rc $rc [$said]"
kept "a link through a linked folder, forced: IN is as it was" "$work/p/in.m3u" "$insum"
grep -q '/m/b.mp3' "$work/r/in.m3u" && pass "a link through a linked folder, forced: the file the system names was written" || fail "a link through a linked folder, forced: the file the system names was not written"
[ -L "$work/q/hop.m3u8" ] && pass "a link through a linked folder, forced: the link is a link still" || fail "a link through a linked folder, forced: the link is gone"
rm -f "$work/q/hop.m3u8" "$work/p/hop"; rm -rf "$work/r"

# OUT spelled through a linked folder and back. Read as text it is a new file in q/; to the system it is
# IN. The text is the rule: the new file is written, where the text says, and IN is not.
mkdir -p "$work/t"; ln -s ../p/deep "$work/t/sub"
said=$("$bin" playlist convert "$work/p/in.m3u" "$work/t/sub/../in.m3u" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ -s "$work/t/in.m3u" ] && pass "through a linked folder and back: written where the text says" || fail "through a linked folder and back: rc $rc"
kept "through a linked folder and back: IN is as it was" "$work/p/in.m3u" "$insum"
rm -rf "$work/t"

# --force onto a link to nothing is refused: nothing is made where the link points, here or elsewhere.
mkdir -p "$work/e"; ln -s "$work/e/far.m3u8" "$work/q/far.m3u8"
refused "forced onto a link to nothing" "$NOTHING" "$work/p/in.m3u" "$work/q/dangling.m3u8" --force
refused "forced onto a link to nothing in another folder" "$NOTHING" "$work/p/in.m3u" "$work/q/far.m3u8" --force
[ ! -e "$work/q/nowhere.m3u8" ] && [ ! -e "$work/e/far.m3u8" ] && pass "nothing was made where the links point" || fail "a file was made where a link points"
rm -f "$work/q/far.m3u8"; rm -rf "$work/e"

# --force onto a link to a file that is there: the file is written, and the link stays a link.
printf '/m/old.mp3\n' > "$work/q/target.m3u8"; ln -s target.m3u8 "$work/q/tolink.m3u8"
said=$("$bin" playlist convert --force "$work/p/in.m3u" "$work/q/tolink.m3u8" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ -L "$work/q/tolink.m3u8" ] && grep -q '/m/b.mp3' "$work/q/target.m3u8" && pass "forced onto a link to a file: the file is written, the link stays" || fail "forced onto a link to a file: rc $rc"
kept "forced onto a link to a file: IN is as it was" "$work/p/in.m3u" "$insum"
rm -f "$work/q/tolink.m3u8" "$work/q/target.m3u8"

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
