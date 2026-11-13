#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_tagwrite.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
#
# The judge here is ffprobe and ffmpeg, not linuxbeectl's own reader: after
# linuxbeectl writes a tag, ffprobe must see it, and the audio packets must
# be byte-for-byte what they were. ffmpeg is a required tool of this project.
# Expected values: docs/spec/tag-writing.md (W-CLI and the rules named).
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-tagwrite.XXXXXX") || exit 2
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT

command -v ffprobe >/dev/null 2>&1 && command -v ffmpeg >/dev/null 2>&1 || {
    echo "FAIL ffmpeg and ffprobe are required for this test"; exit 1; }

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

# check NAME WANT_RC WANT_STDOUT ARGS...
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

tags_of() { ffprobe -v error -show_entries format_tags:stream_tags -of default=nw=1 "$1"; }
# has NAME FILE KEY VALUE: ffprobe reports KEY (any case) with exactly VALUE
has() {
    if tags_of "$2" | grep -i -q -x -F "TAG:$3=$4"; then pass "$1"; else
        fail "$1: ffprobe does not show $3=$4 in $2"; fi
}
hasnt() {
    if tags_of "$2" | grep -i -q "^TAG:$3="; then fail "$1: ffprobe still shows $3"; else pass "$1"; fi
}
audio_md5() { ffmpeg -v error -i "$1" -map 0:a -c copy -f md5 - 2>/dev/null; }

for name in basic.mp3 v23.mp3 v1only.mp3 basic.flac empty.flac basic.ogg basic.opus basic.m4a \
            basic.wav basic.wv; do
    f="$work/$name"
    cp "$data/$name" "$f" && chmod u+w "$f"
    before=$(audio_md5 "$f")
    check "write-$name" 0 "saved" tags write --set "Title=New Title" --set "Artist=Solo" \
        --set "Track#=3" --set "Track Count=9" --set "Year=1999" --clear Album \
        --set-other "MY KEY=my value" "$f"
    has "title-$name" "$f" title "New Title"
    has "artist-$name" "$f" artist "Solo"
    has "date-$name" "$f" "$( [ "$name" = basic.wv ] && echo year || echo date )" "1999"
    hasnt "album-$name" "$f" album
    has "other-$name" "$f" "MY KEY" "my value"
    case "$name" in
        *.flac|*.ogg|*.opus)
            # W-NUMBERS: a Vorbis comment keeps number and count apart
            has "track-$name" "$f" track "3"
            has "count-$name" "$f" TRACKTOTAL "9" ;;
        *)
            has "track-$name" "$f" track "3/9" ;;
    esac
    # W-KEEP: the audio packets did not change
    after=$(audio_md5 "$f")
    if [ -n "$before" ] && [ "$before" = "$after" ]; then pass "audio-$name"; else
        fail "audio-$name: [$before] became [$after]"; fi
    # W-UNCHANGED
    check "again-$name" 0 "unchanged" tags write --set "Title=New Title" --set "Track#=3" "$f"
done

# W-TABLE through ffprobe: Grouping and Publisher land where other programs look.
f="$work/basic.mp3"
check group-mp3 0 "saved" tags write --set "Grouping=G" --set "Publisher=P" "$f"
has group-mp3-seen "$f" grouping "G"
has publisher-mp3-seen "$f" publisher "P"
f="$work/basic.flac"
check group-flac 0 "saved" tags write --set "Grouping=G" --set "Publisher=P" --set "Rating=4" "$f"
has group-flac-seen "$f" CONTENTGROUP "G"
has publisher-flac-seen "$f" ORGANIZATION "P"
has rating-flac-seen "$f" RATING "80"
# W-MULTI: --set twice gives two values; ffprobe joins a Vorbis comment's values with ;
check multi 0 "saved" tags write --set "Genre=One" --set "Genre=Two" "$f"
has multi-seen "$f" GENRE "One;Two"
check multi-read 0 "One
Two" tags read --field Genre "$f"

# W-ID3VER and W-ID3V1 through the first bytes and the last 128.
f="$work/v23.mp3"
[ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = 'ID3003' ] && pass v23-header || fail v23-header
check to-v24 0 "saved" tags write --id3v2.4 --no-id3v1 --set "Title=Four" "$f"
[ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = 'ID3004' ] && pass v24-header || fail v24-header
[ "$(tail -c 128 "$f" | head -c 3)" != "TAG" ] && pass v1-removed || fail v1-removed

# W-PICTURES: ffprobe sees an attached picture as a video stream.
streams() { ffprobe -v error -show_entries stream=codec_name -of csv=p=0 "$1" | tr '\n' ' '; }
f="$work/basic.opus"
printf '\211PNG\r\n\032\n' > "$work/not-really.png"
cp "$data/basic.flac" "$work/pic-source.flac"
check no-pictures 0 "saved" tags write --no-pictures "$work/pic-source.flac"
case "$(streams "$work/pic-source.flac")" in *png*) fail pictures-removed ;; *) pass pictures-removed ;; esac
check picture-bad 2 "" tags write --picture "$data/notaudio.mp3" "$f"
check picture-missing 3 "" tags write --picture "$work/nope.png" "$f"
check picture-both 2 "" tags write --picture "$work/not-really.png" --no-pictures "$f"

# W-MTIME
f="$work/basic.wv"
touch -d '2001-02-03 04:05:06 UTC' "$f"
check keep-mtime 0 "saved" tags write --keep-mtime --set "Title=Kept" "$f"
[ "$(date -u -r "$f" +%Y-%m-%d)" = "2001-02-03" ] && pass mtime-kept || fail mtime-kept

# W-INVALID and W-STATUS through W-CLI
f="$work/basic.m4a"
keep=$(cksum < "$f")
check bad-field 2 "" tags write --set "Nope=x" "$f"
check bad-rating 2 "" tags write --set "Rating=9" --set "Title=Other" "$f"
check bad-pair 2 "" tags write --set "Title" "$f"
check no-file 2 "" tags write --set "Title=x"
[ "$(cksum < "$f")" = "$keep" ] && pass invalid-wrote-nothing || fail invalid-wrote-nothing
err=$("$bin" tags write --set "Nope=x" --set "Rating=9" "$f" 2>&1 >/dev/null)
[ "$(printf '%s\n' "$err" | grep -c .)" = 2 ] && pass invalid-lists-each || fail "invalid-lists-each: [$err]"
check missing 3 "" tags write --set "Title=x" "$work/nope.mp3"
cp "$data/notaudio.mp3" "$work/notaudio.mp3"
check not-audio 3 "" tags write --set "Title=x" "$work/notaudio.mp3"
chmod a-w "$f"
check read-only 3 "" tags write --set "Title=Locked" "$f"
err=$("$bin" tags write --set "Title=Locked" "$f" 2>&1 >/dev/null)
[ "$err" = "not writable: $f" ] && pass read-only-message || fail "read-only-message: [$err]"
[ "$(cksum < "$f")" = "$keep" ] && pass read-only-wrote-nothing || fail read-only-wrote-nothing
# the read command still works
check read-still-works 0 "New Title" tags read --field Title "$f"

echo "cli_tagwrite: $fails failed"
[ "$fails" -eq 0 ]
