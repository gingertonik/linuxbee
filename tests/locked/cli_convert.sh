#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_convert.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
#
# The judge here is ffprobe and ffmpeg: what codec and bitrate the result has,
# whether the tags and the picture are visible to another program, and for
# lossless targets whether the decoded audio is bit-for-bit the source's.
# Expected values: docs/spec/convert.md (C-CLI and the rules named), the ffmpeg
# manuals, and tests/data/convert/MANIFEST.txt for the checksums (GEN).
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/convert"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-convert.XXXXXX") || exit 2
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT

command -v ffprobe >/dev/null 2>&1 && command -v ffmpeg >/dev/null 2>&1 || {
    echo "FAIL ffmpeg and ffprobe are required for this test"; exit 1; }

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

# check LABEL WANT_RC WANT_STDOUT ARGS...
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
# is LABEL WANT GOT
is() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: want [$2] got [$3]"; fi; }

audio() { ffprobe -v error -select_streams a:0 -show_entries "stream=$2" -of csv=p=0 "$1"; }
# The tags of the file and of its audio stream (a picture is a stream with tags of its own).
tag() { ffprobe -v error -select_streams a:0 -show_entries format_tags:stream_tags -of default=nw=1 "$1" | grep -i "^TAG:$2=" | head -n 1 | cut -d= -f2-; }
picture() { ffprobe -v error -select_streams v -show_entries stream=codec_name -of csv=p=0 "$1" | head -n 1; }
pcm_md5() { ffmpeg -v error -i "$1" -map 0:a -c:a pcm_s32le -f md5 - 2>/dev/null; }

TONE_MD5="MD5=7c64f1b4c51d13b477d7968f678daf4c"    # GEN: MANIFEST.txt, tone.flac
HIRES_MD5="MD5=cd4340aa95ed4a289368f4c84ceed5d8"   # GEN: MANIFEST.txt, hires.flac
is fixture-tone "$TONE_MD5" "$(pcm_md5 "$data/tone.flac")"
is fixture-hires "$HIRES_MD5" "$(pcm_md5 "$data/hires.flac")"

# C-PRESETS through C-CLI
is list-first "mp3-320	MP3 320 kbps (CBR)" "$("$bin" convert --list-presets 2>/dev/null | head -n 1)"
is list-count 18 "$("$bin" convert --list-presets 2>/dev/null | grep -c .)"

# The main case: FLAC to MP3 at 320 kbps. No --preset means the first preset.
o="$work/a.mp3"
check default-preset 0 "converted: $data/tone.flac -> $o" convert --output "$o" "$data/tone.flac"
is mp3-codec mp3 "$(audio "$o" codec_name)"
is mp3-bitrate 320000 "$(audio "$o" bit_rate)"       # FF: -b:a 320k is constant bitrate
is mp3-rate 44100 "$(audio "$o" sample_rate)"
is mp3-channels 2 "$(audio "$o" channels)"
is mp3-title "Tone Title" "$(tag "$o" title)"         # C-TAGS, seen by another program
is mp3-album "Tone Album" "$(tag "$o" album)"
is mp3-track "3/12" "$(tag "$o" track)"
is mp3-picture png "$(picture "$o")"
[ ! -e "$work/a.part.mp3" ] && pass no-part-file || fail no-part-file

o="$work/v0.mp3"
check mp3-v0 0 "converted: $data/tone.flac -> $o" convert --preset mp3-v0 --output "$o" "$data/tone.flac"
rate=$(audio "$o" bit_rate)
# FF: -q:a 0 is variable bitrate, so the rate is not one of the fixed steps.
[ "$rate" != 320000 ] && [ "$rate" -gt 32000 ] && pass v0-is-vbr || fail "v0-is-vbr: $rate"

# C-DEPTH and the point of a lossless target: the decoded audio is identical.
for spec in flac:flac flac-8:flac alac:m4a wav:wav wavpack:wv; do
    id=${spec%%:*}; ext=${spec#*:}
    o="$work/lossless-$id.$ext"
    check "lossless-$id" 0 "converted: $data/tone.flac -> $o" convert --preset "$id" --output "$o" "$data/tone.flac"
    is "identical-$id" "$TONE_MD5" "$(pcm_md5 "$o")"
    is "title-$id" "Tone Title" "$(tag "$o" title)"
    o="$work/hires-$id.$ext"
    check "hires-$id" 0 "converted: $data/hires.flac -> $o" convert --preset "$id" --output "$o" "$data/hires.flac"
    is "identical-hires-$id" "$HIRES_MD5" "$(pcm_md5 "$o")"
    is "rate-hires-$id" 96000 "$(audio "$o" sample_rate)"
done
is alac-codec alac "$(audio "$work/lossless-alac.m4a" codec_name)"
is wav-codec pcm_s16le "$(audio "$work/lossless-wav.wav" codec_name)"
is wav-hires-codec pcm_s24le "$(audio "$work/hires-wav.wav" codec_name)"
is wavpack-codec wavpack "$(audio "$work/lossless-wavpack.wv" codec_name)"
is flac-hires-bits 24 "$(audio "$work/hires-flac.flac" bits_per_raw_sample)"

o="$work/cd.flac"
check flac-cd 0 "converted: $data/hires.flac -> $o" convert --preset flac-cd --output "$o" "$data/hires.flac"
is cd-rate 44100 "$(audio "$o" sample_rate)"
is cd-bits 16 "$(audio "$o" bits_per_raw_sample)"

# --format with its own numbers
o="$work/q.ogg"
check ogg 0 "converted: $data/tone.flac -> $o" convert --format ogg --quality 5 --output "$o" "$data/tone.flac"
is ogg-codec vorbis "$(audio "$o" codec_name)"
is ogg-title "Tone Title" "$(tag "$o" title)"
o="$work/o.opus"
check opus 0 "converted: $data/tone.flac -> $o" convert --format opus --bitrate 96 --output "$o" "$data/tone.flac"
is opus-codec opus "$(audio "$o" codec_name)"
o="$work/a.m4a"
check aac 0 "converted: $data/tone.flac -> $o" convert --preset aac-256 --output "$o" "$data/tone.flac"
is aac-codec aac "$(audio "$o" codec_name)"
is aac-title "Tone Title" "$(tag "$o" title)"
o="$work/mono.mp3"
check mono 0 "converted: $data/tone.flac -> $o" convert --format mp3 --bitrate 128 --channels 1 \
    --sample-rate 22050 --output "$o" "$data/tone.flac"
is mono-channels 1 "$(audio "$o" channels)"
is mono-rate 22050 "$(audio "$o" sample_rate)"
is mono-bitrate 128000 "$(audio "$o" bit_rate)"
o="$work/bare.mp3"
check no-tags 0 "converted: $data/tone.flac -> $o" convert --no-tags --output "$o" "$data/tone.flac"
is no-tags-title "" "$(tag "$o" title)"
is no-tags-picture "" "$(picture "$o")"
o="$work/nopic.mp3"
check no-pictures 0 "converted: $data/tone.flac -> $o" convert --no-pictures --output "$o" "$data/tone.flac"
is no-pictures-title "Tone Title" "$(tag "$o" title)"
is no-pictures-picture "" "$(picture "$o")"

# --out-dir without a template: the source's name with the new extension
d="$work/flat"
check out-dir-plain 0 "converted: $data/tone.flac -> $d/tone.mp3
converted: $data/hires.flac -> $d/hires.mp3" convert --preset mp3-128 --out-dir "$d" "$data/tone.flac" "$data/hires.flac"
[ -f "$d/tone.mp3" ] && [ -f "$d/hires.mp3" ] && pass out-dir-files || fail out-dir-files

# --out-dir with a naming template (docs/spec/paths.md, docs/spec/track-fields.md)
d="$work/tree"
T='<Album Artist>\<Album>\<Disc-Track#> <Title>'
want="$d/Tone Artist/Tone Album/01-03 Tone Title.mp3"
check template 0 "converted: $data/tone.flac -> $want" convert --out-dir "$d" --template "$T" "$data/tone.flac"
is template-codec mp3 "$(audio "$want" codec_name)"
# C-EXISTS through C-CLI
check exists-skip 0 "skipped: $data/tone.flac -> $want" convert --out-dir "$d" --template "$T" "$data/tone.flac"
check exists-rename 0 "converted: $data/tone.flac -> $d/Tone Artist/Tone Album/01-03 Tone Title_1.mp3" \
    convert --out-dir "$d" --template "$T" --if-exists rename "$data/tone.flac"
check exists-overwrite 0 "converted: $data/tone.flac -> $want" \
    convert --out-dir "$d" --template "$T" --if-exists overwrite "$data/tone.flac"
check exists-update 0 "tags updated: $data/tone.flac -> $want" \
    convert --out-dir "$d" --template "$T" --if-exists update-tags "$data/tone.flac"
# P-MISSING: hires.flac has a title and nothing else
check template-missing 3 "" convert --out-dir "$d" --template "$T" "$data/hires.flac"
err=$("$bin" convert --out-dir "$d" --template "$T" "$data/hires.flac" 2>&1 >/dev/null)
is template-missing-message "failed: $data/hires.flac: missing tags: Album Artist, Album" "$err" # Disc-Track# is written as #

# One bad file does not stop the others; the exit code says something failed.
d="$work/mixed"
check mixed 3 "converted: $data/tone.flac -> $d/tone.mp3" convert --out-dir "$d" \
    "$work/no-such.flac" "$data/tone.flac"
err=$("$bin" convert --if-exists overwrite --out-dir "$d" "$work/no-such.flac" "$data/tone.flac" 2>&1 >/dev/null)
is mixed-message "failed: $work/no-such.flac: not found" "$err"
err=$("$bin" convert --output "$work/n.mp3" "$data/make_fixtures.py" 2>&1 >/dev/null)
is not-audio-message "failed: $data/make_fixtures.py: not a music file" "$err"
check ffmpeg-missing 3 "" convert --ffmpeg "$work/no-ffmpeg" --output "$work/m.mp3" "$data/tone.flac"

# usage mistakes: exit 2, nothing on stdout
check no-files 2 "" convert --output "$work/x.mp3"
check no-target 2 "" convert "$data/tone.flac"
check both-targets 2 "" convert --output "$work/x.mp3" --out-dir "$work" "$data/tone.flac"
check output-two-files 2 "" convert --output "$work/x.mp3" "$data/tone.flac" "$data/hires.flac"
check template-needs-dir 2 "" convert --template '<Title>' --output "$work/x.mp3" "$data/tone.flac"
check bad-preset 2 "" convert --preset nope --output "$work/x.mp3" "$data/tone.flac"
check preset-and-format 2 "" convert --preset flac --format mp3 --output "$work/x.mp3" "$data/tone.flac"
check bad-format 2 "" convert --format wma --output "$work/x.wma" "$data/tone.flac"
check bad-bitrate 2 "" convert --format mp3 --bitrate 300 --output "$work/x.mp3" "$data/tone.flac"
check bad-number 2 "" convert --format mp3 --bitrate lots --output "$work/x.mp3" "$data/tone.flac"
check bad-exists 2 "" convert --if-exists maybe --output "$work/x.mp3" "$data/tone.flac"
check bad-template 2 "" convert --out-dir "$work" --template '$Nope(x)' "$data/tone.flac"
[ ! -e "$work/x.mp3" ] && pass usage-wrote-nothing || fail usage-wrote-nothing

case "$("$bin" --help 2>/dev/null)" in
    *"  convert "*) pass help-lists-convert ;;
    *) fail help-lists-convert ;;
esac

echo "cli_convert: $fails failed"
[ "$fails" -eq 0 ]
