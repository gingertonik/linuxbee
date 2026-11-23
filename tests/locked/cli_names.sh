#!/bin/sh
# LOCKED ACCEPTANCE TEST (long names and names that are not UTF-8 on the command line, fix-c1).
# Written by the reviewer, not the builder.
# Usage: cli_names.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/delete-files.md (FD-CLI, FD-NAMES, NM-UTF8) and docs/spec/convert.md
# (C-CLI, C-CLEAN, NM-SCRATCH). Convert F7 and F8, Trash F4 and F5 of the audit. Every folder is
# this test's own, the trash among them, and the music is a copy of a fixture.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/convert"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-names.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/xdg-data" XDG_CONFIG_HOME="$work/xdg-config" XDG_CACHE_HOME="$work/xdg-cache"
command -v ffmpeg >/dev/null 2>&1 || { echo "FAIL ffmpeg is required for this test"; exit 1; }
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() {
    label=$1; expected=$2; got=$3
    if [ "$got" = "$expected" ]; then pass "$label"; else fail "$label: got [$got] want [$expected]"; fi
}
# name BYTES EXT: "n" repeated, then EXT, BYTES bytes in all.
name() { printf "%0$(($1 - ${#2}))d%s" 0 "$2" | tr 0 n; }
there() { [ -e "$1" ] && echo yes || echo no; }

mkdir -p "$work/m" "$work/out"
bad=$(printf 'bad\377\376.mp3')
shown=$(printf 'bad\357\277\275\357\277\275.mp3') # as linuxBee reads it: a stand-in for each byte

# trash: a name of 246 bytes (Trash F5).
n246=$(name 246 .mp3)
printf music > "$work/m/$n246"
out=$("$bin" trash "$work/m/$n246" 2>/dev/null); rc=$?
is trash-long "3 failed: $work/m/$n246: the name is too long for the trash: 246 bytes, and the trash takes names of up to 245" "$rc $out"
is trash-long-stays yes "$(there "$work/m/$n246")"

# trash: a name that is not UTF-8 (Trash F4).
cp "$data/tone.mp3" "$work/m/$bad"
out=$("$bin" trash "$work/m/$bad" 2>/dev/null); rc=$?
is trash-not-utf8 "3 refused: $work/m/$shown: its name is not valid UTF-8" "$rc $out"
is trash-not-utf8-stays yes "$(there "$work/m/$bad")"
is trash-empty "" "$(ls -A "$XDG_DATA_HOME/Trash/files" 2>/dev/null)"

# convert: a source whose name is not UTF-8 (Convert F8), to a file and by a template.
"$bin" convert --output "$work/out/x.mp3" "$work/m/$bad" > /dev/null 2> "$work/err"; rc=$?
is convert-not-utf8 "3 failed: $work/m/$shown: its name is not valid UTF-8" "$rc $(cat "$work/err")"
"$bin" convert --out-dir "$work/out" --template '<Title>' "$work/m/$bad" > /dev/null 2> "$work/err"; rc=$?
is convert-not-utf8-template "3 failed: $work/m/$shown: its name is not valid UTF-8" "$rc $(cat "$work/err")"
is convert-not-utf8-nothing "" "$(ls -A "$work/out")"

# convert: a destination of 250 bytes (Convert F7), with the tags.
n250=$(name 250 .mp3)
out=$("$bin" convert --output "$work/out/$n250" "$data/tone.flac" 2>/dev/null); rc=$?
is convert-long "0 converted: $data/tone.flac -> $work/out/$n250" "$rc $out"
is convert-long-alone "$n250" "$(ls -A "$work/out")"

# The other side: a name that is not there is still "not found" and "not there".
"$bin" convert --output "$work/out/y.mp3" "$work/m/gone.flac" > /dev/null 2> "$work/err"; rc=$?
is convert-gone "3 failed: $work/m/gone.flac: not found" "$rc $(cat "$work/err")"
out=$("$bin" trash "$work/m/gone.flac" 2>/dev/null); rc=$?
is trash-gone "3 not there: $work/m/gone.flac" "$rc $out"

[ "$fails" -eq 0 ] && echo "cli_names: 0 failed" && exit 0
echo "cli_names: $fails failed"
exit 1
