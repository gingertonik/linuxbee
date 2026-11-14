#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_tagtool_limits.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/tagging-tools.md, G-CLI and G-RENUMBER, the
# parts added after the review of plan 09: which path `infer` matches, and
# the largest `--digits`.
set -u
bin=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() {
    label=$1; want=$2; got=$3
    if [ "$got" = "$want" ]; then pass "$label"; else fail "$label: got [$got] want [$want]"; fi
}
mkdir -p "$work/m/Some Artist"
N="$work/m/Some Artist/07 - New Title.flac"
cp "$data/basic.flac" "$N"
want_changes='Artist: "First Artist; Second Artist" -> "Some Artist"
Title: "Flac Title" -> "New Title"
Track#: "5" -> "07"'
changes() { sed 's/^.*flac: //'; }

# infer matches where the file is, not how it was named on the command line.
out=$(cd "$work/m/Some Artist" && timeout 8 "$bin" tagtool infer --dry-run --overwrite --pattern '<Artist>/<Track#> - <Title>' '07 - New Title.flac' 2>/dev/null); rc=$?
is infer-bare-name-rc 0 "$rc"
is infer-bare-name "$want_changes" "$(printf '%s\n' "$out" | changes)"
# The file is printed as it was typed.
is infer-prints-as-typed '07 - New Title.flac: Artist: "First Artist; Second Artist" -> "Some Artist"' "$(printf '%s\n' "$out" | head -n 1)"
out=$(cd "$work/m" && timeout 8 "$bin" tagtool infer --dry-run --overwrite --pattern '<Artist>/<Track#> - <Title>' './Some Artist/../Some Artist/07 - New Title.flac' 2>/dev/null)
is infer-dots "$want_changes" "$(printf '%s\n' "$out" | changes)"
# A pattern that reaches above the working directory still matches.
out=$(cd "$work/m/Some Artist" && timeout 8 "$bin" tagtool infer --dry-run --overwrite --pattern '<Album>/<Artist>/<Track#> - <Title>' '07 - New Title.flac' 2>/dev/null)
is infer-above-cwd 'Album: "Flac Album" -> "m"' "$(printf '%s\n' "$out" | changes | head -n 1)"
# A link is matched by its own name and place, not by where it points.
mkdir -p "$work/links/Linked Artist"
ln -s "$N" "$work/links/Linked Artist/03 - Linked.flac"
out=$(timeout 8 "$bin" tagtool infer --dry-run --overwrite --pattern '<Artist>/<Track#> - <Title>' "$work/links/Linked Artist/03 - Linked.flac" 2>/dev/null)
is infer-link 'Artist: "First Artist; Second Artist" -> "Linked Artist"' "$(printf '%s\n' "$out" | changes | head -n 1)"

# --digits: 9 at most.
out=$(timeout 8 "$bin" tagtool renumber --dry-run --digits 9 "$N" 2>/dev/null); rc=$?
is digits-9-rc 0 "$rc"
is digits-9 'Track#: "5" -> "000000001"' "$(printf '%s\n' "$out" | changes)"
for n in 10 11 2000000000 99999999999999999999; do
    out=$(timeout 8 "$bin" tagtool renumber --dry-run --digits "$n" "$N" 2> "$work/err"); rc=$?
    is "digits-$n-rc" 2 "$rc"
    is "digits-$n-stdout" "" "$out"
    [ -s "$work/err" ] && pass "digits-$n-message" || fail "digits-$n-message: nothing on standard error"
done
sum_before=$(sha256sum < "$N")
timeout 8 "$bin" tagtool renumber --digits 10 "$N" > /dev/null 2>&1
is digits-refused-wrote-nothing "$sum_before" "$(sha256sum < "$N")"

echo "cli_tagtool_limits: $fails failed"
[ "$fails" = 0 ]
