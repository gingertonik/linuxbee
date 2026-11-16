#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_tagtool_m4a.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/tagging-tools.md, G-RENUMBER. Works on copies in a temporary folder.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

cp "$data/basic.m4a" "$work/a.m4a"
cp "$data/basic.flac" "$work/b.flac"
out=$("$bin" tagtool renumber --start 41 --digits 3 "$work/a.m4a" "$work/b.flac" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && pass first-run-exit || fail "first-run-exit rc=$rc"
printf '%s\n' "$out" | grep -q "^$work/a.m4a: Track#: .* -> \"41\"\$" && pass m4a-without-padding || fail "m4a-without-padding: [$out]"
printf '%s\n' "$out" | grep -q "^$work/b.flac: Track#: .* -> \"042\"\$" && pass flac-with-padding || fail "flac-with-padding: [$out]"
# The same again: nothing is offered for either file.
out=$("$bin" tagtool renumber --start 41 --digits 3 "$work/a.m4a" "$work/b.flac" 2> /dev/null)
printf '%s\n' "$out" | grep -q 'Track#:' && fail "second-run-offers-nothing: [$out]" || pass second-run-offers-nothing

echo "cli_tagtool_m4a: $fails failed"
[ "$fails" = 0 ]
