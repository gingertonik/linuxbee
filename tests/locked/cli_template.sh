#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_template.sh /path/to/linuxbeectl
# Expected values: docs/spec/templates.md, section "Command line", and the
# source tags of the template rules each case exercises.
set -u
bin="$1"
fails=0

# check NAME WANT_RC WANT_STDOUT ARGS...
check() {
    name=$1; want_rc=$2; want_out=$3
    shift 3
    out=$("$bin" "$@" 2>/dev/null)
    rc=$?
    if [ "$rc" != "$want_rc" ] || [ "$out" != "$want_out" ]; then
        echo "FAIL $name: rc=$rc (want $want_rc) stdout=[$out] (want [$want_out])"
        fails=$((fails + 1))
    else
        echo "PASS $name"
    fi
}

# F-C: $Sort moves the ignore word to the end
check sort 0 "Beatles, The" template eval --field "Artist=The Beatles" '$Sort(<Artist>)'
# F-C: $Pad
check pad 0 "001" template eval --field "Track#=1" '$Pad(<Track#>,3)'
# T-UNKNOWN: a field nobody supplied is printed as written
check unknown-field 0 "<Artist>" template eval '<Artist>'
# W-O: blank field becomes "Unknown X" when asked
check unknown-for-blank 0 "Unknown Album" template eval --field "Album=" --unknown-for-blank '<Album>'
# T-BLANK: and stays empty when not asked
check blank 0 "" template eval --field "Album=" '<Album>'
# T-CLI: the value is everything after the first '='
check equals-in-value 0 "a=b" template eval --field "Title=a=b" '<Title>'
# T-CLI: two fields
check two-fields 0 "A - B" template eval --field "Artist=A" --field "Title=B" '<Artist> - <Title>'
# T-CLI: parse error is exit 2 with nothing on stdout
check parse-error 2 "" template eval '$Nope(x)'
check check-ok 0 "OK" template check '$Upper(a)'
check check-bad 2 "" template check '$Upper(a'
# T-CLI: usage errors are exit 2
check field-without-equals 2 "" template eval --field "Artist" 'x'
check no-template 2 "" template eval
check no-subcommand 2 "" template
check unknown-command 2 "" frobnicate

# T-CLI: the parse error goes to stderr as "error at N: message"
err=$("$bin" template eval 'ab $Nope(x)' 2>&1 >/dev/null)
case "$err" in
    "error at 3: "?*) echo "PASS stderr-format" ;;
    *) echo "FAIL stderr-format: [$err]"; fails=$((fails + 1)) ;;
esac

echo "cli_template: $fails failed"
[ "$fails" -eq 0 ]
