#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_settings_self.sh /path/to/linuxbeectl   (LINUXBEE_TEST_DATA must be set)
# Corrected after the review of fix-87 (the block marked so): a link whose own text goes through "..".
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

# --- The correction (the review of fix-87): the path written is the path checked. ---
# nothing LABEL TO: refused as a link to nothing, the source as it was
nothing() {
    said=$(timeout 10 "$bin" settings import --apply --to "$2" "$src" 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && [ -z "$said" ] && pass "$1: refused, exit 3" || fail "$1: rc $rc stdout [$(printf '%s' "$said" | tail -n 1)]"
    [ "$(cat "$work/err")" = "failed: $2: it is a link to nothing" ] && pass "$1: says why" || fail "$1: stderr [$(cat "$work/err")]"
    [ "$(cksum < "$src")" = "$want" ] && pass "$1: the MusicBee file is as it was" || fail "$1: the MusicBee file was written over"
}

# A link whose own text goes through a folder that is not there, and comes out at the MusicBee file. To
# the system it leads nowhere; read as text it is the file. It was followed as text, and the file written.
fresh; mkdir -p "$work/s/r"; ln -s ../gone/../MusicBee3Settings.ini "$work/s/r/d.ini"
nothing "a link through a folder that is not there" "$work/s/r/d.ini"
[ -L "$work/s/r/d.ini" ] && pass "that link is as it was" || fail "that link is as it was"

# The same link, and the folder in it is a link elsewhere, where another settings file has the name. That
# one is the file the system names: it is written, and the MusicBee file is not.
fresh; mkdir -p "$work/s/r" "$work/s/q/x"; printf '[x]\na=1\n' > "$work/s/q/MusicBee3Settings.ini"
ln -s q/x "$work/s/hop"; ln -s ../hop/../MusicBee3Settings.ini "$work/s/r/d.ini"
said=$(timeout 10 "$bin" settings import --apply --to "$work/s/r/d.ini" "$src" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && pass "a link through a linked folder: exit 0" || fail "a link through a linked folder: rc $rc"
[ "$(cksum < "$src")" = "$want" ] && pass "a link through a linked folder: the MusicBee file is as it was" || fail "a link through a linked folder: the MusicBee file was written over"
grep -q '^edit.editField=F2$' "$work/s/q/MusicBee3Settings.ini" && grep -q '^a=1$' "$work/s/q/MusicBee3Settings.ini" && pass "a link through a linked folder: the file the system names was written, and kept its key" || fail "a link through a linked folder: the file the system names was not written"
[ -L "$work/s/r/d.ini" ] && pass "a link through a linked folder: the link is a link still" || fail "a link through a linked folder: the link is gone"

# --to spelled through a linked folder and back, with another settings file where the system would go.
# Read as text it is the MusicBee file: refused, and the other file is not touched either.
fresh; mkdir -p "$work/s/q/x"; printf '[x]\na=1\n' > "$work/s/q/MusicBee3Settings.ini"; other=$(cksum < "$work/s/q/MusicBee3Settings.ini")
ln -s q/x "$work/s/hop"
self "through a linked folder and back, another settings file beyond" "$work/s/hop/../MusicBee3Settings.ini"
[ "$(cksum < "$work/s/q/MusicBee3Settings.ini")" = "$other" ] && pass "the settings file beyond is as it was" || fail "the settings file beyond was written"

# A link to nothing is refused, and nothing is made where it points.
fresh; ln -s nowhere.ini "$work/s/dangling.ini"
nothing "a link to nothing" "$work/s/dangling.ini"
[ ! -e "$work/s/nowhere.ini" ] && pass "a link to nothing: nothing was made where it points" || fail "a link to nothing: a file was made where it points"

# A link to a settings file that is there: the file is written, and the link stays a link.
fresh; printf '[player]\nvolume=35\n' > "$work/s/real.ini"; ln -s real.ini "$work/s/tolink.ini"
timeout 10 "$bin" settings import --apply --to "$work/s/tolink.ini" "$src" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && [ -L "$work/s/tolink.ini" ] && grep -q '^edit.editField=F2$' "$work/s/real.ini" && grep -q '^volume=35$' "$work/s/real.ini" && pass "a link to a settings file: the file is written, the link stays" || fail "a link to a settings file: rc $rc"

# The other side: a path with .. in it that is another file is written, where the cleaned path says.
fresh
said=$(timeout 10 "$bin" settings import --apply --to "$work/s/deep/../new.ini" "$src" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && grep -q '^edit.editField=F2$' "$work/s/new.ini" 2> /dev/null && pass "another file, spelled with ..: written" || fail "another file, spelled with ..: rc $rc"
[ "$(cksum < "$src")" = "$want" ] && pass "another file: the MusicBee file is as it was" || fail "another file: the MusicBee file was written over"

echo "cli_settings_self: $fails failed"
[ "$fails" = 0 ]
