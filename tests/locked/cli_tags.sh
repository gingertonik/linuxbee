#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_tags.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/tag-reading.md, section "Command line" (R-CLI),
# and the values tests/data/tags/make_fixtures.py wrote into the fixtures.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0

check() {
    name=$1; want_rc=$2; want_out=$3
    shift 3
    out=$("$bin" "$@" 2>/dev/null)
    rc=$?
    if [ "$rc" != "$want_rc" ] || [ "$out" != "$want_out" ]; then
        echo "FAIL $name: rc=$rc (want $want_rc) stdout=[$out] (want [$want_out])"
        fails=$((fails + 1))
    else
        echo "PASS $name"
    fi
}

# R-CLI: the whole report, line for line.
want_wv='format=wv
tags=APEv2
duration_ms=250
sample_rate_hz=44100
channels=2
bits_per_sample=16
field Title=Wv Title
field Artist=Wv Artist
field Album=Wv Album
field Year=2001
field Track#=6
field Track Count=7
field Disc#=1
picture type=Front Cover mime=image/png bytes=69 description=cover.png'
got=$("$bin" tags read "$data/basic.wv" 2>/dev/null | grep -v '^bitrate_kbps=')
if [ "$got" = "$want_wv" ]; then echo "PASS report-wv"; else
    echo "FAIL report-wv: [$got]"; fails=$((fails + 1)); fi

# R-CLI: fields in catalogue order, one line per value, then other, then pictures.
want_flac='field Title=Flac Title
field Artist=First Artist
field Artist=Second Artist
field Album=Flac Album
field Album Artist=Various Artists
field Year=2011
field Track#=5
field Track Count=10
field Disc#=2
field Disc Count=3
field Genre=Rock
field Genre=Blues
field Publisher=A Label
field Grouping=A Group
field Rating=4
field Lyrics=la la
field Compilation=1
other REPLAYGAIN_TRACK_GAIN=-6.50 dB
picture type=Front Cover mime=image/png bytes=69 description=front'
got=$("$bin" tags read "$data/basic.flac" 2>/dev/null | grep -E '^(field|other|unsupported|picture) ')
if [ "$got" = "$want_flac" ]; then echo "PASS report-flac"; else
    echo "FAIL report-flac: [$got]"; fails=$((fails + 1)); fi

# R-CLI: a line break inside a value is written as \n
check escape 0 'I heard\nthat you'"'"'re settled down' tags read --field Lyrics "$data/basic.mp3"
# R-CLI --field: one line per value; nothing for a field with no value
check field-multi 0 "Pop
Soul" tags read --field Genre "$data/basic.mp3"
check field-case 0 "Adele" tags read --field "album artist" "$data/basic.mp3"
check field-blank 0 "" tags read --field Conductor "$data/basic.mp3"
check field-unknown 2 "" tags read --field Nope "$data/basic.mp3"
check unicode 0 "Björk" tags read --field Artist "$data/v23.mp3"
# R-STATUS through R-CLI: exit 3, nothing on stdout
check not-audio 3 "" tags read "$data/notaudio.mp3"
check missing 3 "" tags read "$data/no-such-file.flac"
# usage mistakes: exit 2
check no-file 2 "" tags read
check no-subcommand 2 "" tags
check bad-subcommand 2 "" tags frob "$data/basic.mp3"

# R-QUIET: the tag library's own chatter never reaches standard error.
err=$("$bin" tags read "$data/notaudio.mp3" 2>&1 >/dev/null)
if [ "$err" = "not a music file: $data/notaudio.mp3" ]; then echo "PASS quiet"; else
    echo "FAIL quiet: [$err]"; fails=$((fails + 1)); fi
err=$("$bin" tags read "$data/no-such-file.flac" 2>&1 >/dev/null)
if [ "$err" = "not found: $data/no-such-file.flac" ]; then echo "PASS not-found-message"; else
    echo "FAIL not-found-message: [$err]"; fails=$((fails + 1)); fi

case "$("$bin" --help 2>/dev/null)" in
    *"  tags "*) echo "PASS help-lists-tags" ;;
    *) echo "FAIL help-lists-tags"; fails=$((fails + 1)) ;;
esac

echo "cli_tags: $fails failed"
[ "$fails" -eq 0 ]
