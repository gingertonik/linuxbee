#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_organise_ownfiles.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# docs/spec/organise.md, O-CONFLICT: a run never replaces one of its own files. This is the case the
# write-path audit lost a file with: a chain of names, --replace --delete-replaced.
# Everything happens in a temporary folder, on copies, with folders of its own for the program.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-ownfiles.XXXXXX") || exit 2
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/xdg-data" XDG_CONFIG_HOME="$work/xdg-config" XDG_CACHE_HOME="$work/xdg-cache"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
sum() { sha256sum "$1" | cut -c1-64; }
fresh() {
    rm -rf "$work/m"; mkdir -p "$work/m"
    cp "$data/basic.flac" "$work/m/a.flac"; cp "$data/basic.flac" "$work/m/b.flac"; chmod u+w "$work/m"/*
    "$bin" tags write --set "Title=$1" "$work/m/a.flac" > /dev/null || fail "setup a"
    "$bin" tags write --set "Title=$2" "$work/m/b.flac" > /dev/null || fail "setup b"
    A=$(sum "$work/m/a.flac"); B=$(sum "$work/m/b.flac")
}

for how in "--replace --delete-replaced" "--replace"; do
    fresh b c
    # shellcheck disable=SC2086
    dry=$("$bin" organise --dry-run --template '<Title>' $how "$work/m/a.flac" "$work/m/b.flac" 2> /dev/null)
    [ "$dry" = "would move: $work/m/a.flac -> $work/m/b_1.flac
would move: $work/m/b.flac -> $work/m/c.flac" ] && pass "chain-plan ($how)" || fail "chain-plan ($how): [$dry]"
    # shellcheck disable=SC2086
    out=$("$bin" organise --template '<Title>' $how "$work/m/a.flac" "$work/m/b.flac" 2> /dev/null); rc=$?
    [ "$rc" = 0 ] && [ "$out" = "moved: $work/m/a.flac -> $work/m/b_1.flac
moved: $work/m/b.flac -> $work/m/c.flac" ] && pass "chain-said ($how)" || fail "chain-said ($how): rc=$rc [$out]"
    [ "$(ls -A "$work/m" | tr '\n' ' ')" = "b_1.flac c.flac " ] && pass "chain-names ($how)" || fail "chain-names ($how): $(ls -A "$work/m" | tr '\n' ' ')"
    [ "$(sum "$work/m/b_1.flac" 2> /dev/null)" = "$A" ] && [ "$(sum "$work/m/c.flac" 2> /dev/null)" = "$B" ] && pass "chain-both-whole ($how)" || fail "chain-both-whole ($how)"
    [ -z "$(ls -A "$work/xdg-data/Trash/files" 2> /dev/null)" ] && pass "chain-nothing-trashed ($how)" || fail "chain-nothing-trashed ($how)"

    fresh b a
    # shellcheck disable=SC2086
    "$bin" organise --template '<Title>' $how "$work/m/a.flac" "$work/m/b.flac" > /dev/null 2>&1
    [ "$(sum "$work/m/b_1.flac" 2> /dev/null)" = "$A" ] && [ "$(sum "$work/m/a.flac" 2> /dev/null)" = "$B" ] && pass "swap-both-whole ($how)" || fail "swap-both-whole ($how): $(ls -A "$work/m" | tr '\n' ' ')"
done

echo "cli_organise_ownfiles: $fails failed"
[ "$fails" = 0 ]
