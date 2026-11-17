#!/bin/sh
# LOCKED. Written by the reviewer. docs/spec/release.md, R-BUMP.
# usage: bump-version.sh <X.Y.Z> [<YYYY-MM-DD>]   from the root of the tree; then commit, then tag vX.Y.Z
set -eu
new=${1:?usage: bump-version.sh <X.Y.Z> [<YYYY-MM-DD>]}
day=${2:-$(date -u +%F)}
die() { echo "bump-version.sh: $*" >&2; exit 1; }
printf '%s\n' "$new" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || die "not a version: $new"
printf '%s\n' "$day" | grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2}$' || die "not a date: $day"
old=$(sed -n 's/^project(linuxbee VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$old" ] || die "no version in CMakeLists.txt"
[ "$old" != "$new" ] && [ "$(printf '%s\n%s\n' "$old" "$new" | sort -V | tail -n 1)" = "$new" ] || die "$new is not after $old"
[ "$(grep -c '^## Unreleased$' CHANGELOG.md)" = 1 ] || die "CHANGELOG.md has no single '## Unreleased' heading"
awk '/^## Unreleased$/ { f = 1; next } f && /^## / { exit } f' CHANGELOG.md | grep -q '[^[:space:]]' || die "nothing under '## Unreleased': nothing to release"
grep -q "^## $new\b" CHANGELOG.md && die "CHANGELOG.md already has $new"
old_re=$(printf '%s' "$old" | sed 's/\./\\./g')
sed -i "s/^project(linuxbee VERSION $old_re /project(linuxbee VERSION $new /" CMakeLists.txt
[ ! -f tests/tst_version.cpp ] || sed -i "s/$old_re/$new/g" tests/tst_version.cpp
sed -i "s/^## Unreleased\$/## Unreleased\n\n## $new ($day)/" CHANGELOG.md
echo "$old -> $new ($day): CMakeLists.txt, CHANGELOG.md$([ -f tests/tst_version.cpp ] && echo ', tests/tst_version.cpp')"
