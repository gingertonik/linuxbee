#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_rules_operators.sh /path/to/linuxbeectl
# Expected values: docs/spec/rules.md, U-CLI and U-NAMES. `linuxbeectl rules operators` lists
# every operator: its id and what a person reads for it. The regular expression that heeds case
# is MusicBee's "match RegEx"; `matches`, which ignores case, is its "/i" form.
set -u
bin="$1"
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
tab=$(printf '\t')
want=$(cat <<EOF
is${tab}is
is-not${tab}is not
contains${tab}contains
does-not-contain${tab}does not contain
starts-with${tab}starts with
ends-with${tab}ends with
is-any-of${tab}is any of
is-not-any-of${tab}is not any of
matches${tab}match RegEx/i
does-not-match${tab}does not match RegEx/i
has-value${tab}has value
has-no-value${tab}has no value
greater-than${tab}greater than
less-than${tab}less than
in-range${tab}in range
before${tab}before
after${tab}after
in-the-last${tab}in the last
not-in-the-last${tab}not in the last
matches-case${tab}match RegEx
does-not-match-case${tab}does not match RegEx
EOF
)
out=$("$bin" rules operators 2>/dev/null)
rc=$?
[ "$rc" = 0 ] && pass operators-rc || fail "operators-rc: rc=$rc (want 0)"
[ "$out" = "$want" ] && pass operators-list || fail "operators-list: [$out]"
err=$("$bin" rules operators 2>&1 >/dev/null)
[ -z "$err" ] && pass operators-quiet || fail "operators-quiet: [$err]"
"$bin" rules operators extra >/dev/null 2>&1
rc=$?
[ "$rc" = 2 ] && pass operators-extra || fail "operators-extra: rc=$rc (want 2)"
case "$("$bin" rules 2>&1 >/dev/null)" in
    *"linuxbeectl rules operators"*) pass usage-names-it ;;
    *) fail usage-names-it ;;
esac
HEED='{"rules":[{"field":"Title","op":"matches-case","values":["^B"]}]}'
FOLD='{"rules":[{"field":"Title","op":"matches","values":["^B"]}]}'
"$bin" rules match --field "Title=beat" "$HEED" >/dev/null 2>&1
rc=$?
[ "$rc" = 1 ] && pass match-heeds-case || fail "match-heeds-case: rc=$rc (want 1)"
"$bin" rules match --field "Title=beat" "$FOLD" >/dev/null 2>&1
rc=$?
[ "$rc" = 0 ] && pass match-ignores-case || fail "match-ignores-case: rc=$rc (want 0)"
echo "cli_rules_operators: $fails failed"
[ "$fails" -eq 0 ]
