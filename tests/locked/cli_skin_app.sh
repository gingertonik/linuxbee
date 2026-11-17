#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_skin_app.sh /path/to/linuxbeectl     (LINUXBEE_TEST_DATA must be set; LINUXBEE_APP is the application)
# Expected values: docs/spec/skins.md, SK-MAIN.
set -u
data="${LINUXBEE_TEST_DATA:?}/skins"
app="${LINUXBEE_APP:?}"
fails=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-bus"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() {
    label=$1; expected=$2; got=$3
    if [ "$got" = "$expected" ]; then pass "$label"; else fail "$label: got [$got] want [$expected]"; fi
}

# SK-MAIN
out=$(timeout 8 "$app" --parts 2> /dev/null); rc=$?
is "parts: exit" 0 "$rc"
is "parts: no skin" "skin=none" "$(printf '%s\n' "$out" | grep '^skin=')"
[ "$(printf '%s\n' "$out" | grep -x -A 1 'main-view=albums' | tail -n 1)" = 'skin=none' ] && pass "parts: after the earlier lines" || fail "parts: after the earlier lines [$out]"
ini="$work/config/linuxbee/linuxbee.ini"
mkdir -p "$(dirname "$ini")"
printf '\n[appearance]\nskin=%s\n' "$data/Red Accent.xml" >> "$ini"
is "parts: the stored skin" "skin=Red Accent.xml" "$(timeout 8 "$app" --parts 2> /dev/null | grep '^skin=')"
timeout 20 "$app" --screenshot "$work/skinned.png" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && [ -s "$work/skinned.png" ] && pass "picture with a skin" || fail "picture with a skin rc=$rc"
sed -i "s|^skin=.*|skin=$work/gone.xml|" "$ini"
is "parts: a stored skin that is gone" "skin=none" "$(timeout 8 "$app" --parts 2> /dev/null | grep '^skin=')"

echo "cli_skin_app: $fails failed"
[ "$fails" = 0 ]
