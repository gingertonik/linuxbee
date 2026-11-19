#!/bin/sh
# Checks that need no compiler. Prints nothing and exits 0 when the tree is clean.
set -u
cd "$(dirname "$0")/.." || exit 2
bad=0

# 1. Locked files are byte-identical to what the reviewer committed.
#    tools/locked.sha256 covers the gate itself. Each plan adds its own list,
#    tools/locked/<plan>.sha256, so two plans never edit the same file.
for list in tools/locked.sha256 tools/locked/*.sha256; do
    [ -f "$list" ] || continue
    if ! sha256sum --quiet -c "$list"; then
        echo "lint: a locked file listed in $list was changed. Restore it: git checkout -- <file>"
        bad=1
    fi
done

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

# 5. Build files find sources by pattern. Naming a source file in one of them
#    means two plans edit the same line.
hits=$(grep -nE '[A-Za-z0-9_]\.(cpp|h|sh|qrc)\b' src/core/CMakeLists.txt src/cli/CMakeLists.txt \
    src/ui/CMakeLists.txt src/app/CMakeLists.txt tests/CMakeLists.txt 2>/dev/null | grep -vE '^[^:]+:[0-9]+:[[:space:]]*#' || true)
if [ -n "$hits" ]; then
    echo "lint: build files must not name source files (they are found by pattern):"
    echo "$hits"
    bad=1
fi

# 6. linuxbeectl's main.cpp knows no command. Commands register themselves.
if [ -f src/cli/command.h ]; then
    hits=$(grep -n '#include "' src/cli/main.cpp | grep -vE '"(command|version)\.h"' || true)
    if [ -n "$hits" ]; then
        echo "lint: src/cli/main.cpp may include only command.h and version.h:"
        echo "$hits"
        bad=1
    fi
fi

# 7. Dependencies point one way: app -> ui -> core. The core and the command
#    line tool never include a ui header, and the ui never includes the app.
if [ -d src/ui ]; then
    hits=""
    for header in $(find src/ui -name '*.h' | sed 's|^src/ui/||'); do
        found=$(grep -rnF "#include \"$header\"" src/core src/cli 2>/dev/null || true)
        [ -n "$found" ] && hits="$hits$found
"
    done
    if [ -n "$hits" ]; then
        echo "lint: src/core and src/cli must not include headers from src/ui:"
        printf '%s' "$hits"
        bad=1
    fi
    # The ui never includes the app.
    hits=""
    for header in $(find src/app -name '*.h' 2>/dev/null | sed 's|^src/app/||'); do
        found=$(grep -rnF "#include \"$header\"" src/ui src/core src/cli 2>/dev/null || true)
        [ -n "$found" ] && hits="$hits$found
"
    done
    if [ -n "$hits" ]; then
        echo "lint: only src/app may include headers from src/app:"
        printf '%s' "$hits"
        bad=1
    fi
    # A path that climbs out of its directory would get round both checks.
    hits=$(grep -rnE '#[[:space:]]*include[[:space:]]*["<][^">]*\.\./' src tests 2>/dev/null || true)
    if [ -n "$hits" ]; then
        echo "lint: an include may not contain ../ (name the header from its include root):"
        echo "$hits"
        bad=1
    fi
    hits=$(grep -rnE '#include <Qt(Widgets|Gui)|#include <Q(Widget|Application|MainWindow|Dialog)>' src/cli || true)
    if [ -n "$hits" ]; then
        echo "lint: src/cli must not include QtWidgets or QtGui:"
        echo "$hits"
        bad=1
    fi
fi

# 8. PlaylistStore::setFileOpener is there for the locked tests, which hand the store a device
#    whose reads fail. The program gives none: with one, a playlist could be read from elsewhere.
hits=$(grep -rn 'setFileOpener' src tests 2>/dev/null \
    | grep -vE '^(src/core/playlists/playliststore\.(h|cpp)|tests/locked/[^:]*):' || true)
if [ -n "$hits" ]; then
    echo "lint: setFileOpener may be named only in src/core/playlists/playliststore.{h,cpp} and tests/locked:"
    echo "$hits"
    bad=1
fi

exit $bad
