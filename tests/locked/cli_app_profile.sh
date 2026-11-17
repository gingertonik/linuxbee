#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Where the application keeps its files, the files of an older build taken along, and one program a profile.
# Usage: cli_app_profile.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Rules: docs/spec/main-window.md (W-PLACES, W-ADOPT, W-ONE). Everything happens in folders of this test's own.
set -u
ctl=$1
app="${LINUXBEE_APP:?}"
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-profile.XXXXXX") || exit 2
trap 'for p in $pids; do kill -9 "$p" 2> /dev/null; done; rm -rf "$work"' EXIT
pids=""
export QT_QPA_PLATFORM=offscreen
box() { b="$work/$1"; mkdir -p "$b/config" "$b/data" "$b/cache" "$b/tmp"; }
run() { XDG_CONFIG_HOME="$b/config" XDG_DATA_HOME="$b/data" XDG_CACHE_HOME="$b/cache" TMPDIR="$b/tmp" timeout 15 "$app" "$@"; }
start() { # in the background; up when its library file is there
    XDG_CONFIG_HOME="$b/config" XDG_DATA_HOME="$b/data" XDG_CACHE_HOME="$b/cache" TMPDIR="$b/tmp" "$app" > /dev/null 2> "$b/stderr" &
    pid=$!; pids="$pids $pid"
    n=0; while [ $n -lt 80 ] && [ ! -s "$b/data/linuxbee/library.db" ]; do sleep 0.1; n=$((n + 1)); done
    sleep 0.5
}
gone() { n=0; while [ $n -lt 60 ] && kill -0 "$1" 2> /dev/null; do sleep 0.1; n=$((n + 1)); done; ! kill -0 "$1" 2> /dev/null; }

# --- W-PLACES: one folder for the settings, one for the data, the name once in each
box places
run --version > /dev/null 2>&1
[ -z "$(find "$b/config" "$b/data" "$b/cache" -mindepth 1)" ] && pass version-writes-nothing || fail version-writes-nothing
run --screenshot "$b/w.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && pass places-start || fail "places-start rc=$rc"
[ -s "$b/config/linuxbee/linuxbee.ini" ] && pass places-settings || fail "places-settings: $(find "$b/config" -name linuxbee.ini)"
[ -s "$b/data/linuxbee/library.db" ] && pass places-library || fail "places-library: $(find "$b/data" -name library.db)"
[ -d "$b/data/linuxbee/playlists" ] && pass places-playlists || fail places-playlists
[ ! -e "$b/config/linuxbee/linuxbee" ] && [ ! -e "$b/data/linuxbee/linuxbee" ] && pass places-name-once || fail places-name-once
[ "$(ls -A "$b/config")" = linuxbee ] && [ "$(ls -A "$b/data")" = linuxbee ] && pass places-nothing-else || fail "places-nothing-else: $(ls -A "$b/config" "$b/data" | tr '\n' ' ')"
left=$(ls -A "$b/data/linuxbee" | grep -v '^library.db$\|^playlists$\|^auto-playlists$' | tr '\n' ' ')
[ -z "$left" ] && pass places-no-lock-left || fail "places-no-lock-left: $left"

# --- W-ADOPT: the files of a build that had the name twice are taken along, once
box adopt
old="$b/data/linuxbee/linuxbee"; oldc="$b/config/linuxbee/linuxbee"
mkdir -p "$old/playlists" "$old/skins" "$old/auto-playlists" "$oldc" "$b/music"
cp "$data/basic.mp3" "$data/basic.flac" "$data/basic.ogg" "$b/music/"
"$ctl" library --db "$old/library.db" scan --now 2026-11-13T12:00:00Z "$b/music" > /dev/null 2>&1
printf '%s\n' "$b/music/basic.mp3" > "$old/playlists/Mix.m3u8"
echo skin > "$old/skins/x.xml"
printf '[keep]\nme=1\n' > "$oldc/linuxbee.ini"
sum=$(sha256sum < "$old/playlists/Mix.m3u8")
run --screenshot "$b/w.png" > /dev/null 2> "$b/stderr"; rc=$?
[ "$rc" = 0 ] && pass adopt-start || fail "adopt-start rc=$rc: $(head -n 2 "$b/stderr")"
[ "$("$ctl" library --db "$b/data/linuxbee/library.db" count 2> /dev/null)" = 3 ] && pass adopt-library || fail "adopt-library: $("$ctl" library --db "$b/data/linuxbee/library.db" count 2>&1)"
[ "$(sha256sum < "$b/data/linuxbee/playlists/Mix.m3u8" 2> /dev/null)" = "$sum" ] && pass adopt-playlist || fail adopt-playlist
[ -f "$b/data/linuxbee/skins/x.xml" ] && pass adopt-skins || fail adopt-skins
grep -q '^me=1$' "$b/config/linuxbee/linuxbee.ini" 2> /dev/null && pass adopt-settings || fail adopt-settings
[ ! -e "$old" ] && [ ! -e "$oldc" ] && pass adopt-old-gone || fail "adopt-old-gone: $(ls -A "$old" "$oldc" 2> /dev/null | tr '\n' ' ')"
# again: nothing to take, nothing changed
run --screenshot "$b/w.png" > /dev/null 2>&1
[ "$("$ctl" library --db "$b/data/linuxbee/library.db" count 2> /dev/null)" = 3 ] && pass adopt-again || fail adopt-again

# Both are there: what is in the new place stays, what is in the old is left as it is, and named.
box both
old="$b/data/linuxbee/linuxbee"
mkdir -p "$old/playlists" "$b/data/linuxbee/playlists" "$b/music" "$b/music1"
cp "$data/basic.mp3" "$data/basic.flac" "$b/music/"
cp "$data/basic.ogg" "$b/music1/"
"$ctl" library --db "$old/library.db" scan --now 2026-11-13T12:00:00Z "$b/music" > /dev/null 2>&1
"$ctl" library --db "$b/data/linuxbee/library.db" scan --now 2026-11-13T12:00:00Z "$b/music1" > /dev/null 2>&1
[ "$("$ctl" library --db "$old/library.db" count 2> /dev/null)" = 2 ] && [ "$("$ctl" library --db "$b/data/linuxbee/library.db" count 2> /dev/null)" = 1 ] || fail "both-setup"
echo old > "$old/playlists/Old.m3u8"; echo new > "$b/data/linuxbee/playlists/New.m3u8"; echo extra > "$old/notes.txt"
oldsum=$(sha256sum < "$old/library.db")
run --screenshot "$b/w.png" > /dev/null 2> "$b/stderr"; rc=$?
[ "$rc" = 0 ] && pass both-start || fail "both-start rc=$rc"
[ "$("$ctl" library --db "$b/data/linuxbee/library.db" count 2> /dev/null)" = 1 ] && pass both-new-library-kept || fail both-new-library-kept
[ "$(sha256sum < "$old/library.db" 2> /dev/null)" = "$oldsum" ] && pass both-old-library-untouched || fail both-old-library-untouched
[ -f "$old/playlists/Old.m3u8" ] && [ ! -e "$b/data/linuxbee/playlists/Old.m3u8" ] && pass both-folders-not-merged || fail both-folders-not-merged
[ -f "$b/data/linuxbee/notes.txt" ] && [ ! -e "$old/notes.txt" ] && pass both-free-name-taken || fail both-free-name-taken
grep -q "$old/library.db" "$b/stderr" && grep -q "$old/playlists" "$b/stderr" && pass both-named || fail "both-named: $(head -n 3 "$b/stderr" | tr '\n' ' ')"

# The old folder is a link: not followed, nothing taken.
box link
mkdir -p "$b/elsewhere" "$b/data/linuxbee"; echo precious > "$b/elsewhere/library.db"
ln -s "$b/elsewhere" "$b/data/linuxbee/linuxbee"
run --screenshot "$b/w.png" > /dev/null 2>&1
[ "$(cat "$b/elsewhere/library.db")" = precious ] && [ -L "$b/data/linuxbee/linuxbee" ] && pass link-left || fail link-left
[ "$("$ctl" library --db "$b/data/linuxbee/library.db" count 2> /dev/null)" = 0 ] && pass link-fresh-library || fail link-fresh-library

# --- W-ONE: one program a profile
box one
start; first=$pid
kill -0 "$first" 2> /dev/null && pass one-first-up || fail one-first-up
before=$(cd "$b" && find config data -type f -exec sha256sum {} + | sort)
run --parts > "$b/out2" 2> "$b/err2"; rc=$?
[ "$rc" = 5 ] && pass one-second-refused || fail "one-second-refused rc=$rc"
grep -qi 'already running' "$b/err2" && pass one-second-says || fail "one-second-says: $(head -n 1 "$b/err2")"
[ ! -s "$b/out2" ] && pass one-second-prints-nothing || fail one-second-prints-nothing
run > /dev/null 2>&1; rc=$?
[ "$rc" = 5 ] && pass one-second-window-refused || fail "one-second-window-refused rc=$rc"
run --screenshot "$b/w2.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 5 ] && [ ! -e "$b/w2.png" ] && pass one-second-screenshot-refused || fail "one-second-screenshot-refused rc=$rc"
after=$(cd "$b" && find config data -type f -exec sha256sum {} + | sort)
[ "$before" = "$after" ] && pass one-second-changed-nothing || fail one-second-changed-nothing
run --version > /dev/null 2>&1 && pass one-version-still-answers || fail one-version-still-answers
kill -0 "$first" 2> /dev/null && pass one-first-still-up || fail one-first-still-up
# another profile is another matter
box other; run --parts > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && pass one-other-profile || fail "one-other-profile rc=$rc"
b="$work/one"
kill -TERM "$first"; gone "$first" && pass one-first-ended || fail one-first-ended
run --parts > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && pass one-after-exit || fail "one-after-exit rc=$rc"
# a program that was killed does not hold the profile for ever
start; first=$pid
kill -9 "$first"; gone "$first"
run --parts > /dev/null 2> "$b/err3"; rc=$?
[ "$rc" = 0 ] && pass one-after-kill || fail "one-after-kill rc=$rc: $(head -n 1 "$b/err3")"

# Two starts at the same moment on a profile with nothing in it: one of them or both run, neither breaks.
bad=""; i=0
while [ $i -lt 10 ]; do
    box "race$i"
    (run --parts > /dev/null 2> "$b/e1"; echo $? > "$b/rc1") &
    p1=$!
    (run --parts > /dev/null 2> "$b/e2"; echo $? > "$b/rc2") &
    wait $p1; wait $!
    r1=$(cat "$b/rc1"); r2=$(cat "$b/rc2")
    case "$r1$r2" in 00 | 05 | 50) ;; *) bad="$bad round $i: $r1 $r2 ($(head -n 1 "$b/e1" "$b/e2" | tr '\n' ' '));" ;; esac
    [ "$("$ctl" library --db "$b/data/linuxbee/library.db" count 2> /dev/null)" = 0 ] || bad="$bad round $i: no usable library;"
    i=$((i + 1))
done
[ -z "$bad" ] && pass two-first-starts || fail "two-first-starts:$bad"

echo "cli_app_profile: $fails failed"
[ "$fails" -eq 0 ]
