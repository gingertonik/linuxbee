#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_organise_twice.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/organise.md, O-PLAN ("An input given more than once"), O-EMPTY
# ("Floors") and O-CLI. The fault this holds shut (a probe of 2026-11-19): a folder named with
# the folder above it, or named twice, had each of its files planned once for each naming. A Copy
# made x.mp3 and x_1.mp3; a Move moved the file, then said `failed: ...: the file is gone` and
# exited 3. Everything happens in a temporary folder, on copies.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-organise-twice.XXXXXX") || exit 2
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/xdg-data" XDG_CONFIG_HOME="$work/xdg-config" XDG_CACHE_HOME="$work/xdg-cache"

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
# check LABEL RC STDOUT ARGS...: the exit status, standard output whole, and nothing on standard error.
check() {
    label=$1; want_rc=$2; want_out=$3
    shift 3
    out=$("$bin" "$@" 2>"$work/stderr")
    rc=$?
    err=$(cat "$work/stderr")
    if [ "$rc" != "$want_rc" ] || [ "$out" != "$want_out" ] || [ -n "$err" ]; then
        fail "$label: rc=$rc (want $want_rc) stdout=[$out] (want [$want_out]) stderr=[$err]"
    else
        pass "$label"
    fi
}
listing() { (cd "$1" && find . -type f | sort | tr '\n' ' '); }
fresh() {
    rm -rf "$work/in" "$work/out"
    mkdir -p "$work/in/a" "$work/out"
    cp "$data/basic.mp3" "$work/in/a/x.mp3"
    cp "$data/basic.flac" "$work/in/a/y.flac"
}
X="$work/in/a/x.mp3"; Y="$work/in/a/y.flac"
MOVED="moved: $X -> $work/out/x.mp3
moved: $Y -> $work/out/y.flac"
COPIED="copied: $X -> $work/out/x.mp3
copied: $Y -> $work/out/y.flac"
BOTH="./x.mp3 ./y.flac "

fresh
check dry-run-folder-and-above 0 "would move: $X -> $work/out/x.mp3
would move: $Y -> $work/out/y.flac" organise --dry-run --keep-names --to "$work/out" "$work/in/a" "$work/in"

fresh
check move-folder-and-above 0 "$MOVED" organise --keep-names --to "$work/out" "$work/in/a" "$work/in"
[ "$(listing "$work/out")" = "$BOTH" ] && pass move-result || fail "move-result: $(listing "$work/out")"
[ -z "$(listing "$work/in")" ] && pass move-sources-gone || fail "move-sources-gone: $(listing "$work/in")"

fresh
check copy-folder-and-above 0 "$COPIED" organise --copy --keep-names --to "$work/out" "$work/in/a" "$work/in"
[ "$(listing "$work/out")" = "$BOTH" ] && pass copy-result || fail "copy-result: $(listing "$work/out")"
[ "$(listing "$work/in")" = "./a/x.mp3 ./a/y.flac " ] && pass copy-sources-stay || fail copy-sources-stay

fresh
check copy-folder-twice 0 "$COPIED" organise --copy --keep-names --to "$work/out" "$work/in/a" "$work/in/a"
[ "$(listing "$work/out")" = "$BOTH" ] && pass copy-twice-result || fail "copy-twice-result: $(listing "$work/out")"

fresh
check copy-file-three-times 0 "copied: $X -> $work/out/x.mp3" organise --copy --keep-names \
    --to "$work/out" "$X" "$work/in/a/../a/x.mp3" "$X"
[ "$(listing "$work/out")" = "./x.mp3 " ] && pass copy-thrice-result || fail "copy-thrice-result: $(listing "$work/out")"

fresh
check move-file-and-its-folder 0 "$MOVED" organise --keep-names --to "$work/out" "$X" "$work/in/a"

# O-EMPTY: a folder that was named is never removed, the folder above it named or not.
fresh
check named-folder-stays 0 "$MOVED" organise --remove-empty-folders --keep-names \
    --to "$work/out" "$work/in/a" "$work/in"
[ -d "$work/in/a" ] && pass named-folder-is-there || fail named-folder-is-there
# Named alone, the folder above: the one below it goes, as before.
fresh
check folder-below-goes 0 "$MOVED
removed folder: $work/in/a" organise --remove-empty-folders --keep-names --to "$work/out" "$work/in"

echo "cli_organise_twice: $fails failed"
[ "$fails" -eq 0 ]
