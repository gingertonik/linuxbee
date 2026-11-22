#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_settings_gone.sh /path/to/linuxbeectl
# linuxBee reads no MusicBee settings file and knows no MusicBee command name (the owner's ruling
# 1.1 of 2026-11-20). Its hotkeys are the catalogue's defaults (main-window.md, H-CATALOGUE, after
# the wiki's "Hotkeys" table). Rules: docs/spec/import.md, "Not in this version"; cli.md, the
# command list. Reads no file of the user's; writes nothing.
set -u
bin=$1
src="$(cd "$(dirname "$0")/../../src" && pwd)"
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-gone.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT

# No settings command: an unknown command, exit 2, named on standard error, nothing written.
out=$("$bin" settings import "$work/none.ini" 2> "$work/err"); rc=$?
[ "$rc" = 2 ] && [ -z "$out" ] && pass settings-unknown || fail "settings-unknown rc=$rc [$out]"
grep -q 'unknown command "settings"' "$work/err" && pass settings-named || fail "settings-named [$(head -n 1 "$work/err")]"
[ -z "$(ls -A "$work" | grep -v '^err$')" ] && pass settings-wrote-nothing || fail "settings-wrote-nothing [$(ls -A "$work")]"
"$bin" --help 2> /dev/null | grep -q '^  settings ' && fail help-lists-no-settings || pass help-lists-no-settings

# No MusicBee command name in the program's source: the names of the table it had, as whole words.
names="EditClickedColumn EditSelectAll EditCopy EditPaste EditProperties EditCustomSearch EditPreferences GeneralReload FileScanNewMedia PlaybackPlayNow PlaybackPlayNext PlaybackPlayLast PlaybackNextTrack PlaybackPreviousTrack PlaybackPlayPause PlaybackStop PlaybackVolumeUp PlaybackVolumeDown PlaybackVolumeMute ToolsAutoOrganise ToolsAnalyseVolume"
found=""
for n in $names; do
    hits=$(grep -rlw -- "$n" "$src" 2> /dev/null | sed "s|^$src/||" | tr '\n' ' ')
    [ -n "$hits" ] && found="$found $n: $hits;"
done
[ -z "$found" ] && pass no-musicbee-command-names || fail "no-musicbee-command-names [$found]"

echo "cli_settings_gone: $fails failed"
[ "$fails" = 0 ]
