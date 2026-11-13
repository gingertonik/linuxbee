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
ctest --preset dev > build/ctest.log 2>&1
rc=$?
grep -E "tests passed|\*\*\*Failed|Not Run|No tests" build/ctest.log
echo "rc=$rc"
exit $rc
