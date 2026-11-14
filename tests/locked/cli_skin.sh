#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_skin.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/skins.md, SK-CLI.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/skins"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-bus"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() {
    label=$1; expected=$2; got=$3
    if [ "$got" = "$expected" ]; then pass "$label"; else fail "$label: got [$got] want [$expected]"; fi
}

out=$("$bin" skin show "$data/Plain Grey.xml" 2> "$work/err"); rc=$?
is "show: exit" 0 "$rc"
is "show: lines" 'alternateBase = 243,243,243
base = 250,250,250
button = 225,225,225
buttonText = 0,0,0
dimText = 120,120,120
header = 220,220,220
headerText = 40,40,40
highlight = 30,120,200
highlightText = 255,255,255
menu = 245,245,245
menuBar = 210,210,210
menuBarText = 10,10,10
menuHighlight = 30,120,200
menuHighlightText = 255,255,255
menuText = 20,20,20
player = 60,60,60
playerText = 240,240,240
statusBar = 215,215,215
statusBarText = 60,60,60
tab = 215,215,215
tabSelected = 250,250,250
tabSelectedText = 30,120,200
tabText = 50,50,50
text = 21,21,21
window = 230,230,230
windowText = 21,21,21
problem: variable broken: cannot read "300,1,1"
problem: Odd.One bg: cannot read "nosuchname"
problem: Odd.One fg: cannot read "1,2"
26 colours, 23 elements, 3 problems' "$out"
is "show: nothing on stderr" "" "$(cat "$work/err")"
is "show: a dependent skin, alone" 'depends on: Plain Grey.xml
highlight = 160,40,40
highlightText = 255,250,250
menuHighlight = 160,40,40
menuHighlightText = 255,250,250
tabSelectedText = 160,40,40
5 colours, 4 elements, 0 problems' "$("$bin" skin show "$data/Red Accent.xml" 2> /dev/null)"

out=$("$bin" skin show "$data/Compiled Base.xmlc" 2> "$work/err"); rc=$?
is "compiled: exit and stdout" "3 " "$rc $out"
grep -q "^failed: $data/Compiled Base.xmlc: ." "$work/err" && pass "compiled: says so" || fail "compiled: says so [$(cat "$work/err")]"
"$bin" skin show "$work/none.xml" > /dev/null 2> "$work/err"; rc=$?
is "missing: exit" 3 "$rc"
grep -q "^failed: $work/none.xml: ." "$work/err" && pass "missing: says so" || fail "missing: says so [$(cat "$work/err")]"
"$bin" skin > "$work/out" 2> /dev/null; is "usage: nothing" "2 " "$? $(cat "$work/out")"
"$bin" skin show > "$work/out" 2> /dev/null; is "usage: no file" "2 " "$? $(cat "$work/out")"
"$bin" skin hide "$data/Plain Grey.xml" > "$work/out" 2> /dev/null; is "usage: unknown" "2 " "$? $(cat "$work/out")"
"$bin" skin show "$data/Plain Grey.xml" "$data/Red Accent.xml" > "$work/out" 2> /dev/null; is "usage: two files" "2 " "$? $(cat "$work/out")"

echo "cli_skin: $fails failed"
[ "$fails" = 0 ]
