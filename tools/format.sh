#!/bin/sh
# Rewrites every file under src/ to the project style. tests/locked/ is never touched.
set -u
cd "$(dirname "$0")/.." || exit 2
find src \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 clang-format -i
