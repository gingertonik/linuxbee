#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_rules.sh /path/to/linuxbeectl
# Expected values: docs/spec/rules.md, U-CLI and the rules each case names.
set -u
bin="$1"
fails=0

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

ROCK='{"match":"all","rules":[{"field":"Genre","op":"is","values":["Rock"]}]}'
# U-JSON through check: the canonical text comes back, whatever the spacing or key order was
check check 0 "$ROCK" rules check "$ROCK"
check check-loose 0 "$ROCK" rules check ' { "rules" : [ { "values":["Rock"], "op":"is", "field":"Genre" } ] } '
check check-empty 0 '{"match":"all","rules":[]}' rules check '{}'

# U-CLI match: exit 0 and "match", or exit 1 and "no match"
check match 0 "match" rules match --field "Genre=Blues; Rock" "$ROCK"
check no-match 1 "no match" rules match --field "Genre=Jazz" "$ROCK"
check no-field 1 "no match" rules match "$ROCK"
check empty-set 0 "match" rules match '{}'

ANY='{"match":"any","rules":[{"field":"Rating","op":"greater-than","values":["4"]},{"field":"Artist","op":"is","values":["Adele"],"and":[{"field":"Year","op":"before","values":["2012"]}]}]}'
check any-first 0 "match" rules match --field "Rating=4.5" --field "Artist=Muse" "$ANY"
check any-second 0 "match" rules match --field "Rating=3" --field "Artist=adele" --field "Year=2011-01-24" "$ANY"
check any-sub-fails 1 "no match" rules match --field "Rating=3" --field "Artist=Adele" --field "Year=2015" "$ANY"

# U-LAST with --now
LAST='{"rules":[{"field":"Last Played","op":"in-the-last","values":["7","days"]}]}'
check last-yes 0 "match" rules match --now 2026-11-13T12:00:00Z --field "Last Played=2026-11-10T08:00:00" "$LAST"
check last-no 1 "no match" rules match --now 2026-11-13T12:00:00Z --field "Last Played=2026-11-01" "$LAST"
check last-never 1 "no match" rules match --now 2026-11-13T12:00:00Z --field "Last Played=" "$LAST"

# refusals: exit 2, nothing on standard output, a message on standard error
check not-json 2 "" rules check 'Genre is Rock'
check unknown-op 2 "" rules check '{"rules":[{"field":"A","op":"equals","values":["x"]}]}'
check invalid 2 "" rules check '{"rules":[{"field":"A","op":"is"}]}'
check invalid-match 2 "" rules match --field "A=x" '{"rules":[{"field":"A","op":"matches","values":["("]}]}'
check bad-now 2 "" rules match --now soon "$ROCK"
check bad-field 2 "" rules match --field Genre "$ROCK"
check no-json 2 "" rules check
check no-subcommand 2 "" rules
check bad-subcommand 2 "" rules frob "$ROCK"
err=$("$bin" rules check '{"rules":[{"field":"A","op":"is","values":["x"]},{"field":"B","op":"in-range","values":["9","1"]}]}' 2>&1 >/dev/null)
case "$err" in "rule 2:"*) pass problem-position ;; *) fail "problem-position: [$err]" ;; esac

case "$("$bin" --help 2>/dev/null)" in
    *"  rules "*) pass help-lists-rules ;;
    *) fail help-lists-rules ;;
esac

echo "cli_rules: $fails failed"
[ "$fails" -eq 0 ]
