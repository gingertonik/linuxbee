#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_convert_own.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# docs/spec/convert.md, C-OWN: a run of `convert` never writes onto one of its own inputs, nor onto the
# result of an earlier input. The fault (the write-path audit, part 3, F2 and F5): with --if-exists
# overwrite, an input was replaced by the conversion of another and deleted.
# Everything happens in a temporary folder, on copies, with folders of its own for the program.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/convert"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-convown.XXXXXX") || exit 2
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/xdg-data" XDG_CONFIG_HOME="$work/xdg-config" XDG_CACHE_HOME="$work/xdg-cache"
command -v ffmpeg >/dev/null 2>&1 || { echo "FAIL ffmpeg is required for this test"; exit 1; }
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: want [$2] got [$3]"; fi; }
sum() { sha256sum "$1" 2>/dev/null | cut -c1-64; }
names() { LC_ALL=C ls -A "$1" | tr '\n' ' '; }
title() { "$bin" tags read "$1" 2>/dev/null | sed -n 's/^field Title=//p'; }
fresh() { rm -rf "$work/t" "$work/u"; mkdir -p "$work/t" "$work/u"; }

for how in overwrite update-tags; do
    # The audit's case: the template gives a the name b has. b is an input: it is not written.
    fresh
    cp "$data/tone.mp3" "$work/t/a.mp3"; cp "$data/tone.mp3" "$work/t/b.mp3"; chmod u+w "$work/t"/*
    "$bin" tags write --set Title=b "$work/t/a.mp3" > /dev/null || fail "setup a"
    "$bin" tags write --set Title=c "$work/t/b.mp3" > /dev/null || fail "setup b"
    A=$(sum "$work/t/a.mp3"); B=$(sum "$work/t/b.mp3")
    out=$("$bin" convert --format mp3 --out-dir "$work/t" --template '<Title>' --if-exists $how "$work/t/a.mp3" "$work/t/b.mp3" 2> "$work/err"); rc=$?
    is "template-said ($how)" "converted: $work/t/a.mp3 -> $work/t/b_1.mp3
converted: $work/t/b.mp3 -> $work/t/c.mp3" "$out"
    is "template-rc ($how)" "0" "$rc"
    is "template-no-failure ($how)" "" "$(grep '^failed' "$work/err")"
    is "template-names ($how)" "a.mp3 b.mp3 b_1.mp3 c.mp3 " "$(names "$work/t")"
    is "template-a-untouched ($how)" "$A" "$(sum "$work/t/a.mp3")"
    is "template-b-untouched ($how)" "$B" "$(sum "$work/t/b.mp3")"
    is "template-result-is-a ($how)" "b" "$(title "$work/t/b_1.mp3")"

    # No template, into the inputs' own folder: a.flac would be a.mp3, which is an input.
    fresh
    cp "$data/tone.flac" "$work/t/a.flac"; cp "$data/tone.mp3" "$work/t/a.mp3"; chmod u+w "$work/t"/*
    "$bin" tags write --set Title=mine "$work/t/a.mp3" > /dev/null || fail "setup"
    B=$(sum "$work/t/a.mp3")
    out=$("$bin" convert --format mp3 --out-dir "$work/t" --if-exists $how "$work/t/a.flac" "$work/t/a.mp3" 2> /dev/null); rc=$?
    is "plain-said ($how)" "converted: $work/t/a.flac -> $work/t/a_1.mp3
converted: $work/t/a.mp3 -> $work/t/a_2.mp3" "$out"
    is "plain-rc ($how)" "0" "$rc"
    is "plain-input-untouched ($how)" "$B" "$(sum "$work/t/a.mp3")"

    # The same folder by another spelling: through a link to it.
    fresh
    cp "$data/tone.flac" "$work/t/a.flac"; cp "$data/tone.mp3" "$work/t/a.mp3"; chmod u+w "$work/t"/*
    ln -s "$work/t" "$work/l"
    B=$(sum "$work/t/a.mp3")
    out=$("$bin" convert --format mp3 --out-dir "$work/l" --if-exists $how "$work/t/a.flac" "$work/t/a.mp3" 2> /dev/null)
    is "link-said ($how)" "converted: $work/t/a.flac -> $work/l/a_1.mp3
converted: $work/t/a.mp3 -> $work/l/a_2.mp3" "$out"
    is "link-input-untouched ($how)" "$B" "$(sum "$work/t/a.mp3")"
    rm -f "$work/l"
done

# F5: two inputs with one name, from two folders. The second does not write over the first one's result.
fresh
cp "$data/tone.flac" "$work/t/x.flac"; cp "$data/hires.flac" "$work/u/x.flac"
mkdir "$work/o"
out=$("$bin" convert --format flac --out-dir "$work/o" --if-exists overwrite "$work/t/x.flac" "$work/u/x.flac" 2> /dev/null); rc=$?
is "same-name-said" "converted: $work/t/x.flac -> $work/o/x.flac
converted: $work/u/x.flac -> $work/o/x_1.flac" "$out"
is "same-name-rc" "0" "$rc"
is "same-name-first" "44100" "$("$bin" tags read "$work/o/x.flac" 2>/dev/null | sed -n 's/^sample_rate_hz=//p')"
is "same-name-second" "96000" "$("$bin" tags read "$work/o/x_1.flac" 2>/dev/null | sed -n 's/^sample_rate_hz=//p')"

# The other side: a file that is there and is no input is still replaced when asked, and skipped by default.
fresh
cp "$data/tone.flac" "$work/t/a.flac"; echo "an old result" > "$work/t/a.mp3"
is "skip-still-skips" "skipped: $work/t/a.flac -> $work/t/a.mp3" "$("$bin" convert --format mp3 --out-dir "$work/t" "$work/t/a.flac" 2> /dev/null)"
is "overwrite-still-overwrites" "converted: $work/t/a.flac -> $work/t/a.mp3" "$("$bin" convert --format mp3 --out-dir "$work/t" --if-exists overwrite "$work/t/a.flac" 2> /dev/null)"
is "overwrite-names" "a.flac a.mp3 " "$(names "$work/t")"
is "overwrite-result" "Tone Title" "$(title "$work/t/a.mp3")"

echo "cli_convert_own: $fails failed"
[ "$fails" = 0 ]
