#!/bin/sh
# LOCKED. Written by the reviewer. docs/spec/ci.md.
# What each CI job runs, one step a call, so that the same can be run by hand in the same container:
#   as root:  sh tools/ci.sh packages && sh tools/ci.sh user
#   as ci:    sh tools/ci.sh gate | sanitize | release
set -eu
cd "$(dirname "$0")/.."
step=${1:?usage: ci.sh packages|user|gate|sanitize|release}
jobs=${CI_JOBS:-$(nproc)}

throwaway() { # these two steps change the system they run on
    [ "${CI:-}" = true ] || { echo "ci.sh: '$step' is for a throwaway container only. Say so with CI=true." >&2; exit 2; }
}

case "$step" in
packages) # C-CONTAINER: everything a build and the tests need, from the distribution, and nothing else
    throwaway
    # ttf-dejavu: the image has no font at all, and the window's layout is measured in two tests.
    pacman -Syu --noconfirm --needed git cmake ninja gcc clang pkgconf qt6-base qt6-multimedia qt6-svg taglib \
        ffmpeg sqlite dbus desktop-file-utils appstream shellcheck ttf-dejavu
    ;;
user) # C-USER: the tests run as an ordinary user; some of them rely on a file they may not read
    throwaway
    id ci > /dev/null 2>&1 || useradd -m ci
    chown -R ci: .
    ;;
gate) # C-GATE: the one gate, as a builder runs it
    [ "$(id -u)" != 0 ] || { echo "ci.sh: the gate does not run as root" >&2; exit 2; }
    CMAKE_BUILD_PARALLEL_LEVEL=$jobs dbus-run-session -- sh tools/check.sh
    ;;
sanitize) # C-SANITIZE: every test again, built with the address and undefined-behaviour sanitizers
    [ "$(id -u)" != 0 ] || { echo "ci.sh: the tests do not run as root" >&2; exit 2; }
    flags="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g1"
    mkdir -p build
    # The dev preset with the sanitizers added: the same options the gate builds with (cli_build looks).
    cmake --preset dev -B build/asan \
        "-DCMAKE_CXX_FLAGS=$flags" "-DCMAKE_EXE_LINKER_FLAGS=$flags" "-DCMAKE_SHARED_LINKER_FLAGS=$flags" > build/asan-configure.log 2>&1 \
        || { tail -n 20 build/asan-configure.log; exit 1; }
    ninja -j "$jobs" -C build/asan > build/asan-build.log 2>&1 || { grep -E 'error|FAILED' build/asan-build.log | head -n 40; exit 1; }
    box=$(mktemp -d "$PWD/build/asan-home.XXXXXX")
    mkdir -p "$box/data" "$box/config" "$box/cache"
    rc=0
    # Leaks are not looked for: Qt's own at exit drown the program's. (ci.md, "Not in this version".)
    (cd build/asan && XDG_DATA_HOME="$box/data" XDG_CONFIG_HOME="$box/config" XDG_CACHE_HOME="$box/cache" QT_QPA_PLATFORM=offscreen \
        ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:strict_string_checks=1:detect_stack_use_after_return=1 \
        UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
        dbus-run-session -- ctest --timeout 900 --output-on-failure > ../asan-ctest.log 2>&1) || rc=$?
    rm -rf "$box"
    grep -E 'tests passed|\*\*\*Failed|Not Run|Timeout' build/asan-ctest.log || true
    if grep -qE 'ERROR: AddressSanitizer|runtime error:' build/asan-ctest.log; then
        grep -E -A 12 'ERROR: AddressSanitizer|runtime error:' build/asan-ctest.log | head -n 80
        rc=1
    fi
    echo "rc=$rc"
    exit "$rc"
    ;;
release) # C-RELEASE: the build that is installed builds, with no test library, and runs
    [ "$(id -u)" != 0 ] || { echo "ci.sh: the build does not run as root" >&2; exit 2; }
    cmake --preset release > build-release-configure.log 2>&1 || { tail -n 20 build-release-configure.log; exit 1; }
    rm -f build-release-configure.log
    cmake --build --preset release -j "$jobs"
    build/release/src/cli/linuxbeectl --version
    QT_QPA_PLATFORM=offscreen build/release/src/app/linuxbee --version
    ;;
*)
    echo "ci.sh: no such step: $step" >&2
    exit 2
    ;;
esac
