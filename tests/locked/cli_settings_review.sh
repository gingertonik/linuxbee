#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_settings_review.sh /path/to/linuxbeectl   (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/settings-import.md, SI-CLI: the file given is
# never written, also when --to names that same file.
set -u
case "$1" in /*) bin="$1" ;; *) bin="$(pwd)/$1" ;; esac
fixture="${LINUXBEE_TEST_DATA:?}/settings/MusicBee3Settings.ini"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

same() {
    # $1 label, $2 the --to argument; the file imported is always $work/d/MusicBee3Settings.ini
    lbl=$1; dest=$2
    rm -rf "$work/d"; mkdir -p "$work/d"
    cp "$fixture" "$work/d/MusicBee3Settings.ini"
    before=$(cksum < "$work/d/MusicBee3Settings.ini")
    out=$(cd "$work" && timeout 10 "$bin" settings import --apply --to "$dest" d/MusicBee3Settings.ini 2>/dev/null)
    rc=$?
    after=$(cksum < "$work/d/MusicBee3Settings.ini")
    if [ "$rc" -eq 3 ]; then pass "$lbl: refused, exit 3"; else fail "$lbl: rc $rc"; fi
    if [ -z "$out" ]; then pass "$lbl: nothing on stdout"; else fail "$lbl: stdout [$out]"; fi
    if [ "$before" = "$after" ]; then pass "$lbl: the file given is not changed"; else fail "$lbl: the file given was rewritten"; fi
}

same "same path" d/MusicBee3Settings.ini
same "same file, other spelling" ./d/../d/MusicBee3Settings.ini
same "same file, absolute" "$work/d/MusicBee3Settings.ini"
rm -rf "$work/d"; mkdir -p "$work/d"; cp "$fixture" "$work/d/MusicBee3Settings.ini"
ln -s d/MusicBee3Settings.ini "$work/link.ini"
before=$(cksum < "$work/d/MusicBee3Settings.ini")
out=$(cd "$work" && timeout 10 "$bin" settings import --apply --to link.ini d/MusicBee3Settings.ini 2>/dev/null)
rc=$?
after=$(cksum < "$work/d/MusicBee3Settings.ini")
if [ "$rc" -eq 3 ] && [ -z "$out" ] && [ "$before" = "$after" ]; then pass "through a link: refused, unchanged"; else fail "through a link: rc $rc, changed: $([ "$before" = "$after" ] && echo no || echo yes)"; fi

# a different file is still written (the rule must not refuse every --to)
rm -rf "$work/d"; mkdir -p "$work/d"; cp "$fixture" "$work/d/MusicBee3Settings.ini"
out=$(cd "$work" && timeout 10 "$bin" settings import --apply --to linuxbee.ini d/MusicBee3Settings.ini 2>/dev/null)
rc=$?
if [ "$rc" -eq 0 ] && [ -f "$work/linuxbee.ini" ]; then pass "another file: written"; else fail "another file: rc $rc"; fi

echo "cli_settings_review: $fails failed"
[ "$fails" -eq 0 ]
