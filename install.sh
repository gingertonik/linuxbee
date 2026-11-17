#!/bin/sh
# install.sh: builds linuxBee from one tagged release's source and installs it for one user.
# 1. Asks the repository which commit the tag is at and refuses unless it is the commit
#    stamped into this file; then looks for the build tools (it installs none).
# 2. Clones the tag into a temporary folder, checks the commit again, builds the release
#    preset there, installs into a folder there, and copies from it into $XDG_BIN_HOME
#    (default ~/.local/bin) and $XDG_DATA_HOME (default ~/.local/share), with its list in
#    linuxbee-installer/manifest. Every action is printed after "+ ".
# It refuses to run as root, never runs sudo, su, doas or pkexec, and never writes under
# linuxbee/ in your data, settings or cache folders. --uninstall removes only listed files.
set -eu

TAG=unstamped
COMMIT=unstamped

say() { printf '%s\n' "$*"; }
die() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }
act() { printf '+ %s\n' "$*"; "$@"; }
usage() {
    say "usage: install.sh [--dry-run | --uninstall | --help]"
    say "  --dry-run    make every check, print what would be done, change nothing"
    say "  --uninstall  remove what an earlier install put in place (needs no network or tools)"
    say "environment: LINUXBEE_REPO (clone this repository instead of the project's own),"
    say "  LINUXBEE_OS_RELEASE (read this file instead of /etc/os-release), XDG_BIN_HOME, XDG_DATA_HOME."
}
# Sets bin, data, manifest and their physical forms; both must be absolute paths.
roots() {
    : "${HOME:?HOME is not set}"
    bin=${XDG_BIN_HOME:-$HOME/.local/bin}
    data=${XDG_DATA_HOME:-$HOME/.local/share}
    case $bin in /*) ;; *) die "the programs' folder is not an absolute path: $bin" ;; esac
    case $data in /*) ;; *) die "the data folder is not an absolute path: $data" ;; esac
    case "$bin/$data/" in */../* | */./*) die "a folder spelled with .. or /./ is refused: $bin, $data" ;; esac
    manifest=$data/linuxbee-installer/manifest
    pbin=$(cd "$bin" 2> /dev/null && pwd -P) || pbin=$bin
    pdata=$(cd "$data" 2> /dev/null && pwd -P) || pdata=$data
}
# allowed PATH BIN DATA: PATH is below BIN or DATA, has no .. and is not under DATA/linuxbee.
allowed() {
    case /$1/ in */../*) return 1 ;; esac
    case $1 in "$3"/linuxbee | "$3"/linuxbee/*) return 1 ;; "$2"/* | "$3"/*) return 0 ;; esac
    return 1
}
# honoured PATH: a line of the list that may be acted on, in its written form and as the
# real folder it sits in (a link must not lead out of the two roots).
honoured() {
    if [ -d "$1" ] && [ ! -L "$1" ]; then return 1; fi
    allowed "$1" "$bin" "$data" || return 1
    pdir=$(cd "$(dirname -- "$1")" 2> /dev/null && pwd -P) || return 0
    allowed "$pdir/x" "$pbin" "$pdata"
}
# prune DIR: remove DIR and its parents while they are empty and below a root.
prune() {
    d=$1
    while allowed "$d" "$bin" "$data" && rmdir "$d" 2> /dev/null; do d=$(dirname -- "$d"); done
}
remove_listed() {
    if ! honoured "$1"; then say "left alone, not a file this script may remove: '$1'"; return 0; fi
    if [ -e "$1" ] || [ -L "$1" ]; then act rm -f -- "$1" || die "cannot remove $1; the list is kept, so run --uninstall again"; prune "$(dirname -- "$1")"; fi
}
uninstall() {
    if [ ! -f "$manifest" ]; then say "Nothing was installed by this script ($manifest is not there)."; return 0; fi
    while IFS= read -r line || [ -n "$line" ]; do remove_listed "$line"; done < "$manifest"
    act rm -f -- "$manifest" "$manifest.lbnew"
    prune "$(dirname -- "$manifest")"
    say "Removed. Your library, settings, playlists and music were not touched."
}
# version_ok HAVE WANT: HAVE is at least WANT (major.minor).
version_ok() {
    [ -n "$1" ] || return 1
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -t. -k1,1n -k2,2n | sed -n 1p)" = "$2" ]
}
lack() { missing="$missing$1;"; case "$pkgs " in *" $2 "*) ;; *) pkgs="$pkgs $2" ;; esac; }
is_arch() {
    [ -r "$osrel" ] || die "cannot read $osrel, so the system is not known"
    case " $(sed -n 's/^ID=//p;s/^ID_LIKE=//p' "$osrel" | tr -d "\"'" | tr '\n' ' ') " in *" arch "*) return 0 ;; esac
    return 1
}
check_needs() {
    missing=; pkgs=
    for t in git:git ninja:ninja pkg-config:pkgconf ffmpeg:ffmpeg; do
        if ! command -v "${t%%:*}" > /dev/null 2>&1; then lack "${t%%:*}" "${t#*:}"; fi
    done
    v=; if command -v cmake > /dev/null 2>&1; then v=$(cmake --version | sed -n '1s/[^0-9]*\([0-9][0-9.]*\).*/\1/p'); fi
    if ! version_ok "$v" 3.25; then lack "CMake 3.25 or later" cmake; fi
    if ! command -v c++ > /dev/null 2>&1 || ! printf 'consteval int f() { return 0; }\nint main() { return f(); }\n' | c++ -std=c++20 -fsyntax-only -x c++ - > /dev/null 2>&1; then lack "a C++20 compiler" gcc; fi
    if command -v pkg-config > /dev/null 2>&1; then
        v=$(pkg-config --modversion Qt6Widgets 2> /dev/null || true)
        if ! version_ok "$v" 6.5; then lack "Qt 6.5 or later" qt6-base; fi
        for q in Multimedia:qt6-multimedia Sql:qt6-base DBus:qt6-base; do
            if ! pkg-config --exists "Qt6${q%%:*}" 2> /dev/null; then lack "Qt 6 ${q%%:*}" "${q#*:}"; fi
        done
        v=$(pkg-config --modversion taglib 2> /dev/null || true)
        if ! version_ok "$v" 2.0; then lack "TagLib 2.0 or later" taglib; fi
    fi
    if [ -n "$missing" ]; then say "Missing, and needed to build linuxBee:"; printf '%s\n' "$missing" | tr ';' '\n' | sed '/^$/d;s/^/  /'; fi
    if ! is_arch; then die "this script installs on Arch-family systems only; other systems name these packages differently"; fi
    if [ -n "$missing" ]; then say "Install them with this line (this script does not run it), then run it again:"; say "  pacman -Syu --needed$pkgs"; exit 1; fi
}
tag_commit() {
    out=$(git ls-remote -- "$repo" "refs/tags/$TAG" "refs/tags/$TAG^{}") || die "cannot ask $repo for the commit of $TAG"
    printf '%s\n' "$out" | awk -v a="refs/tags/$TAG^{}" -v b="refs/tags/$TAG" '$2==a{x=$1} $2==b{y=$1} END{print (x!=""?x:y)}'
}
fetch() {
    work=$(mktemp -d)
    say "+ git clone --depth 1 --branch $TAG $repo $work/src"
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$TAG" -- "$repo" "$work/src"
    got=$(git -C "$work/src" rev-parse HEAD)
    if [ "$got" != "$COMMIT" ]; then die "the clone is at $got, not at the commit wanted, $COMMIT; nothing was installed"; fi
}
build() {
    cd "$work/src"
    say "+ cmake --preset release"; cmake --preset release > /dev/null
    act cmake --build --preset release
    act cmake --install build/release --prefix "$work/stage"
}
# keep FILE: make FILE the list, through a temporary name that is cleared if the copy fails.
keep() {
    act mkdir -p "$(dirname -- "$manifest")"
    act cp "$1" "$manifest.lbnew" || { rm -f -- "$manifest.lbnew"; die "cannot write the list $manifest"; }
    act mv -f "$manifest.lbnew" "$manifest"
}
# place: pair every staged file with its target, refuse any that is in the way, write the list
# (every target and its temporary name, with the earlier list), copy, remove what an earlier
# install had and this one has not, then rewrite the list as exactly what is in place.
place() {
    tab=$(printf '\t'); stage=$work/stage
    (cd "$stage" && find . ! -type d) | sed 's|^\./||' | sort > "$work/rel"
    : > "$work/pairs"; clash=0
    while IFS= read -r rel; do
        case $rel in
            bin/*) dst=$bin/${rel#bin/} ;;
            share/linuxbee | share/linuxbee/*) die "the build installs $rel, under linuxbee/ in the data folder, which this script never writes" ;;
            share/*) dst=$data/${rel#share/} ;;
            *) die "the build installs $rel, outside bin and share" ;;
        esac
        if [ ! -f "$stage/$rel" ] || [ -L "$stage/$rel" ]; then die "not a plain file: $rel"; fi
        printf '%s\t%s\n' "$rel" "$dst" >> "$work/pairs"
    done < "$work/rel"
    cut -f2 "$work/pairs" | sort > "$work/new"
    { cat "$work/new"; sed 's/$/.lbnew/' "$work/new"; } | sort > "$work/mine"
    while IFS= read -r dst; do
        if { [ -e "$dst" ] || [ -L "$dst" ]; } && ! grep -Fxq -- "$dst" "$manifest" 2> /dev/null; then
            say "in the way, and not installed by this script: $dst"; clash=1
        fi
    done < "$work/mine"
    if [ "$clash" = 1 ]; then die "nothing was changed; move those files and run this again"; fi
    : > "$work/old"; if [ -f "$manifest" ]; then cp "$manifest" "$work/old"; fi
    { cat "$work/mine" "$work/old"; } | sort -u > "$work/all"; keep "$work/all"
    while IFS="$tab" read -r rel dst; do
        act mkdir -p "$(dirname -- "$dst")"
        act cp -p "$stage/$rel" "$dst.lbnew"
        act mv -f "$dst.lbnew" "$dst"
    done < "$work/pairs"
    while IFS= read -r old || [ -n "$old" ]; do
        if ! grep -Fxq -- "$old" "$work/new"; then remove_listed "$old"; fi
    done < "$work/old"
    keep "$work/new"
}
main() {
    work=; dry=0; mode=install
    for a in "$@"; do
        case $a in
            --help | -h) usage; return 0 ;;
            --dry-run) dry=1 ;;
            --uninstall) mode=uninstall ;;
            *) usage >&2; return 2 ;;
        esac
    done
    if [ "$dry" = 1 ] && [ "$mode" = uninstall ]; then die "--dry-run and --uninstall do not go together"; fi
    if [ "$(id -u)" = 0 ]; then die "refusing to run as root; run it as the user who will use linuxBee"; fi
    roots
    if [ "$mode" = uninstall ]; then uninstall; return 0; fi
    if [ "$TAG" = unstamped ] || [ "$COMMIT" = unstamped ]; then
        die "this is the copy in the source tree, not a release copy, and installs nothing; use install.sh from the latest release at https://github.com/gingertonik/linuxbee/releases"
    fi
    repo=${LINUXBEE_REPO:-https://github.com/gingertonik/linuxbee.git}
    osrel=${LINUXBEE_OS_RELEASE:-/etc/os-release}
    say "linuxBee $TAG, commit $COMMIT, from $repo"
    check_needs
    have=$(tag_commit)
    if [ "$have" != "$COMMIT" ]; then die "the tag $TAG is at '$have', not at the commit wanted, $COMMIT; nothing was installed"; fi
    say "Programs go to $bin; data files to $data; the list to $manifest"
    if [ "$dry" = 1 ]; then
        say "+ git clone --depth 1 --branch $TAG $repo <temporary folder>/src"
        say "+ cmake --preset release, cmake --build, cmake --install into <temporary folder>/stage"
        say "+ copy the staged files to $bin and $data, and write $manifest"
        say "Dry run: nothing was changed."; return 0
    fi
    trap 'rm -rf "$work"' EXIT
    trap 'exit 1' INT TERM HUP
    fetch; build; place
    say "Installed linuxBee $TAG. To remove it: sh install.sh --uninstall"
    case ":${PATH:-}:" in *":$bin:"*) ;; *) say "Note: $bin is not on your PATH, so add it to run linuxbee from a shell." ;; esac
}

{ main "$@"; }
