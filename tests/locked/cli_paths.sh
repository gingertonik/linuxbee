#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_paths.sh /path/to/linuxbeectl
# Expected values: docs/spec/paths.md, section "Command line" (P-CLI), and the
# rules each case names. W-H is the wiki example path.
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

T='<Album Artist>\<Album>\<Disc-Track#> <Title>'
# W-H
check wiki-example 0 "Adele/21/01-11 Someone like You.mp3" path build --ext mp3 \
    --field "Album Artist=Adele" --field "Album=21" --field "Disc-Track#=01-11" \
    --field "Title=Someone like You" "$T"
# P-EXT: no --ext means no extension
check no-ext 0 "Adele/Someone like You" path build --field "Album Artist=Adele" \
    --field "Title=Someone like You" '<Album Artist>\<Title>'
# P-SEP, P-MAP
check value-slash 0 "AC DC/T.N.T.flac" path build --ext flac --field "Artist=AC/DC" \
    --field "Title=T.N.T." '<Artist>/<Title>'
# P-MISSING: exit 3, nothing on stdout
check missing 3 "" path build --ext mp3 --field "Album Artist=Adele" --field "Album=" \
    --field "Title=x" '<Album Artist>\<Album>\<Title>'
# P-MISSING with --allow-missing (W-O "Unknown X")
check allow-missing 0 "Adele/Unknown Album/x.mp3" path build --ext mp3 --allow-missing \
    --field "Album Artist=Adele" --field "Album=" --field "Title=x" '<Album Artist>\<Album>\<Title>'
# P-EMPTY: exit 3
check empty-name 3 "" path build --ext mp3 --field "Title=..." '<Title>'
# T-ERRORS through P-CLI: exit 2
check bad-template 2 "" path build --ext mp3 '$Nope(x)'
# P-CLI usage mistakes: exit 2
check no-template 2 "" path build --ext mp3
check bad-field 2 "" path build --field "Title" '<Title>'
check no-subcommand 2 "" path
check bad-subcommand 2 "" path frob '<Title>'
check bad-max 2 "" path build --max-bytes zero '<Title>'
# P-CLI --map: "C=TEXT" replaces character C; "C=" removes it
check map 0 "Someone_like_You.mp3" path build --ext mp3 --map " =_" \
    --field "Title=Someone like You" '<Title>'
check map-remove 0 "Who.mp3" path build --ext mp3 --map "?=" --field "Title=W?h?o" '<Title>'
# P-LENGTH through --max-bytes. MATH: 12 - 4 = 8
check max-bytes 0 "abcdefgh.mp3" path build --ext mp3 --max-bytes 12 \
    --field "Title=abcdefghijklmnop" '<Title>'

# P-CLI: the missing tags are named on stderr, in template order
err=$("$bin" path build --field "A=" --field "B=" '<B>\<A>' 2>&1 >/dev/null)
case "$err" in
    "missing tags: B, A") echo "PASS missing-stderr" ;;
    *) echo "FAIL missing-stderr: [$err]"; fails=$((fails + 1)) ;;
esac
# P-CLI: the command is listed
case "$("$bin" --help 2>/dev/null)" in
    *"  path "*) echo "PASS help-lists-path" ;;
    *) echo "FAIL help-lists-path"; fails=$((fails + 1)) ;;
esac

echo "cli_paths: $fails failed"
[ "$fails" -eq 0 ]
