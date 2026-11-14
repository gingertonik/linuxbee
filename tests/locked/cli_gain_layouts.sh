#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_gain_layouts.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/replay-gain.md, N-MEASURE: a file is measured
# as it is. -27.8 LUFS and -24.1 dBFS for mono.flac, -20.1 LUFS and
# -24.1 dBFS for surround.flac (ffmpeg n9.0.2).
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/sound"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$data/mono.flac" "$data/surround.flac" "$data/quietmono.flac" "$work/"
cd "$work" || exit 1

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
tag() {
    ffprobe -v error -show_entries format_tags -of default=nw=1 "$1" \
        | awk -F= -v k="TAG:$2" 'tolower($1) == tolower(k) { sub(/^[^=]*=/, ""); print; exit }'
}
is() {
    label=$1; want=$2; got=$3
    if [ "$got" = "$want" ]; then pass "$label"; else fail "$label: got [$got] want [$want]"; fi
}

check "one channel" 0 "mono.flac: track +9.80 dB, peak 0.062373" gain measure mono.flac
check "six channels" 0 "surround.flac: track +2.10 dB, peak 0.062373" gain measure surround.flac
check "an album of one layout" 0 "mono.flac: track +9.80 dB, peak 0.062373
quietmono.flac: track +21.70 dB, peak 0.015668
album: +12.50 dB, peak 0.062373" gain measure --album mono.flac quietmono.flac
check "an album of two layouts" 0 "surround.flac: track +2.10 dB, peak 0.062373
mono.flac: track +9.80 dB, peak 0.062373
album: +4.50 dB, peak 0.062373" gain measure --album surround.flac mono.flac

"$bin" gain write --album mono.flac quietmono.flac >/dev/null 2>&1
is "written: track gain" "+9.80 dB" "$(tag mono.flac REPLAYGAIN_TRACK_GAIN)"
is "written: track peak" "0.062373" "$(tag mono.flac REPLAYGAIN_TRACK_PEAK)"
is "written: album gain" "+12.50 dB" "$(tag quietmono.flac REPLAYGAIN_ALBUM_GAIN)"
is "written: album peak" "0.062373" "$(tag quietmono.flac REPLAYGAIN_ALBUM_PEAK)"

echo "cli_gain_layouts: $fails failed"
[ "$fails" = 0 ]
