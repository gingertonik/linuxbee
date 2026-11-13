#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_playlist.sh /path/to/linuxbeectl
# Expected values: docs/spec/playlists.md, Y-CLI and the rules each case names.
set -u
bin="$1"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
tab=$(printf '\t')

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
check() {
    label=$1; want_rc=$2; want_out=$3
    shift 3
    out=$("$bin" "$@" 2>/dev/null)
    rc=$?
    if [ "$rc" != "$want_rc" ] || [ "$out" != "$want_out" ]; then
        fail "$label: rc=$rc (want $want_rc) stdout=[$out] (want [$want_out])"
    else
        pass "$label"
    fi
}
same() {
    label=$1; file=$2; want=$3
    if [ "$(cat "$file" 2>/dev/null)" = "$want" ]; then pass "$label"; else
        fail "$label: file holds [$(cat "$file" 2>/dev/null)] want [$want]"; fi
}

mkdir -p "$work/lists" "$work/out/deep"
printf '#EXTM3U\r\n#EXTINF:215,Adele - Hello\r\n../music/a.mp3\r\n\r\nsub\\b.flac\r\n#EXTINF:-1,Radio\r\nhttp://example.invalid/s\r\n' \
    > "$work/lists/in.m3u"

check show 0 "$work/music/a.mp3${tab}215${tab}Adele - Hello
$work/lists/sub/b.flac${tab}-1${tab}
http://example.invalid/s${tab}-1${tab}Radio" playlist show "$work/lists/in.m3u"

check to-pls 0 "3 entries" playlist convert "$work/lists/in.m3u" "$work/out/a.pls"
same to-pls-file "$work/out/a.pls" "[playlist]
File1=$work/music/a.mp3
Title1=Adele - Hello
Length1=215
File2=$work/lists/sub/b.flac
Length2=-1
File3=http://example.invalid/s
Title3=Radio
Length3=-1
NumberOfEntries=3
Version=2"

check to-m3u8-relative 0 "3 entries" playlist convert --relative "$work/out/a.pls" "$work/out/deep/b.M3U8"
same to-m3u8-relative-file "$work/out/deep/b.M3U8" "#EXTM3U
#EXTINF:215,Adele - Hello
../../music/a.mp3
../../lists/sub/b.flac
#EXTINF:-1,Radio
http://example.invalid/s"

check to-plain 0 "3 entries" playlist convert --plain "$work/out/deep/b.M3U8" "$work/out/c.m3u"
same to-plain-file "$work/out/c.m3u" "$work/music/a.mp3
$work/lists/sub/b.flac
http://example.invalid/s"
# the round trip gives what the first file said
check show-again 0 "$work/music/a.mp3${tab}215${tab}Adele - Hello
$work/lists/sub/b.flac${tab}-1${tab}
http://example.invalid/s${tab}-1${tab}Radio" playlist show "$work/out/deep/b.M3U8"

: > "$work/lists/empty.m3u8"
check show-empty 0 "" playlist show "$work/lists/empty.m3u8"
check convert-empty 0 "0 entries" playlist convert "$work/lists/empty.m3u8" "$work/out/e.pls"

# refusals
check no-subcommand 2 "" playlist
check bad-subcommand 2 "" playlist frob "$work/lists/in.m3u"
check show-no-file 2 "" playlist show
check show-two-files 2 "" playlist show "$work/lists/in.m3u" "$work/out/a.pls"
check show-not-a-playlist 2 "" playlist show "$work/lists/in.txt"
check convert-one-file 2 "" playlist convert "$work/lists/in.m3u"
check convert-bad-out 2 "" playlist convert "$work/lists/in.m3u" "$work/out/x.xspf"
[ ! -e "$work/out/x.xspf" ] && pass convert-bad-out-wrote-nothing || fail convert-bad-out-wrote-nothing
check show-missing 3 "" playlist show "$work/lists/none.m3u"
check convert-missing 3 "" playlist convert "$work/lists/none.m3u" "$work/out/y.pls"
[ ! -e "$work/out/y.pls" ] && pass convert-missing-wrote-nothing || fail convert-missing-wrote-nothing
check convert-unwritable 3 "" playlist convert "$work/lists/in.m3u" "$work/no-such-folder/z.pls"

case "$("$bin" --help 2>/dev/null)" in
    *"  playlist "*) pass help-lists-playlist ;;
    *) fail help-lists-playlist ;;
esac

echo "cli_playlist: $fails failed"
[ "$fails" -eq 0 ]
