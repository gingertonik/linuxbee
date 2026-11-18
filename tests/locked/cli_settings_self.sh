#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_settings_self.sh /path/to/linuxbeectl   (LINUXBEE_TEST_DATA must be set)
# docs/spec/settings-import.md, SI-CLI-SELF: the file being imported is never the one written, under any
# spelling. The fault (the write-path audit, part 8, F1): `--to D/sub/../FILE`, with sub missing or a link,
# got past the check, and the MusicBee file was written over as an INI file while the command said "failed".
set -u
case "$1" in /*) bin="$1" ;; *) bin="$(pwd)/$1" ;; esac
fixture="${LINUXBEE_TEST_DATA:?}/settings/MusicBee3Settings.ini"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-setself.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/xdg-data" XDG_CONFIG_HOME="$work/xdg-config" XDG_CACHE_HOME="$work/xdg-cache"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
src="$work/s/MusicBee3Settings.ini"
want=$(cksum < "$fixture")

# self LABEL TO: refused with the reason, exit 3, nothing on standard output, the source as it was.
self() {
    lbl=$1; to=$2
    said=$(timeout 10 "$bin" settings import --apply --to "$to" "$src" 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && pass "$lbl: exit 3" || fail "$lbl: rc $rc"
    [ -z "$said" ] && pass "$lbl: nothing on stdout" || fail "$lbl: stdout [$said]"
    [ "$(cat "$work/err")" = "failed: $to: this is the file being imported" ] && pass "$lbl: says why" || fail "$lbl: stderr [$(cat "$work/err")]"
    [ "$(cksum < "$src")" = "$want" ] && pass "$lbl: the MusicBee file is as it was" || fail "$lbl: the MusicBee file was written over"
}
fresh() { rm -rf "$work/s"; mkdir -p "$work/s/deep/x"; cp "$fixture" "$src"; chmod u+w "$src"; }

fresh; self "its own name" "$src"
fresh; self "through a folder that is not there" "$work/s/sub/../MusicBee3Settings.ini"
[ ! -e "$work/s/sub" ] && pass "the folder that is not there was not made" || fail "the folder that is not there was made"
fresh; ln -s "$work/s/deep/x" "$work/s/lnk"
self "through a linked folder and back" "$work/s/lnk/../MusicBee3Settings.ini"
[ ! -e "$work/s/deep/MusicBee3Settings.ini" ] && pass "nothing was written where the link leads" || fail "a file was written where the link leads"
fresh; self "with a dot in the path" "$work/s/./deep/../MusicBee3Settings.ini"
fresh; ln -s MusicBee3Settings.ini "$work/s/link.ini"; self "a link to it" "$work/s/link.ini"
fresh; ln "$src" "$work/s/second.ini"; self "its second name" "$work/s/second.ini"
[ "$(cksum < "$work/s/second.ini")" = "$want" ] && pass "its second name: as it was" || fail "its second name: written"

# The other side: a path with .. in it that is another file is written, where the cleaned path says.
fresh
said=$(timeout 10 "$bin" settings import --apply --to "$work/s/deep/../new.ini" "$src" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && grep -q '^edit.editField=F2$' "$work/s/new.ini" 2> /dev/null && pass "another file, spelled with ..: written" || fail "another file, spelled with ..: rc $rc"
[ "$(cksum < "$src")" = "$want" ] && pass "another file: the MusicBee file is as it was" || fail "another file: the MusicBee file was written over"

echo "cli_settings_self: $fails failed"
[ "$fails" = 0 ]
