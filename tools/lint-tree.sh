#!/bin/sh
# Checks that need no compiler. Prints nothing and exits 0 when the tree is clean.
set -u
cd "$(dirname "$0")/.." || exit 2
bad=0

# 1. Locked files are byte-identical to what the reviewer committed.
if [ -f tools/locked.sha256 ]; then
    if ! sha256sum --quiet -c tools/locked.sha256; then
        echo "lint: a locked file was changed. Restore it: git checkout -- <file>"
        bad=1
    fi
fi

# 2. This tree is published. No home paths, user names or machine names.
#    tools/private-words.txt is git-ignored: one word per line, kept only on
#    the machine doing the work, so the words themselves are never published.
pattern='/home/|/Users/'
if [ -f tools/private-words.txt ]; then
    words=$(grep -v '^#' tools/private-words.txt | grep -v '^$' | paste -sd'|' -)
    [ -n "$words" ] && pattern="$pattern|\\b($words)\\b"
fi
hits=$(grep -rnEi "$pattern" . \
    --exclude-dir=build --exclude-dir=.git \
    --exclude=LICENSE --exclude=lint-tree.sh --exclude=private-words.txt || true)
if [ -n "$hits" ]; then
    echo "lint: private path or name in the tree:"
    echo "$hits"
    bad=1
fi

# 3. The core library never depends on the GUI modules.
hits=$(grep -rnE '#include <Qt(Widgets|Gui)|#include <Q(Widget|Application|MainWindow|Dialog)>' src/core || true)
if [ -n "$hits" ]; then
    echo "lint: src/core must not include QtWidgets or QtGui:"
    echo "$hits"
    bad=1
fi

exit $bad
