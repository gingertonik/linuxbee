#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_tagtool.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/tagging-tools.md, G-CLI. Works on copies in a
# temporary folder. What a file holds afterwards is judged by ffprobe, not by
# the program under test.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
check() {
    label=$1; want_rc=$2; want_out=$3
    shift 3
    out=$("$bin" "$@" 2>/dev/null)
    rc=$?
    if [ "$rc" != "$want_rc" ] || [ "$out" != "$want_out" ]; then
        fail "$label: rc=$rc (want $want_rc) stdout=[$out] (want [$want_out])"
    else
        pass "$label"
    fi
}
# tag FILE KEY: the value ffprobe reports (format level, key matched without case)
tag() {
    ffprobe -v error -show_entries format_tags -of default=nw=1 "$1" \
        | awk -F= -v k="TAG:$2" 'tolower($1) == tolower(k) { sub(/^[^=]*=/, ""); print; exit }'
}
is() {
    label=$1; want=$2; got=$3
    if [ "$got" = "$want" ]; then pass "$label"; else fail "$label: got [$got] want [$want]"; fi
}
fresh() {
    rm -rf "$work/m"
    mkdir -p "$work/m/Some Artist"
    cp "$data/basic.flac" "$work/m/a.flac"
    cp "$data/basic.mp3" "$work/m/b.mp3"
    cp "$data/basic.flac" "$work/m/Some Artist/07 - New Title.flac"
}
A="$work/m/a.flac"
Bm="$work/m/b.mp3"
N="$work/m/Some Artist/07 - New Title.flac"

# --dry-run prints the changes and writes nothing
fresh
sum_before=$(sha256sum < "$A")
# (a file with nothing to change prints nothing)
check dry-run 0 "$A: Title: \"Flac Title\" -> \"Flac Name\"" \
    tagtool replace --dry-run --find title --replace Name --field Title "$A" "$Bm"
is dry-run-wrote-nothing "$sum_before" "$(sha256sum < "$A")"

# replace, for real
check replace 0 "$A: Title: \"Flac Title\" -> \"Flac Name\"" \
    tagtool replace --find title --replace Name --field Title "$A" "$Bm"
is replace-saved "Flac Name" "$(tag "$A" title)"
is replace-other-file-untouched "Someone like You" "$(tag "$Bm" title)"
check replace-again 0 "" tagtool replace --find title --replace Name --field Title "$A"
check replace-regex 0 "$Bm: Title: \"Someone like You\" -> \"You like Someone\"" \
    tagtool replace --regex --find '^(\w+) like (\w+)$' --replace '$2 like $1' "$Bm"
is replace-regex-saved "You like Someone" "$(tag "$Bm" title)"
check replace-case 0 "" tagtool replace --match-case --find flac --replace X --field Album "$A"
check replace-word 0 "" tagtool replace --whole-word --find Fla --replace X --field Album "$A"

# case
fresh
check case 0 "$Bm: Title: \"Someone like You\" -> \"Someone Like You\"" tagtool case "$A" "$Bm"
is case-saved "Someone Like You" "$(tag "$Bm" title)"
check case-upper 0 "$A: Album: \"Flac Album\" -> \"FLAC ALBUM\"" tagtool case --style upper --field Album "$A"
is case-upper-saved "FLAC ALBUM" "$(tag "$A" album)"

# remove
fresh
check remove 0 "$A: Album: \"Flac Album\" -> \"\"
$A: Genre: \"Rock; Blues\" -> \"\"" tagtool remove --field Genre --field album --field Conductor "$A"
is remove-saved "" "$(tag "$A" genre)$(tag "$A" album)"
is remove-kept-the-rest "Flac Title" "$(tag "$A" title)"
check remove-pictures 0 "$A: pictures: 1 -> 0" tagtool remove --pictures "$A"
is remove-pictures-saved "1" "$(ffprobe -v error -show_entries stream=codec_type -of csv=p=0 "$A" | grep -c .)"
"$bin" tagtool remove --all "$A" >/dev/null 2>&1
is remove-all-saved "" "$(tag "$A" title)$(tag "$A" artist)$(tag "$A" date)"

# renumber: in the order the files are given
fresh
check renumber 0 "$Bm: Track Count: \"12\" -> \"2\"
$Bm: Track#: \"11\" -> \"01\"
$A: Track Count: \"10\" -> \"2\"
$A: Track#: \"5\" -> \"02\"" tagtool renumber --digits 2 --total "$Bm" "$A"
is renumber-saved "02/2" "$(tag "$A" track)/$(tag "$A" tracktotal)"
check renumber-start 0 "$A: Track#: \"02\" -> \"7\"" tagtool renumber --start 7 "$A"

# infer: blank fields only unless --overwrite
fresh
check infer-blank-only 0 "" tagtool infer --pattern '<Artist>/<Track#> - <Title>' "$N"
check infer 0 "$N: Artist: \"First Artist; Second Artist\" -> \"Some Artist\"
$N: Title: \"Flac Title\" -> \"New Title\"
$N: Track#: \"5\" -> \"07\"" tagtool infer --overwrite --pattern '<Artist>/<Track#> - <Title>' "$N"
is infer-saved "New Title|Some Artist|07" "$(tag "$N" title)|$(tag "$N" artist)|$(tag "$N" track)"
check infer-no-match 0 "" tagtool infer --overwrite --pattern '<Artist> = <Title>' "$N"

# copy and swap
fresh
check copy 0 "$A: Album Artist: \"Various Artists\" -> \"First Artist; Second Artist\"" \
    tagtool copy --from Artist --to "Album Artist" "$A"
check swap 0 "$A: Album: \"Flac Album\" -> \"Flac Title\"
$A: Title: \"Flac Title\" -> \"Flac Album\"" tagtool swap --from Title --to Album "$A"
is swap-saved "Flac Album|Flac Title" "$(tag "$A" title)|$(tag "$A" album)"

# refusals: exit 2 before anything is read or written
fresh
sum_before=$(sha256sum < "$A")
check no-tool 2 "" tagtool
check bad-tool 2 "" tagtool frob "$A"
check no-file 2 "" tagtool case
check replace-no-find 2 "" tagtool replace --replace x "$A"
check replace-bad-regex 2 "" tagtool replace --regex --find '(' --replace x "$A"
check replace-bad-field 2 "" tagtool replace --find a --replace b --field Nonsense "$A"
check case-bad-style 2 "" tagtool case --style shouty "$A"
check remove-nothing 2 "" tagtool remove "$A"
check renumber-bad-start 2 "" tagtool renumber --start x "$A"
check infer-bad-pattern 2 "" tagtool infer --pattern '<Artist><Title>' "$A"
check infer-no-pattern 2 "" tagtool infer "$A"
check copy-no-to 2 "" tagtool copy --from Title "$A"
check swap-bad-field 2 "" tagtool swap --from Title --to Nonsense "$A"
# a file that cannot be read: exit 3, and no file of the run is changed
check missing-file 3 "" tagtool case --style upper "$A" "$work/m/none.flac"
check not-music 3 "" tagtool case --style upper "$A" "$data/notaudio.mp3"
is refusals-wrote-nothing "$sum_before" "$(sha256sum < "$A")"

case "$("$bin" --help 2>/dev/null)" in
    *"  tagtool "*) pass help-lists-tagtool ;;
    *) fail help-lists-tagtool ;;
esac

echo "cli_tagtool: $fails failed"
[ "$fails" -eq 0 ]
