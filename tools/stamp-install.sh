#!/bin/sh
# LOCKED. Written by the reviewer. docs/spec/install.md, I-STAMP.
# usage: stamp-install.sh <tag> <commit>  < install.sh  > the release copy
set -eu
tag=${1:?usage: stamp-install.sh <tag> <commit>}
commit=${2:?usage: stamp-install.sh <tag> <commit>}
printf '%s\n' "$tag" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9.]+)?$' || { echo "stamp-install: not a release tag: $tag" >&2; exit 1; }
printf '%s\n' "$commit" | grep -Eq '^[0-9a-f]{40}$' || { echo "stamp-install: not a full commit: $commit" >&2; exit 1; }
t=$(mktemp)
trap 'rm -f "$t"' EXIT
cat > "$t"
[ "$(grep -c '^TAG=unstamped$' "$t")" = 1 ] && [ "$(grep -c '^COMMIT=unstamped$' "$t")" = 1 ] \
    || { echo "stamp-install: the script must have one line TAG=unstamped and one line COMMIT=unstamped" >&2; exit 1; }
sed "s/^TAG=unstamped\$/TAG=$tag/; s/^COMMIT=unstamped\$/COMMIT=$commit/" "$t"
