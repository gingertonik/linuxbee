#!/bin/sh
# LOCKED. Written by the reviewer. docs/spec/release.md.
# Run from the root of the tree:
#   release.sh check <tag>             the tag is the version, and the changelog has it (R-CHECK)
#   release.sh notes <tag>             the release's notes, on the output
#   release.sh publish <tag> <commit>  stamp the installer and make the GitHub Release (R-PUBLISH)
set -eu
step=${1:?usage: release.sh check|notes|publish <tag> [<commit>]}
tag=${2:?usage: release.sh check|notes|publish <tag> [<commit>]}
die() { echo "release.sh: $*" >&2; exit 1; }
printf '%s\n' "$tag" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9.]+)?$' || die "not a release tag: $tag"
number=${tag#v}
number=${number%%-*}
early=no
[ "$number" = "${tag#v}" ] || early=yes # v1.0.0-rc1: a pre-release
section() { # the lines under the heading "## $1", to the next heading
    awk -v h="## $1" 'index($0, h) == 1 && (length($0) == length(h) || substr($0, length(h) + 1, 1) == " ") { f = 1; next } f && /^## / { exit } f' CHANGELOG.md
}
has() { section "$1" | grep -q '[^[:space:]]'; }

check() {
    version=$(sed -n 's/^project(linuxbee VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
    [ "$version" = "$number" ] || die "the tag is $tag but CMakeLists.txt says $version"
    if [ "$early" = no ]; then
        has "$number" || die "CHANGELOG.md has nothing under '## $number'"
        ! has Unreleased || die "CHANGELOG.md still has lines under '## Unreleased'; run tools/bump-version.sh"
    else
        has "$number" || has Unreleased || die "CHANGELOG.md has nothing for $number"
    fi
}

notes() {
    case "$number" in 0.*) printf 'Before 1.0, for early testing: commands, settings and the library file may still change.\n\n' ;; esac
    if has "$number"; then section "$number"; else section Unreleased; fi
}

case "$step" in
check) check ;;
notes) check && notes ;;
publish)
    commit=${3:?usage: release.sh publish <tag> <commit>}
    check
    # R-PUBLISH, "the commit": what is published is what is checked out here (and was tested here), and
    # what the tag names where it was pushed. An annotated tag is listed twice, its commit last.
    head=$(git rev-parse HEAD 2> /dev/null) || die "this is not a git checkout"
    [ "$head" = "$commit" ] || die "the commit given is $commit but $head is checked out"
    named=$(git ls-remote origin "refs/tags/$tag" "refs/tags/$tag^{}" 2> /dev/null | awk '{ c = $1 } END { print c }')
    [ -n "$named" ] || die "origin has no tag $tag"
    [ "$named" = "$commit" ] || die "at origin the tag $tag names $named, not $commit"
    out=$(mktemp -d)
    trap 'rm -rf "$out"' EXIT
    sh tools/stamp-install.sh "$tag" "$commit" < install.sh > "$out/install.sh"
    (cd "$out" && sha256sum install.sh > install.sh.sha256)
    notes > "$out/notes.md"
    if [ "$early" = yes ]; then kind="--prerelease --latest=false"; else kind="--latest"; fi
    # shellcheck disable=SC2086
    gh release create "$tag" "$out/install.sh" "$out/install.sh.sha256" --verify-tag --title "linuxBee ${tag#v}" --notes-file "$out/notes.md" $kind
    ;;
*) die "no such step: $step" ;;
esac
