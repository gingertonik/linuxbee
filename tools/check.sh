#!/bin/sh
# The one gate: tree lint, format check, configure, build, every test.
# The last line printed is always rc=N. Only rc=0 is a pass.
set -u
cd "$(dirname "$0")/.." || exit 2
mkdir -p build

if ! sh tools/lint-tree.sh; then
    echo "rc=1 (tree lint failed)"
    exit 1
fi
if ! find src \( -name '*.cpp' -o -name '*.h' \) -print0 \
        | xargs -0 clang-format --dry-run -Werror > build/format.log 2>&1; then
    grep -E "error:" build/format.log | head -n 20
    echo "rc=1 (format check failed; run: sh tools/format.sh)"
    exit 1
fi
if ! cmake --preset dev > build/configure.log 2>&1; then
    tail -n 20 build/configure.log
    echo "rc=1 (configure failed)"
    exit 1
fi
# A header that gains or loses Q_OBJECT between two builds of the same build
# directory (a merge, a branch switch) is not always noticed by the moc step.
# Removing its stamp makes that step look again; it costs well under a second.
find build/dev -path '*_autogen/timestamp' -delete 2> /dev/null
if ! nice -n 10 cmake --build --preset dev > build/build.log 2>&1; then
    grep -E "error|FAILED" build/build.log | grep -v "^/usr/bin/" | head -n 40
    echo "rc=1 (build failed)"
    exit 1
fi
# No test may reach the user's own folders. Every test runs with data, settings and
# cache folders made for this run (the desktop's trash is in the data folder), and
# the run fails if the user's trash grew meanwhile.
real_trash="${XDG_DATA_HOME:-$HOME/.local/share}/Trash/files"
trash_before=$(ls -A "$real_trash" 2> /dev/null | wc -l)
LINUXBEE_GATE_HOME=$(mktemp -d "$PWD/build/gate-home.XXXXXX") || { echo "rc=1 (no folder for the tests)"; exit 1; }
export LINUXBEE_GATE_HOME
mkdir -p "$LINUXBEE_GATE_HOME/data" "$LINUXBEE_GATE_HOME/config" "$LINUXBEE_GATE_HOME/cache"
XDG_DATA_HOME="$LINUXBEE_GATE_HOME/data" XDG_CONFIG_HOME="$LINUXBEE_GATE_HOME/config" \
    XDG_CACHE_HOME="$LINUXBEE_GATE_HOME/cache" ctest --preset dev > build/ctest.log 2>&1
rc=$?
# A test that sends a file to the trash names a trash of its own. One that left it to
# the run's folder is named here, and fails the run.
strays=$(ls -A "$LINUXBEE_GATE_HOME/data/Trash/files" 2> /dev/null | head -n 5 | tr '\n' ' ')
rm -rf "$LINUXBEE_GATE_HOME"
trash_after=$(ls -A "$real_trash" 2> /dev/null | wc -l)
if [ "$trash_after" -gt "$trash_before" ]; then
    grep -E "tests passed|\*\*\*Failed|Not Run|No tests" build/ctest.log
    echo "rc=1 (the user's trash grew during the tests: $trash_before to $trash_after entries in $real_trash)"
    exit 1
fi
if [ -n "$strays" ]; then
    grep -E "tests passed|\*\*\*Failed|Not Run|No tests" build/ctest.log
    echo "rc=1 (a test sent files to a trash it did not name: $strays)"
    exit 1
fi
grep -E "tests passed|\*\*\*Failed|Not Run|No tests" build/ctest.log
echo "rc=$rc"
exit $rc
