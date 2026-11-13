#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_organise.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set)
# Expected values: docs/spec/organise.md (O-CLI and the rules named) and the
# tags tests/data/tags/make_fixtures.py wrote into the fixtures.
# Everything happens in a temporary folder, on copies.
set -u
bin="$1"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-organise.XXXXXX") || exit 2
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/xdg-data"

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
listing() { (cd "$1" && find . -type f | sort); }
fresh() {
    chmod -R u+w "$work/in" "$work/out" 2>/dev/null; rm -rf "$work/in" "$work/out"
    mkdir -p "$work/in/a" "$work/in/b" "$work/out"
    cp "$data/basic.mp3" "$work/in/a/x.mp3"
    cp "$data/basic.flac" "$work/in/a/y.flac"
    cp "$data/basic.ogg" "$work/in/b/o.ogg"
    echo jpg > "$work/in/a/cover.jpg"
    chmod -R u+w "$work/in"
}
MP3="Adele/21/01-11 Someone like You.mp3"
FLAC="Various Artists/Flac Album/02-05 Flac Title.flac"

# --dry-run: the plan, and nothing touched. A folder argument is searched (O-MUSIC order).
fresh
before=$(listing "$work")
check dry-run 0 "would move: $work/in/a/x.mp3 -> $work/out/$MP3
would move: $work/in/a/y.flac -> $work/out/$FLAC
skip: $work/in/b/o.ogg: missing tags: Album Artist, Album" organise --dry-run --to "$work/out" "$work/in"
[ "$(listing "$work")" = "$before" ] && pass dry-run-touched-nothing || fail dry-run-touched-nothing

# The real run. O-EMPTY: in/a still holds cover.jpg, so it stays.
check move 0 "moved: $work/in/a/x.mp3 -> $work/out/$MP3
moved: $work/in/a/y.flac -> $work/out/$FLAC
skip: $work/in/b/o.ogg: missing tags: Album Artist, Album" organise --to "$work/out" "$work/in"
[ "$(listing "$work/out")" = "./$MP3
./$FLAC" ] && pass move-result || fail "move-result: $(listing "$work/out")"
cmp -s "$work/out/$MP3" "$data/basic.mp3" && pass move-bytes || fail move-bytes
[ -f "$work/in/a/cover.jpg" ] && pass cover-stayed || fail cover-stayed
check again 0 "skip: $work/out/$MP3: already in place" organise --to "$work/out" "$work/out/$MP3"

# --other-files and the removed folders. O-OTHER: in/a also holds y.flac, a
# track that stays behind here, so the cover stays with it.
fresh
check other-files-stay 0 "moved: $work/in/a/x.mp3 -> $work/out/$MP3" organise --other-files \
    --to "$work/out" "$work/in/a/x.mp3"
[ -f "$work/in/a/cover.jpg" ] && pass other-stayed || fail other-stayed
fresh
check other-files-go 0 "moved: $work/in/a/x.mp3 -> $work/out/$MP3
moved: $work/in/a/y.flac -> $work/out/Adele/21/Someone.flac
moved: $work/in/a/cover.jpg -> $work/out/Adele/21/cover.jpg
removed folder: $work/in/a" organise --other-files --to "$work/out" \
    --template '$If(<.Ext>=flac,Adele\21\Someone,<Album Artist>\<Album>\<Disc-Track#> <Title>)' "$work/in/a"
[ ! -e "$work/in/a" ] && [ -d "$work/in/b" ] && pass folder-removed || fail folder-removed

# --copy, --keep-names, --keep-empty-folders, --allow-missing
fresh
check copy 0 "copied: $work/in/a/x.mp3 -> $work/out/x.mp3" organise --copy --keep-names \
    --to "$work/out" "$work/in/a/x.mp3"
[ -f "$work/in/a/x.mp3" ] && pass copy-kept-source || fail copy-kept-source
check allow-missing 0 "moved: $work/in/b/o.ogg -> $work/out/Unknown Album Artist/Unknown Album/01-04 Ogg Title.ogg" \
    organise --allow-missing --keep-empty-folders --to "$work/out" "$work/in/b/o.ogg"
[ -d "$work/in/b" ] && pass kept-empty-folder || fail kept-empty-folder
# no --to: rename in place
check in-place 0 "moved: $work/in/a/y.flac -> $work/in/a/Flac Title.flac" organise --template '<Title>' "$work/in/a/y.flac"

# conflicts: O-CONFLICT, and --replace
fresh
mkdir -p "$work/out/Adele/21" && echo old > "$work/out/$MP3"
check conflict-dry 0 "would move: $work/in/a/x.mp3 -> $work/out/Adele/21/01-11 Someone like You_1.mp3" \
    organise --dry-run --to "$work/out" "$work/in/a/x.mp3"
check replace-dry 0 "would move: $work/in/a/x.mp3 -> $work/out/$MP3 (replaces)" \
    organise --dry-run --replace --to "$work/out" "$work/in/a/x.mp3"
check replace 0 "moved: $work/in/a/x.mp3 -> $work/out/$MP3" \
    organise --replace --delete-replaced --to "$work/out" "$work/in/a/x.mp3"
cmp -s "$work/out/$MP3" "$data/basic.mp3" && pass replaced-bytes || fail replaced-bytes

# failures: exit 3, the message on standard error, the others still done
fresh
mkdir -p "$work/out/Adele" && chmod a-w "$work/out/Adele"
check failure 3 "moved: $work/in/a/y.flac -> $work/out/$FLAC" organise --to "$work/out" \
    "$work/in/a/x.mp3" "$work/in/a/y.flac"
chmod u+w "$work/out/Adele"
[ -f "$work/in/a/x.mp3" ] && pass failed-file-stayed || fail failed-file-stayed
fresh
err=$("$bin" organise --to "$work/out" "$work/in/nope.mp3" "$work/in/a/cover.jpg" 2>&1 >/dev/null)
[ -z "$err" ] && pass skips-are-not-errors || fail "skips-are-not-errors: [$err]"
check skips 0 "skip: $work/in/nope.mp3: not found
skip: $work/in/a/cover.jpg: not a music file" organise --to "$work/out" "$work/in/nope.mp3" "$work/in/a/cover.jpg"

# usage mistakes: exit 2, nothing done
before=$(listing "$work")
check no-input 2 "" organise --to "$work/out"
check bad-template 2 "" organise --to "$work/out" --template '$Nope(x)' "$work/in/a/x.mp3"
check template-and-keep 2 "" organise --to "$work/out" --template '<Title>' --keep-names "$work/in/a/x.mp3"
check keep-needs-to 2 "" organise --keep-names "$work/in/a/x.mp3"
check unknown-option 2 "" organise --frob --to "$work/out" "$work/in/a/x.mp3"
[ "$(listing "$work")" = "$before" ] && pass usage-touched-nothing || fail usage-touched-nothing

case "$("$bin" --help 2>/dev/null)" in
    *"  organise "*) pass help-lists-organise ;;
    *) fail help-lists-organise ;;
esac

echo "cli_organise: $fails failed"
[ "$fails" -eq 0 ]
