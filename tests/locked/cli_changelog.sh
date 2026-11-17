#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# What a release said of itself is not changed afterwards: each released section of CHANGELOG.md is as
# it is in that release's tag. New lines go under "Unreleased", which comes first.
# Usage: cli_changelog.sh [anything]
# Rules: docs/spec/release.md, R-CHANGELOG. (Found at 0.7.0: git merges a branch cut before a release
# with its new line under the released heading, and reports no conflict.)
# Where the tree is not a git checkout, or has none of the tags, the comparison is skipped and says so.
set -u
SRC=$(cd "$(dirname "$0")/../.." && pwd)
LOG="$SRC/CHANGELOG.md"
fails=0
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
section() { awk -v h="## $1 " 'index($0, h) == 1 { f = 1; next } /^## / { f = 0 } f'; }
[ -f "$LOG" ] || { echo "FAIL no CHANGELOG.md"; exit 1; }

[ "$(grep '^## ' "$LOG" | head -n 1)" = "## Unreleased" ] || fail "the first heading is not '## Unreleased': $(grep '^## ' "$LOG" | head -n 1)"
[ "$(grep -c '^## Unreleased$' "$LOG")" = 1 ] || fail "there is not exactly one '## Unreleased'"
versions=$(sed -n 's/^## \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\) (.*/\1/p' "$LOG")
[ -n "$versions" ] || fail "no released section was found"
[ "$(echo "$versions" | sort | uniq -d | wc -l)" = 0 ] || fail "a version has two sections: $(echo "$versions" | sort | uniq -d | tr '\n' ' ')"
[ "$(grep '^## ' "$LOG" | grep -cv '^## Unreleased$')" = "$(echo "$versions" | grep -c .)" ] || fail "a heading is neither 'Unreleased' nor 'X.Y.Z (date)'"

compared=0
if git -C "$SRC" rev-parse --git-dir > /dev/null 2>&1; then
    for v in $versions; do
        git -C "$SRC" rev-parse -q --verify "refs/tags/v$v^{commit}" > /dev/null 2>&1 || continue
        compared=$((compared + 1))
        then=$(git -C "$SRC" show "v$v:CHANGELOG.md" 2> /dev/null | section "$v")
        now=$(section "$v" < "$LOG")
        [ -n "$then" ] || { fail "the tag v$v has no section $v in its own CHANGELOG.md"; continue; }
        [ "$then" = "$now" ] && continue
        fail "the section $v is not as it was released (a new line belongs under 'Unreleased'):"
        printf '%s\n' "$then" > "${TMPDIR:-/tmp}/lb-changelog.$$"
        printf '%s\n' "$now" | diff "${TMPDIR:-/tmp}/lb-changelog.$$" - | grep '^[<>]' | head -n 6
        rm -f "${TMPDIR:-/tmp}/lb-changelog.$$"
    done
    # A tag whose section has been taken out altogether.
    for t in $(git -C "$SRC" tag -l 'v[0-9]*.[0-9]*.[0-9]*'); do
        echo "$t" | grep -qx 'v[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*' || continue
        git -C "$SRC" show "$t:CHANGELOG.md" 2> /dev/null | grep -q "^## ${t#v} (" || continue
        echo "$versions" | grep -qx "${t#v}" || fail "the section ${t#v}, released as $t, is gone"
    done
fi
[ "$compared" -gt 0 ] && echo "PASS compared $compared released sections with their tags" || echo "SKIP no tag to compare with here (not a git checkout, or one without tags)"
echo "cli_changelog: $fails failed"
[ "$fails" -eq 0 ]
