#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# A scan puts back what Organise set aside, or keeps it and names both files, and says so once
# (L-CLI, L-SETASIDE, fix-h1). Reading commands never do it.
# Usage: cli_library_scan_setaside.sh /path/to/linuxbeectl   (LINUXBEE_TEST_DATA set)
# Everything happens in folders of this test's own. LINUXBEE_LEFTOVER_AGE=0 makes a file made
# here count as old.
set -u
bin=$1
data="${LINUXBEE_TEST_DATA:?}"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-setaside.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
export QT_QPA_PLATFORM=offscreen LINUXBEE_LEFTOVER_AGE=0
export XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$work/m/a/.linuxbee-replaced-1" "$work/m/b/.linuxbee-replaced-2"
cp "$data/tags/basic.mp3" "$work/m/a/.linuxbee-replaced-1/old.mp3"
cp "$data/tags/basic.flac" "$work/m/b/.linuxbee-replaced-2/new.flac"
printf 'the newcomer' > "$work/m/b/taken.mp3"
printf 'the old one' > "$work/m/b/.linuxbee-replaced-2/taken.mp3"
now=2026-11-13T12:00:00Z
lib="$work/library.db"

# Reading commands never put anything back.
"$bin" library --db "$lib" count > /dev/null 2>&1
"$bin" library --db "$lib" list > /dev/null 2>&1
[ -e "$work/m/a/.linuxbee-replaced-1/old.mp3" ] && [ ! -e "$work/m/a/old.mp3" ] \
    && pass reading-leaves-it || fail reading-leaves-it

out=$("$bin" library --db "$lib" scan --now "$now" "$work/m" 2> "$work/err"); rc=$?
[ "$rc" = 0 ] && pass setaside-rc || fail "setaside-rc rc=$rc [$(cat "$work/err")]"
want=$(printf 'put back 2 files that Organise had set aside:\n  %s\n  %s\nkept 1 file that Organise had set aside, because a file has its name now:\n  %s (beside %s)' \
    "$work/m/a/old.mp3" "$work/m/b/new.flac" "$work/m/b/.linuxbee-replaced-2/taken.mp3" "$work/m/b/taken.mp3")
said=$(printf '%s\n' "$out" | sed -n '2,$p')
[ "$said" = "$want" ] && pass setaside-said || fail "setaside-said [$said] want [$want]"
cmp -s "$data/tags/basic.mp3" "$work/m/a/old.mp3" && pass put-back-whole || fail put-back-whole
[ ! -e "$work/m/a/.linuxbee-replaced-1" ] && pass empty-waiting-folder-gone || fail empty-waiting-folder-gone
[ "$(cat "$work/m/b/taken.mp3")" = "the newcomer" ] && [ "$(cat "$work/m/b/.linuxbee-replaced-2/taken.mp3")" = "the old one" ] \
    && pass both-kept || fail both-kept

# Said once: the next scan has nothing to put back, and names the one kept again.
out=$("$bin" library --db "$lib" scan --now "$now" "$work/m" 2> /dev/null)
want=$(printf 'kept 1 file that Organise had set aside, because a file has its name now:\n  %s (beside %s)' \
    "$work/m/b/.linuxbee-replaced-2/taken.mp3" "$work/m/b/taken.mp3")
said=$(printf '%s\n' "$out" | sed -n '2,$p')
[ "$said" = "$want" ] && pass again-said || fail "again-said [$said]"

echo "cli_library_scan_setaside: $fails failed"
[ "$fails" = 0 ]
