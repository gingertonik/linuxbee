#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_shell.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Expected values: docs/spec/library-shell.md, SH-MAIN. Runs with no display,
# in temporary configuration and data folders.
set -u
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d)
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

"$app" --version > /dev/null 2>&1
[ ! -e "$work/data" ] && pass version-writes-nothing || fail version-writes-nothing

timeout 20 "$app" --screenshot "$work/window.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && pass start-exit || fail "start-exit rc=$rc"
db=$(find "$work/data" -name library.db 2>/dev/null | head -n 1)
[ -n "$db" ] && [ -s "$db" ] && pass library-file || fail library-file
[ "$(dirname "$db")" = "$work/data/linuxbee/linuxbee" ] && pass library-place || fail "library-place [$db]"
[ -d "$work/data/linuxbee/linuxbee/playlists" ] && pass playlist-folder || fail playlist-folder
grep -q '^node=library:Music$' "$work/config/linuxbee/linuxbee/linuxbee.ini" 2>/dev/null \
    && pass node-stored || fail node-stored

# A second start finds everything there.
timeout 20 "$app" --screenshot "$work/window2.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && pass second-start || fail "second-start rc=$rc"
[ -s "$db" ] && pass library-still-there || fail library-still-there

# A library file that is not a library: say so and leave with 4, touching nothing.
printf 'this is not a database, it is a sentence that is long enough to be read as a header\n' > "$db"
before=$(sha256sum "$db" | cut -d' ' -f1)
err=$(timeout 20 "$app" --screenshot "$work/window3.png" 2>&1 > /dev/null); rc=$?
[ "$rc" = 4 ] && pass bad-library-exit || fail "bad-library-exit rc=$rc"
case "$err" in *"library cannot be opened"*) pass bad-library-message ;; *) fail "bad-library-message [$err]" ;; esac
[ "$(sha256sum "$db" | cut -d' ' -f1)" = "$before" ] && pass bad-library-kept || fail bad-library-kept
[ ! -e "$work/window3.png" ] && pass bad-library-no-window || fail bad-library-no-window

echo "cli_shell: $fails failed"
[ "$fails" = 0 ]
