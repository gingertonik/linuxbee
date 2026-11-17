#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# The program's sound is made on a thread of its own, and the program still starts and ends cleanly.
# Usage: cli_app_audio.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Rules: docs/spec/playback-shell.md (PS-MAIN), docs/spec/audio-thread.md (AT-OWN, AT-END). Nothing is played.
# Everything happens in folders of this test's own.
set -u
app="${LINUXBEE_APP:?}"
SRC=$(cd "$(dirname "$0")/../.." && pwd)
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-audio.XXXXXX") || exit 2
pid=""
trap '[ -n "$pid" ] && kill -9 "$pid" 2> /dev/null; rm -rf "$work"' EXIT
mkdir -p "$work/config" "$work/data" "$work/cache" "$work/tmp"
# env, and not a function of this shell: what is started in the background must be the program itself,
# so that its number is the program's and not a shell's.
box="XDG_CONFIG_HOME=$work/config XDG_DATA_HOME=$work/data XDG_CACHE_HOME=$work/cache TMPDIR=$work/tmp QT_QPA_PLATFORM=offscreen"

# PS-MAIN: main makes neither the sink nor the engine itself (so neither can be the window thread's);
# it hands the one maker of both to the backend that has a thread of its own.
main="$SRC/src/app/main.cpp"
code() { grep -v '^[[:space:]]*//' "$main"; }
code | grep -q 'ThreadedAudioBackend' && pass main-threaded || fail "main-threaded: main.cpp does not use ThreadedAudioBackend"
code | grep -q 'makeDeviceBackend' && pass main-maker || fail "main-maker: main.cpp does not use makeDeviceBackend"
code | grep -q 'DeviceSink\|FfmpegAudioBackend\|audio/devicesink.h\|playback/audioengine.h' \
    && fail "main-makes-none: main.cpp names the sink or the engine: $(code | grep -n 'DeviceSink\|FfmpegAudioBackend\|devicesink.h\|audioengine.h' | head -n 2 | tr '\n' ' ')" || pass main-makes-none

# AT-OWN: the running program has the audio thread.
# shellcheck disable=SC2086
env $box "$app" > /dev/null 2> "$work/stderr" &
pid=$!
n=0; while [ $n -lt 80 ] && [ -z "$(find "$work/data" -name library.db 2> /dev/null)" ]; do sleep 0.1; n=$((n + 1)); done
n=0; while [ $n -lt 50 ] && ! cat /proc/"$pid"/task/*/comm 2> /dev/null | grep -qx audio; do sleep 0.1; n=$((n + 1)); done
if kill -0 "$pid" 2> /dev/null; then pass up; else fail "up: $(head -n 2 "$work/stderr")"; fi
[ "$(cat /proc/"$pid"/comm 2> /dev/null)" = linuxbee ] && pass is-the-program || fail "is-the-program: the test started [$(cat /proc/"$pid"/comm 2> /dev/null)]"
cat /proc/"$pid"/task/*/comm 2> /dev/null | grep -qx audio && pass audio-thread \
    || fail "audio-thread: the program has no thread named audio: $(cat /proc/"$pid"/task/*/comm 2> /dev/null | sort | uniq -c | tr '\n' ' ')"
[ "$(cat /proc/"$pid"/task/*/comm 2> /dev/null | grep -cx audio)" = 1 ] && pass one-audio-thread || fail one-audio-thread

# AT-END, W-SIGNAL: told to end, it ends by itself, in time, with status 0, and says nothing of a thread left behind.
kill -TERM "$pid"
n=0; while [ $n -lt 50 ] && kill -0 "$pid" 2> /dev/null; do sleep 0.1; n=$((n + 1)); done
if kill -0 "$pid" 2> /dev/null; then fail ended-in-time; else
    wait "$pid"; rc=$?
    [ "$rc" = 0 ] && pass exit-0 || fail "exit-0: status $rc"
fi
pid=""
grep -q 'audio thread' "$work/stderr" && fail "left-behind: $(grep 'audio thread' "$work/stderr" | head -n 1)" || pass nothing-left-behind

# The starts that show no window end as cleanly, with the thread made and ended.
for flag in --commands --parts; do
    # shellcheck disable=SC2086
    env $box timeout 20 "$app" $flag > "$work/out" 2> "$work/stderr"; rc=$?
    [ "$rc" = 0 ] && [ -s "$work/out" ] && pass "clean$flag" || fail "clean$flag: status $rc"
    grep -q 'audio thread\|QThread: Destroyed' "$work/stderr" && fail "quiet$flag: $(grep 'audio thread\|QThread' "$work/stderr" | head -n 1)" || pass "quiet$flag"
done
echo "cli_app_audio: $fails failed"
[ "$fails" -eq 0 ]
