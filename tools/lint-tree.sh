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
#    In a git worktree .git is a file that holds an absolute path, so it is
#    excluded by name as well as by directory.
#    tools/private-words.txt is git-ignored: one word per line, kept only on
#    the machine doing the work, so the words themselves are never published.
pattern='/home/|/Users/'
if [ -f tools/private-words.txt ]; then
    words=$(grep -v '^#' tools/private-words.txt | grep -v '^$' | paste -sd'|' -)
    [ -n "$words" ] && pattern="$pattern|\\b($words)\\b"
fi
hits=$(grep -rnEi "$pattern" . \
    --exclude-dir=build --exclude-dir=.git \
    --exclude=LICENSE --exclude=lint-tree.sh --exclude=private-words.txt \
    --exclude=.git || true)
if [ -n "$hits" ]; then
    echo "lint: private path or name in the tree:"
    echo "$hits"
    bad=1
fi

# 3. This tree holds the project only. Plans, run reports and agent session
#    files are kept outside it.
hits=$(find . \( -path ./build -o -path ./.git \) -prune -o \( \
    -iname 'PLAN*.md' -o -iname '*EXECUTE-PROMPT*' -o -iname 'REPORT*.md' \
    -o -iname 'RESUME*' -o -name '.hermes' -o -name '.worknotes' \
    -o -path './plans' -o -path './docs/superpowers' \) -print)
if [ -n "$hits" ]; then
    echo "lint: planning or session files do not belong in this tree:"
    echo "$hits"
    bad=1
fi
hits=$(grep -rnE '\.hermes/plans|hermes chat' . \
    --exclude-dir=build --exclude-dir=.git --exclude=.git \
    --exclude=lint-tree.sh || true)
if [ -n "$hits" ]; then
    echo "lint: reference to a plan file outside the tree:"
    echo "$hits"
    bad=1
fi

# 4. The core library never depends on the GUI modules.
hits=$(grep -rnE '#include <Qt(Widgets|Gui)|#include <Q(Widget|Application|MainWindow|Dialog)>' src/core || true)
if [ -n "$hits" ]; then
    echo "lint: src/core must not include QtWidgets or QtGui:"
    echo "$hits"
    bad=1
fi

exit $bad
