#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_settings_target.sh /path/to/linuxbeectl   (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/settings-import.md, SI-CLI-TARGET: a file that
# is there and is not a settings file is never written over.
set -u
case "$1" in /*) bin="$1" ;; *) bin="$(pwd)/$1" ;; esac
fixture="${LINUXBEE_TEST_DATA:?}/settings/MusicBee3Settings.ini"
music="${LINUXBEE_TEST_DATA:?}/tags/basic.mp3"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

refused() {
    # $1 label, $2 the file given with --to, which must come through untouched
    lbl=$1; dest=$2
    before=$(cksum < "$dest")
    out=$(timeout 10 "$bin" settings import --apply --to "$dest" "$fixture" 2> "$work/err")
    rc=$?
    after=$(cksum < "$dest")
    if [ "$rc" -eq 3 ]; then pass "$lbl: refused, exit 3"; else fail "$lbl: rc $rc"; fi
    if [ -z "$out" ]; then pass "$lbl: nothing on stdout"; else fail "$lbl: stdout [$out]"; fi
    if [ "$before" = "$after" ]; then pass "$lbl: the file is not changed"; else fail "$lbl: the file was written over"; fi
    if grep -q "^failed: $dest: ." "$work/err"; then pass "$lbl: says so"; else fail "$lbl: stderr [$(cat "$work/err")]"; fi
}

printf 'Dear diary,\nthis is not a settings file\nat all.\n' > "$work/notes.txt"
refused "words" "$work/notes.txt"
cp "$music" "$work/song.mp3"
refused "a music file" "$work/song.mp3"
printf '[player\nvolume=35\n' > "$work/broken.ini"
refused "a section never closed" "$work/broken.ini"

# A folder is not a settings file either, and stays a folder.
mkdir "$work/folder"
out=$(timeout 10 "$bin" settings import --apply --to "$work/folder" "$fixture" 2> /dev/null); rc=$?
if [ "$rc" -eq 3 ] && [ -z "$out" ] && [ -d "$work/folder" ]; then pass "a folder: refused"; else fail "a folder: rc $rc stdout [$out]"; fi

# What is a settings file is written, and keeps what it had.
printf '[player]\nvolume=35\n\n[hotkeys]\nview.refresh=F9\n' > "$work/good.ini"
out=$(timeout 10 "$bin" settings import --apply --to "$work/good.ini" "$fixture" 2> /dev/null); rc=$?
if [ "$rc" -eq 0 ]; then pass "a settings file: exit 0"; else fail "a settings file: rc $rc"; fi
case "$out" in *" written, "*" skipped") pass "a settings file: says what it wrote" ;; *) fail "a settings file: stdout ends [$(printf '%s\n' "$out" | tail -n 1)]" ;; esac
grep -q '^volume=35$' "$work/good.ini" && pass "a settings file: other keys stay" || fail "a settings file: other keys stay"
grep -q '^edit.editField=F2$' "$work/good.ini" && pass "a settings file: the import is in it" || fail "a settings file: the import is in it"
grep -q '^view.refresh=F5$' "$work/good.ini" && pass "a settings file: an imported key replaces the stored one" || fail "a settings file: an imported key replaces the stored one"

# An empty file is an empty settings file; a file that is not there is made.
: > "$work/empty.ini"
timeout 10 "$bin" settings import --apply --to "$work/empty.ini" "$fixture" > /dev/null 2>&1; rc=$?
if [ "$rc" -eq 0 ] && grep -q '^edit.editField=F2$' "$work/empty.ini"; then pass "an empty file: written"; else fail "an empty file: rc $rc"; fi
timeout 10 "$bin" settings import --apply --to "$work/new/dir/linuxbee.ini" "$fixture" > /dev/null 2>&1; rc=$?
if [ "$rc" -eq 0 ] && grep -q '^edit.editField=F2$' "$work/new/dir/linuxbee.ini"; then pass "no file yet: made"; else fail "no file yet: rc $rc"; fi

echo "cli_settings_target: $fails failed"
[ "$fails" -eq 0 ]
