#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_settings_running.sh /path/to/linuxbeectl   (LINUXBEE_APP is the application; LINUXBEE_TEST_DATA must be set)
# docs/spec/settings-import.md, SI-CLI-RUNNING: the settings file of a profile that a running linuxBee
# holds is not written. The fault (the write-path audit, part 8, F3): the import said "11 written", and
# when the program was closed it saved its own hotkeys over them.
# Runs with no display and no message bus, in folders of its own.
set -u
case "$1" in /*) bin="$1" ;; *) bin="$(pwd)/$1" ;; esac
app="${LINUXBEE_APP:?}"
fixture="${LINUXBEE_TEST_DATA:?}/settings/MusicBee3Settings.ini"
fails=0
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-setrun.XXXXXX") || exit 2
pid=
trap '[ -n "$pid" ] && kill -9 "$pid" 2> /dev/null; rm -rf "$work"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-bus"
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
ini="$work/config/linuxbee/linuxbee.ini"
lock="$work/data/linuxbee/profile.lock"
RUNNING="linuxBee is running on this profile; close it and import again"
has() { grep -q '^edit.editField=F2$' "$1" 2> /dev/null; }
start() {
    # Not under `timeout`: the number kept must be the program's own, for the kill below.
    "$app" > /dev/null 2>&1 &
    pid=$!
    n=0
    while [ ! -e "$lock" ] || [ ! -e "$ini" ]; do
        n=$((n + 1)); [ "$n" -gt 150 ] && break
        kill -0 "$pid" 2> /dev/null || break
        sleep 0.1
    done
    sleep 0.5
}
stop() { kill -TERM "$pid" 2> /dev/null; wait "$pid" 2> /dev/null; pid=; }

# One run first, so that the profile and its settings file are there.
timeout 30 "$app" --screenshot "$work/first.png" > /dev/null 2>&1 || fail "setup: the program did not run"
[ -e "$ini" ] && pass "setup: the settings file is there" || fail "setup: no settings file"

start
kill -0 "$pid" 2> /dev/null && [ -e "$lock" ] && pass "setup: the program is running and holds the profile" || fail "setup: the program is not running"

# refused LABEL TO
refused() {
    before=$(cksum < "$ini")
    said=$(timeout 10 "$bin" settings import --apply --to "$2" "$fixture" 2> "$work/err"); rc=$?
    after=$(cksum < "$ini")
    [ "$rc" = 3 ] && pass "$1: exit 3" || fail "$1: rc $rc"
    [ -z "$said" ] && pass "$1: nothing on stdout" || fail "$1: stdout [$(printf '%s' "$said" | tail -n 1)]"
    [ "$(cat "$work/err")" = "failed: $2: $RUNNING" ] && pass "$1: says why" || fail "$1: stderr [$(cat "$work/err")]"
    [ "$before" = "$after" ] && pass "$1: the settings file is as it was" || fail "$1: the settings file was written"
}
refused "while it runs" "$ini"
refused "while it runs, spelled another way" "$work/config/linuxbee/../linuxbee/linuxbee.ini"
ln -s "$work/config" "$work/cfg"
refused "while it runs, through a linked folder" "$work/cfg/linuxbee/linuxbee.ini"

# Looking is allowed, and so is writing a file that is not the profile's.
said=$(timeout 10 "$bin" settings import "$fixture" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && [ -n "$said" ] && pass "while it runs: the plan is still shown" || fail "while it runs: the plan: rc $rc"
timeout 10 "$bin" settings import --apply --to "$work/other.ini" "$fixture" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && has "$work/other.ini" && pass "while it runs: another file is written" || fail "while it runs: another file: rc $rc"

stop
has "$ini" && fail "after it was closed: the import is in the file though it was refused" || pass "after it was closed: nothing of the import is in the file"

# With the program closed the import is written.
said=$(timeout 10 "$bin" settings import --apply --to "$ini" "$fixture" 2> /dev/null); rc=$?
[ "$rc" = 0 ] && has "$ini" && pass "closed: written" || fail "closed: rc $rc"

# A program that was killed does not hold the profile.
sed -i '/^edit.editField=/d' "$ini"
start
kill -0 "$pid" 2> /dev/null && [ -e "$lock" ] && pass "after a kill: it was running" || fail "after a kill: it was not running"
kill -9 "$pid" 2> /dev/null; wait "$pid" 2> /dev/null; pid=
[ -e "$lock" ] && pass "after a kill: the lock file is still there" || fail "after a kill: no lock file was left to judge"
timeout 10 "$bin" settings import --apply --to "$ini" "$fixture" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && has "$ini" && pass "after a kill: written" || fail "after a kill: rc $rc"

# A profile that was never used: there is nothing to hold, and no data folder is made for the asking.
rm -rf "$work/data" "$work/config"
timeout 10 "$bin" settings import --apply --to "$ini" "$fixture" > /dev/null 2>&1; rc=$?
[ "$rc" = 0 ] && has "$ini" && pass "a new profile: written" || fail "a new profile: rc $rc"
[ ! -e "$work/data" ] && pass "a new profile: no data folder was made" || fail "a new profile: a data folder was made"

echo "cli_settings_running: $fails failed"
[ "$fails" = 0 ]
