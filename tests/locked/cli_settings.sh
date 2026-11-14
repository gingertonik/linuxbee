#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_settings.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/settings-import.md, SI-CLI.
set -u
bin="$1"
file="${LINUXBEE_TEST_DATA:?}/settings/MusicBee3Settings.ini"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() {
    label=$1; want=$2; got=$3
    if [ "$got" = "$want" ]; then pass "$label"; else fail "$label: got [$got] want [$want]"; fi
}

# (not called "want": the function above uses that name)
planned='import hotkeys/edit.editField = F2 (hotkey EditClickedColumn)
import hotkeys/view.refresh = F5 (hotkey GeneralReload)
skip hotkey FileNewTab: linuxBee has no such command
import hotkeys/edit.selectAll = Ctrl+A (hotkey EditSelectAll)
import hotkeys/file.editTags = Shift+Return (hotkey EditProperties)
import hotkeys/edit.find = Ctrl+F (hotkey EditCustomSearch)
import hotkeys/track.playNow = Alt+Return (hotkey PlaybackPlayNow)
import hotkeys/track.queueNext = Ctrl+Shift+Return (hotkey PlaybackPlayNext)
import hotkeys/track.queueLast = Ctrl+Return (hotkey PlaybackPlayLast)
skip hotkey MultiMediaNext: linuxBee has no such command
import hotkeys/controls.playPause = Ctrl+P (hotkey PlaybackPlayPause)
skip hotkey PlaybackStop: the key is already taken by controls.playPause
import hotkeys/controls.volumeUp = Ctrl+Alt+Up (hotkey PlaybackVolumeUp)
skip hotkey RatingSelectedFilesTickDown: linuxBee has no such command
skip hotkey PlaybackNextTrack: the key has no name here
skip hotkey EditSelectAll: already set by an earlier line
skip hotkey PlaybackPreviousTrack: global hotkeys are not supported
import organise/template = <Album Artist>\<Album>\<Disc-Track#> <Title> (organise template)'

# Looking only.
out=$("$bin" settings import "$file" 2> "$work/err"); rc=$?
is "plan: exit" 0 "$rc"
is "plan: lines" "$planned
11 to import, 7 skipped" "$out"
is "plan: nothing on stderr" "" "$(cat "$work/err")"
is "plan: nothing written" "" "$(ls "$work" | grep -v '^err$')"

# Applying.
ini="$work/new/linuxbee.ini"
mkdir -p "$work/new"
printf '[organise]\ncopy=true\n\n[hotkeys]\ncontrols.stop=Ctrl+S\n' > "$ini"
out=$("$bin" settings import --apply --to "$ini" "$file" 2> /dev/null); rc=$?
is "apply: exit" 0 "$rc"
is "apply: lines" "$planned
11 written, 7 skipped" "$out"
value() { awk -v g="[$1]" -v k="$2" '$0 == g { on = 1; next } /^\[/ { on = 0 } on { i = index($0, "="); if (substr($0, 1, i - 1) == k) { print substr($0, i + 1); exit } }' "$ini"; }
is "apply: a hotkey" "F2" "$(value hotkeys edit.editField)"
is "apply: another" "Ctrl+Shift+Return" "$(value hotkeys track.queueNext)"
is "apply: what was there stays" "Ctrl+S" "$(value hotkeys controls.stop)"
is "apply: and the rest" "true" "$(value organise copy)"
grep -q 'Disc-Track#' "$ini" && pass "apply: the template" || fail "apply: the template [$(cat "$ini")]"
is "apply: the file given is not changed" "$(sha256sum < "${LINUXBEE_TEST_DATA}/settings/MusicBee3Settings.ini")" "$(sha256sum < "$file")"
# A settings file that is not there yet is made.
"$bin" settings import --apply --to "$work/fresh.ini" "$file" > /dev/null 2>&1; rc=$?
is "apply: makes the file" "0 yes" "$rc $([ -s "$work/fresh.ini" ] && echo yes || echo no)"

# Not a settings file, and no file.
printf 'hello\n' > "$work/not.ini"
out=$("$bin" settings import "$work/not.ini" 2> "$work/err"); rc=$?
is "not one: exit" 3 "$rc"
is "not one: nothing on stdout" "" "$out"
grep -q "^failed: $work/not.ini: ." "$work/err" && pass "not one: says so" || fail "not one: says so [$(cat "$work/err")]"
out=$("$bin" settings import --apply --to "$work/never.ini" "$work/not.ini" 2> /dev/null); rc=$?
is "not one, applying: exit and no file" "3 no" "$rc $([ -e "$work/never.ini" ] && echo yes || echo no)"
"$bin" settings import "$work/no-such-file.ini" > /dev/null 2> "$work/err"; rc=$?
is "missing: exit" 3 "$rc"
grep -q "^failed: $work/no-such-file.ini: ." "$work/err" && pass "missing: says so" || fail "missing: says so [$(cat "$work/err")]"

# Usage mistakes.
for args in "" "import" "export $file" "import $file $file" "import --apply $file" "import --to $work/x.ini $file" "import --nonsense $file"; do
    # shellcheck disable=SC2086
    "$bin" settings $args > "$work/out" 2> /dev/null; rc=$?
    is "usage [$args]" "2 " "$rc $(cat "$work/out")"
done
[ -e "$work/x.ini" ] && fail "usage: wrote a file" || pass "usage: wrote nothing"

echo "cli_settings: $fails failed"
[ "$fails" = 0 ]
