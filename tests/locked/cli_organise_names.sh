#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_organise_names.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/organise.md, O-CLI ("Names that are not valid UTF-8") and O-EXECUTE
# ("A source that is a link"). The fault this holds shut (the write-path audit, part 2, F6): a
# file whose name is not UTF-8 was left where it was by a run over its folder, with no line and
# exit 0. Everything happens in a temporary folder, on copies.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-organise-names.XXXXXX") || exit 2
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/xdg-data" XDG_CONFIG_HOME="$work/xdg-config" XDG_CACHE_HOME="$work/xdg-cache"

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
check() {
    label=$1; want_rc=$2; want_out=$3
    shift 3
    out=$("$bin" "$@" 2>"$work/stderr")
    rc=$?
    if [ "$rc" != "$want_rc" ] || [ "$out" != "$want_out" ]; then
        fail "$label: rc=$rc (want $want_rc) stdout=[$out] (want [$want_out])"
    else
        pass "$label"
    fi
}
bad1=$(printf 'bad\377.mp3')
bad2=$(printf 'worse\376.flac')
badfolder=$(printf 'dir\377')
fresh() {
    chmod -R u+w "$work/in" "$work/out" 2>/dev/null; rm -rf "$work/in" "$work/out"
    mkdir -p "$work/in/a" "$work/in/b" "$work/out"
    cp "$data/basic.mp3" "$work/in/a/x.mp3"
    cp "$data/basic.mp3" "$work/in/a/$bad1"
    cp "$data/basic.flac" "$work/in/b/y.flac"
}
fresh
if [ ! -f "$work/in/a/$bad1" ]; then
    echo "SKIPPED: this disk does not take a name that is not UTF-8"
    exit 0
fi
ONE="passed over 1 name that is not valid UTF-8, in:
  $work/in/a"

# --dry-run says it too, after the plan.
check names-dry-run 0 "would move: $work/in/a/x.mp3 -> $work/out/x.mp3
would move: $work/in/b/y.flac -> $work/out/y.flac
$ONE" organise --dry-run --keep-names --to "$work/out" "$work/in"

# The run: after every outcome. The exit status is that of the outcomes.
check names-run 0 "moved: $work/in/a/x.mp3 -> $work/out/x.mp3
moved: $work/in/b/y.flac -> $work/out/y.flac
$ONE" organise --keep-names --to "$work/out" "$work/in"
[ -f "$work/in/a/$bad1" ] && pass the-file-stayed || fail the-file-stayed
[ "$(ls -A "$work/out" | wc -l)" = 2 ] && pass nothing-else-moved || fail "nothing-else-moved: $(ls -A "$work/out")"

# After the removed folders, when there are any.
fresh
check names-after-folders 0 "moved: $work/in/a/x.mp3 -> $work/out/x.mp3
moved: $work/in/b/y.flac -> $work/out/y.flac
removed folder: $work/in/b
$ONE" organise --remove-empty-folders --keep-names --to "$work/out" "$work/in"

# More than one: a second file, and a folder, which is counted once and not gone into. The
# folders that hold them, each once, in ascending order.
fresh
cp "$data/basic.flac" "$work/in/b/$bad2"
mkdir "$work/in/b/$badfolder"
cp "$data/basic.mp3" "$work/in/b/$badfolder/z.mp3"
check names-three 0 "would move: $work/in/a/x.mp3 -> $work/out/x.mp3
would move: $work/in/b/y.flac -> $work/out/y.flac
passed over 3 names that are not valid UTF-8, in:
  $work/in/a
  $work/in/b" organise --dry-run --keep-names --to "$work/out" "$work/in"

# A folder named twice, and with the one above it: each entry is counted once.
fresh
mkdir "$work/in/c"
cp "$data/basic.mp3" "$work/in/c/$bad1"
check names-once 0 "would move: $work/in/a/x.mp3 -> $work/out/x.mp3
would move: $work/in/b/y.flac -> $work/out/y.flac
passed over 2 names that are not valid UTF-8, in:
  $work/in/a
  $work/in/c" organise --dry-run --keep-names --to "$work/out" "$work/in/c" "$work/in" "$work/in/c"

# Only folders are looked through. A file named by itself says nothing of its neighbours.
fresh
check names-file-argument 0 "moved: $work/in/a/x.mp3 -> $work/out/x.mp3" organise --keep-names \
    --to "$work/out" "$work/in/a/x.mp3"

# A folder with no such name: no line.
fresh
check names-none 0 "moved: $work/in/b/y.flac -> $work/out/y.flac" organise --keep-names \
    --to "$work/out" "$work/in/b"

# O-EXECUTE, a source that is a link, through the command line: it fails, on standard error,
# with exit 3; the link stays and the other file is moved.
fresh
rm -f "$work/in/a/$bad1"
mkdir -p "$work/real"
cp "$data/basic.mp3" "$work/real/r.mp3"
ln -s ../../real/r.mp3 "$work/in/a/l.mp3"
check link-fails 3 "moved: $work/in/a/x.mp3 -> $work/out/x.mp3" organise --keep-names \
    --to "$work/out" "$work/in/a"
want="failed: $work/in/a/l.mp3: the link would not lead to its file from there"
[ "$(cat "$work/stderr")" = "$want" ] && pass link-says-why || fail "link-says-why: [$(cat "$work/stderr")]"
[ -L "$work/in/a/l.mp3" ] && cmp -s "$work/in/a/l.mp3" "$work/real/r.mp3" && pass link-stayed || fail link-stayed
[ ! -e "$work/out/l.mp3" ] && [ ! -L "$work/out/l.mp3" ] && pass link-target-free || fail link-target-free

echo "cli_organise_names: $fails failed"
[ "$fails" -eq 0 ]
