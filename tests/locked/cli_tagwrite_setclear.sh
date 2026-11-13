#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_tagwrite_setclear.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
#
# Expected values: docs/spec/tag-writing.md, W-CLI: one field cannot be both
# set and cleared in one command. ffprobe is the judge of what the file holds.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-setclear.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
tags_of() { ffprobe -v error -show_entries format_tags:stream_tags -of default=nw=1 "$1"; }

# refused NAME ARGS...: exit 2, nothing on standard output, something on
# standard error, and the file byte for byte as it was.
refused() {
    label=$1
    shift
    cp "$data/basic.flac" "$work/f.flac"
    before=$(sha256sum "$work/f.flac" | cut -d' ' -f1)
    out=$("$bin" tags write "$@" "$work/f.flac" 2> "$work/err")
    rc=$?
    if [ "$rc" != 2 ] || [ -n "$out" ]; then
        fail "$label: rc=$rc (want 2) stdout=[$out] (want nothing)"
    elif [ ! -s "$work/err" ]; then
        fail "$label: nothing on standard error"
    elif [ "$(sha256sum "$work/f.flac" | cut -d' ' -f1)" != "$before" ]; then
        fail "$label: the file was changed"
    else
        pass "$label"
    fi
}

refused same-name --clear Title --set "Title=X"
refused set-first --set "Title=X" --clear Title
refused other-case --clear title --set "Title=X"
refused other-case-2 --clear TITLE --set "title=X"
refused among-others --set "Album=Kept" --clear Genre --set "Genre=Jazz" --set "Year=1999"
refused two-values --set "Artist=A" --set "Artist=B" --clear artist
refused other-tag --clear-other CUSTOMKEY --set-other "CUSTOMKEY=calm"
refused other-tag-case --set-other "CustomKey=calm" --clear-other CUSTOMKEY

# The message names the field once, as it was first written.
cp "$data/basic.flac" "$work/f.flac"
"$bin" tags write --clear title --set "Title=X" "$work/f.flac" > /dev/null 2> "$work/err"
grep -q -i 'title' "$work/err" && pass message-names-field || fail "message-names-field: [$(cat "$work/err")]"

# What is still allowed: different fields, and a field beside an other tag.
cp "$data/basic.flac" "$work/g.flac"
out=$("$bin" tags write --clear Genre --set "Title=New Title" "$work/g.flac" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ "$out" = saved ] && pass different-fields || fail "different-fields: rc=$rc [$out]"
tags_of "$work/g.flac" | grep -i -q -x -F 'TAG:TITLE=New Title' && pass different-fields-set || fail different-fields-set
tags_of "$work/g.flac" | grep -i -q '^TAG:GENRE=' && fail different-fields-cleared || pass different-fields-cleared
cp "$data/basic.flac" "$work/h.flac"
out=$("$bin" tags write --set-other "CUSTOMKEY=calm" --clear Genre --set "Title=T" "$work/h.flac" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ "$out" = saved ] && pass field-and-other || fail "field-and-other: rc=$rc [$out]"
tags_of "$work/h.flac" | grep -i -q -x -F 'TAG:CUSTOMKEY=calm' && pass other-was-set || fail other-was-set

echo "cli_tagwrite_setclear: $fails failed"
[ "$fails" = 0 ]
