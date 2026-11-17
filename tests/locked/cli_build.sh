#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# How the tree is configured: the presets, warnings as errors, and the tests' own needs.
# Rules: docs/spec/build.md (B-PRESETS, B-WERROR, B-TESTS). It configures into a folder of
# its own and builds nothing.
# usage: cli_build.sh /path/to/linuxbeectl
set -u
CTL=${1:?usage: cli_build.sh /path/to/linuxbeectl}
DEV=$(cd "$(dirname "$CTL")/../.." && pwd)
SRC=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
no() { echo "FAIL: $1"; fail=$((fail + 1)); }
cache() { grep "^$2[:=]" "$1/CMakeCache.txt" 2> /dev/null | head -n 1 | cut -d= -f2; }
# Qt's test library made unfindable: whoever asks for it cannot configure.
NOTEST=-DCMAKE_DISABLE_FIND_PACKAGE_Qt6Test=ON

# B-PRESETS: two presets, by name.
presets=$(cd "$SRC" && cmake --list-presets 2> /dev/null)
echo "$presets" | grep -q '"dev"' || no "no dev preset"
echo "$presets" | grep -q '"release"' || no "no release preset"

# The release preset: optimised, no tests, warnings are not errors, and Qt's test library is not needed.
if (cd "$SRC" && cmake --preset release -B "$T/release" $NOTEST > "$T/release.log" 2>&1); then
    C="$T/release/compile_commands.json"
    [ "$(cache "$T/release" CMAKE_BUILD_TYPE)" = Release ] || no "release: the build type is $(cache "$T/release" CMAKE_BUILD_TYPE)"
    grep -q -- '-O[23]' "$C" || no "release: not optimised"
    grep -q -- '-DNDEBUG' "$C" || no "release: assertions are on"
    grep -q -- '-Wall' "$C" || no "release: the warnings are off"
    grep -q -- '-Werror' "$C" && no "release: a warning is an error"
    grep -q '/tests/' "$C" && no "release: the tests are built"
    grep -q 'src/app/main.cpp' "$C" || no "release: the application is not built"
    grep -q 'src/cli/' "$C" || no "release: linuxbeectl is not built"
else
    no "the release preset does not configure without Qt's test library: $(grep -i 'error' "$T/release.log" | head -n 2 | tr '\n' ' ')"
fi

# B-TESTS, B-WERROR: a plain configure, as a packager types it, is the same in both respects.
if cmake -S "$SRC" -B "$T/plain" -G Ninja $NOTEST > "$T/plain.log" 2>&1; then
    C="$T/plain/compile_commands.json"
    grep -q -- '-Werror' "$C" && no "plain: a warning is an error"
    grep -q '/tests/' "$C" && no "plain: the tests are built"
    grep -q -- '-Wall' "$C" || no "plain: the warnings are off"
else
    no "a plain configure needs Qt's test library: $(grep -i 'error' "$T/plain.log" | head -n 2 | tr '\n' ' ')"
fi

# Asked for, the tests are built, and then Qt's test library is needed.
if cmake -S "$SRC" -B "$T/asked" -G Ninja -DLINUXBEE_BUILD_TESTS=ON -DLINUXBEE_WERROR=ON > "$T/asked.log" 2>&1; then
    C="$T/asked/compile_commands.json"
    grep -q '/tests/locked/' "$C" || no "asked: the tests are not built"
    grep -q -- '-Werror' "$C" || no "asked: a warning is not an error"
else
    no "asking for the tests does not configure: $(grep -i 'error' "$T/asked.log" | head -n 2 | tr '\n' ' ')"
fi
if cmake -S "$SRC" -B "$T/asked2" -G Ninja -DLINUXBEE_BUILD_TESTS=ON $NOTEST > /dev/null 2>&1; then
    no "the tests were configured without Qt's test library"
fi

# The dev preset, which built this very test: the tests, a debug build, and warnings as errors.
C="$DEV/compile_commands.json"
[ "$(cache "$DEV" CMAKE_BUILD_TYPE)" = Debug ] || no "dev: the build type is $(cache "$DEV" CMAKE_BUILD_TYPE)"
grep -q -- '-Werror' "$C" || no "dev: a warning is not an error"
grep -q '/tests/locked/' "$C" || no "dev: the tests are not built"

echo "cli_build: $fail failed"
[ "$fail" -eq 0 ]
