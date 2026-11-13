#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_gain.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/replay-gain.md, N-CLI. Works on copies in a
# temporary folder. What a file holds afterwards is judged by ffprobe, and
# its sound by a checksum of the decoded audio.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/sound"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

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
# like check, but the output is matched against an extended regular expression, line by line
check_re() {
    label=$1; want_rc=$2; want_re=$3
    shift 3
    out=$("$bin" "$@" 2>/dev/null)
    rc=$?
    if [ "$rc" = "$want_rc" ] && printf '%s\n' "$out" | tr '\n' '|' | grep -Eq "^${want_re}\$"; then
        pass "$label"
    else
        fail "$label: rc=$rc (want $want_rc) stdout=[$out] (want to match [$want_re])"
    fi
}
tag() {
    ffprobe -v error -show_entries format_tags -of default=nw=1 "$1" \
        | awk -F= -v k="TAG:$2" 'tolower($1) == tolower(k) { sub(/^[^=]*=/, ""); print; exit }'
}
audio() { ffmpeg -nostdin -v error -i "$1" -map 0:a:0 -f md5 - 2>/dev/null; }
matches() {
    label=$1; re=$2; got=$3
    if printf '%s' "$got" | grep -Eq "^${re}\$"; then pass "$label"; else fail "$label: got [$got] want to match [$re]"; fi
}
is() {
    label=$1; want=$2; got=$3
    if [ "$got" = "$want" ]; then pass "$label"; else fail "$label: got [$got] want [$want]"; fi
}
fresh() { rm -rf "$work/m"; mkdir -p "$work/m"; cp "$data/tone.flac" "$data/hires.flac" "$work/m/"; }
T="$work/m/tone.flac"
H="$work/m/hires.flac"
GAIN='[+-][0-9]+\.[0-9][0-9] dB'
PEAK='[0-9]+\.[0-9]{6}'
quote() { printf '%s' "$1" | sed 's/[][\.*^$()+?{}|]/\\&/g'; }

fresh
sound_before=$(audio "$T")
sum_before=$(sha256sum < "$T")
check_re measure 0 "$(quote "$T"): track \+3\.[78]0 dB, peak 0\.08[0-9]{4}\|" gain measure "$T"
is measure-wrote-nothing "$sum_before" "$(sha256sum < "$T")"
check_re measure-album 0 "$(quote "$T"): track $GAIN, peak $PEAK\|$(quote "$H"): track $GAIN, peak $PEAK\|album: $GAIN, peak $PEAK\|" \
    gain measure --album "$T" "$H"

# write: track tags only
check_re write 0 "$(quote "$T"): track $GAIN, peak $PEAK\|" gain write "$T"
matches write-track-gain '\+3\.[78]0 dB' "$(tag "$T" REPLAYGAIN_TRACK_GAIN)"
matches write-track-peak "0\.08[0-9]{4}" "$(tag "$T" REPLAYGAIN_TRACK_PEAK)"
is write-no-album-tag "" "$(tag "$T" REPLAYGAIN_ALBUM_GAIN)"
is write-keeps-other-tags "Tone Title" "$(tag "$T" title)"
is write-keeps-the-sound "$sound_before" "$(audio "$T")"

# write --album: all four, and both files get the same album values
check_re write-album 0 "[^|]*\|[^|]*\|album: $GAIN, peak $PEAK\|" gain write --album "$T" "$H"
matches album-gain "$GAIN" "$(tag "$T" REPLAYGAIN_ALBUM_GAIN)"
is album-gain-same "$(tag "$T" REPLAYGAIN_ALBUM_GAIN)" "$(tag "$H" REPLAYGAIN_ALBUM_GAIN)"
is album-peak-same "$(tag "$T" REPLAYGAIN_ALBUM_PEAK)" "$(tag "$H" REPLAYGAIN_ALBUM_PEAK)"
matches hires-track-gain "$GAIN" "$(tag "$H" REPLAYGAIN_TRACK_GAIN)"
# what was written is what measure prints
is written-is-measured "$T: track $(tag "$T" REPLAYGAIN_TRACK_GAIN), peak $(tag "$T" REPLAYGAIN_TRACK_PEAK)" \
    "$("$bin" gain measure "$T" 2>/dev/null)"

# remove
check remove 0 "$T: 4 removed
$H: 4 removed" gain remove "$T" "$H"
is remove-gone "" "$(tag "$T" REPLAYGAIN_TRACK_GAIN)$(tag "$T" REPLAYGAIN_ALBUM_PEAK)"
is remove-keeps-other-tags "Tone Title" "$(tag "$T" title)"
is remove-keeps-the-sound "$sound_before" "$(audio "$T")"
check remove-again 0 "$T: 0 removed" gain remove "$T"

# failures: nothing is changed when one file of the run cannot be measured
fresh
sum_before=$(sha256sum < "$T")
check write-missing 3 "" gain write "$T" "$work/m/none.flac"
check write-not-audio 3 "" gain write --album "$T" "$data/notaudio.mp3"
is failures-wrote-nothing "$sum_before" "$(sha256sum < "$T")"
check remove-missing 3 "$T: 0 removed" gain remove "$work/m/none.flac" "$T"

# usage
check no-subcommand 2 "" gain
check bad-subcommand 2 "" gain frob "$T"
check measure-no-file 2 "" gain measure
check write-no-file 2 "" gain write --album
check remove-album 2 "" gain remove --album "$T"

case "$("$bin" --help 2>/dev/null)" in
    *"  gain "*) pass help-lists-gain ;;
    *) fail help-lists-gain ;;
esac

echo "cli_gain: $fails failed"
[ "$fails" -eq 0 ]
