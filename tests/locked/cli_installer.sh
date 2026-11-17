#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# install.sh, against a home folder made for this test and a small repository that stands in
# for the project (it installs in a second). Rules: docs/spec/install.md.
# Nothing here reaches the network, the user's home or the real project's build.
# usage: cli_installer.sh /path/to/linuxbeectl   (the argument is not used)
set -u
SRC=$(cd "$(dirname "$0")/../.." && pwd)
S="$SRC/install.sh"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
no() { echo "FAIL: $1"; fail=$((fail + 1)); }
[ -f "$S" ] || { echo "FAIL: no install.sh"; echo "cli_installer: 1 failed"; exit 1; }

# --- the stand-in repository: two releases; the second no longer has linuxbeectl
REPO="$T/repo"
mkdir -p "$REPO/packaging"
g() { git -C "$REPO" -c user.name=t -c user.email=t@example.invalid -c commit.gpgsign=false -c tag.gpgsign=false "$@" > /dev/null 2>&1; }
cat > "$REPO/CMakePresets.json" << 'EOF'
{ "version": 6,
  "configurePresets": [ { "name": "release", "generator": "Ninja", "binaryDir": "${sourceDir}/build/release" } ],
  "buildPresets": [ { "name": "release", "configurePreset": "release" } ] }
EOF
cat > "$REPO/CMakeLists.txt" << 'EOF'
cmake_minimum_required(VERSION 3.25)
project(standin LANGUAGES NONE)
include(GNUInstallDirs)
install(PROGRAMS linuxbee linuxbeectl DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/x.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/x.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
EOF
printf '#!/bin/sh\necho one\n' > "$REPO/linuxbee"
printf '#!/bin/sh\necho ctl\n' > "$REPO/linuxbeectl"
echo entry > "$REPO/packaging/x.desktop"
echo icon > "$REPO/packaging/x.svg"
git init -q "$REPO" && g add -A && g commit -m one && g tag -a v9.9.1 -m one
C1=$(git -C "$REPO" rev-parse HEAD)
sed -i 's/ linuxbeectl / /' "$REPO/CMakeLists.txt"
printf '#!/bin/sh\necho two\n' > "$REPO/linuxbee"
g add -A && g commit -m two && g tag v9.9.2
C2=$(git -C "$REPO" rev-parse HEAD)
[ -n "$C1" ] && [ -n "$C2" ] && [ "$C1" != "$C2" ] || { echo "FAIL: could not make the stand-in repository"; exit 1; }
WRONG=$(printf '%s' "$C1" | tr '0-9a-f' 'f0-9a-e')

# --- stamping (I-STAMP)
ST="$SRC/tools/stamp-install.sh"
[ "$(grep -c '^TAG=unstamped$' "$S")" = 1 ] || no "install.sh has no single line TAG=unstamped"
[ "$(grep -c '^COMMIT=unstamped$' "$S")" = 1 ] || no "install.sh has no single line COMMIT=unstamped"
sh "$ST" v9.9.1 "$C1" < "$S" > "$T/one.sh" || no "stamping failed"
sh "$ST" v9.9.2 "$C2" < "$S" > "$T/two.sh" || no "stamping failed"
sh "$ST" v9.9.1 "$WRONG" < "$S" > "$T/wrong.sh" || no "stamping failed"
grep -qx 'TAG=v9.9.1' "$T/one.sh" && grep -qx "COMMIT=$C1" "$T/one.sh" || no "the stamp is not in the release copy"
sh "$ST" main "$C1" < "$S" > /dev/null 2>&1 && no "a branch name was taken for a tag"
sh "$ST" v9.9.1 abc123 < "$S" > /dev/null 2>&1 && no "a short commit was taken"
sh "$ST" v9.9.1 "$C1" < "$T/one.sh" > /dev/null 2>&1 && no "a stamped script was stamped again"

# --- the shape of the script (I-CLOSED)
[ "$(tail -n 1 "$S")" = '{ main "$@"; }' ] || no 'the last line is not: { main "$@"; }'
grep -qx 'set -eu' "$S" || no "no set -eu"
[ "$(wc -l < "$S")" -le 220 ] || no "longer than 220 lines: $(wc -l < "$S")"
sh -n "$S" || no "sh -n"
grep -q 'raw.githubusercontent\|/main/' "$S" && no "the script names the main branch or the raw host"
if command -v shellcheck > /dev/null 2>&1; then
    shellcheck -s sh "$S" > "$T/sc.log" 2>&1 || no "shellcheck: $(head -n 3 "$T/sc.log" | tr '\n' ' ')"
else
    echo "SKIPPED: shellcheck is not installed here, so install.sh was NOT checked with it"
fi

# --- a home of its own, with things that must never change
H="$T/home"
mkdir -p "$H/.local/share/linuxbee/playlists" "$H/.config/linuxbee" "$H/.cache/linuxbee" "$H/Music"
echo library > "$H/.local/share/linuxbee/library.db"
echo list > "$H/.local/share/linuxbee/playlists/a.m3u8"
echo settings > "$H/.config/linuxbee/settings.ini"
echo song > "$H/Music/song.flac"
FAKE="$T/fakebin"
mkdir -p "$FAKE"
for x in sudo su doas pkexec; do printf '#!/bin/sh\necho "$0 $*" >> "%s/escalated"\nexit 1\n' "$T" > "$FAKE/$x"; chmod +x "$FAKE/$x"; done
printf 'ID=cachyos\nID_LIKE=arch\n' > "$T/os-arch"
printf 'ID=ubuntu\nID_LIKE=debian\n' > "$T/os-other"
printf 'ID=arch\n' > "$T/os-arch2"
run() { # script, then its arguments; the output goes to $T/out
    script=$1; shift
    env -i HOME="$H" PATH="$FAKE:$PATH" TMPDIR="$T/tmp" LINUXBEE_REPO="file://$REPO" LINUXBEE_OS_RELEASE="${OSR:-$T/os-arch}" \
        sh "$script" "$@" > "$T/out" 2>&1
}
mkdir -p "$T/tmp"
state() { (cd "$H" && find . -print | sort; find . -type f -exec sha256sum {} + | sort); }
kept() { (cd "$H" && sha256sum .local/share/linuxbee/library.db .local/share/linuxbee/playlists/a.m3u8 .config/linuxbee/settings.ini Music/song.flac 2>&1); }
state > "$T/state0"; kept > "$T/kept0"
same() { state > "$T/state1"; cmp -s "$T/state0" "$T/state1" || no "$1: the home folder changed: $(diff "$T/state0" "$T/state1" | head -n 3 | tr '\n' ' ')"; }
B="$H/.local/bin"; D="$H/.local/share"; M="$D/linuxbee-installer/manifest"
files() { (cd "$H" && find . -type f -exec sha256sum {} + | sort); }
files > "$T/files0"
samefiles() { files > "$T/files1"; cmp -s "$T/files0" "$T/files1" || no "$1: files were left or changed: $(diff "$T/files0" "$T/files1" | head -n 4 | tr '\n' ' ')"; }

# The copy in the tree installs nothing.
run "$S" && no "the unstamped copy ran"
grep -qi 'release' "$T/out" || no "the unstamped copy does not say what it is"
same "unstamped"

# A download cut off anywhere does nothing (I-CLOSED): every length, to the last line but one.
n=$(wc -l < "$T/one.sh"); i=1; cut_ran=0
while [ "$i" -lt "$n" ]; do
    head -n "$i" "$T/one.sh" > "$T/cut.sh"
    run "$T/cut.sh"
    [ -s "$T/out" ] && grep -qv 'yntax\|unexpected\|end of file' "$T/out" && cut_ran=$i
    i=$((i + 1))
done
[ "$cut_ran" = 0 ] || no "a script cut off after line $cut_ran still did something"
# ... and anywhere inside the last line, when it was given an option (sh -s -- --dry-run): a cut there must not
# become a run without the option. (All but the last byte, which is only the line's end.)
size=$(wc -c < "$T/one.sh"); i=2; cut_ran=0
printf '#!/bin/sh\necho "$*" >> "%s/git.ran"\nexec "%s" "$@"\n' "$T" "$(command -v git)" > "$FAKE/git"; chmod +x "$FAKE/git"
while [ "$i" -le 16 ]; do
    head -c $((size - i)) "$T/one.sh" > "$T/cut.sh"
    run "$T/cut.sh" --dry-run
    [ -e "$T/git.ran" ] && cut_ran=$i
    i=$((i + 1))
done
rm -f "$FAKE/git"
[ "$cut_ran" = 0 ] || no "a script cut off $cut_ran bytes before its end still ran: $(head -n 1 "$T/git.ran")"
rm -f "$T/git.ran"
same "cut off in the last line"
same "cut off"
[ -z "$(ls -A "$T/tmp")" ] || no "a cut-off script left something in the temporary folder"

# --help names every option and both overrides, and changes nothing.
run "$S" --help || no "--help failed"
for word in '--dry-run' '--uninstall' LINUXBEE_REPO LINUXBEE_OS_RELEASE; do grep -q -- "$word" "$T/out" || no "--help does not name $word"; done
same "help"

# --dry-run: the plan, and nothing changed (I-DRY).
run "$T/one.sh" --dry-run || no "the dry run failed: $(tail -n 2 "$T/out" | tr '\n' ' ')"
grep -q 'v9.9.1' "$T/out" || no "the dry run does not name the tag"
grep -q "$C1" "$T/out" || no "the dry run does not name the commit"
grep -q "$B" "$T/out" || no "the dry run does not say where the programs go"
grep -q '^+ ' "$T/out" || no "the dry run prints no action"
same "dry run"
[ -z "$(ls -A "$T/tmp")" ] || no "the dry run left something in the temporary folder"

# A wrong commit is refused, dry or not (I-STAMP).
run "$T/wrong.sh" --dry-run && no "a dry run with a wrong commit went through"
grep -q "$WRONG" "$T/out" || no "the refusal does not name the commit it wanted"
run "$T/wrong.sh" && no "an install with a wrong commit went through"
same "wrong commit"
# The repository says the right commit and hands over another: caught after the clone.
REALGIT=$(command -v git)
printf '#!/bin/sh\ncase "$1" in ls-remote) printf "%%s\\trefs/tags/v9.9.1\\n" "%s" ;; *) exec "%s" "$@" ;; esac\n' "$WRONG" "$REALGIT" > "$FAKE/git"; chmod +x "$FAKE/git"
run "$T/wrong.sh" && no "a clone that is not the stamped commit was installed"
grep -q "$WRONG" "$T/out" || no "the refusal after the clone does not name the commit it wanted"
rm -f "$FAKE/git"
same "a clone at another commit"
[ -z "$(ls -A "$T/tmp")" ] || no "a refused clone was left in the temporary folder"

# A repository that is an option to git is not run as one (I-STAMP).
env -i HOME="$H" PATH="$FAKE:$PATH" TMPDIR="$T/tmp" LINUXBEE_REPO="--upload-pack=touch $T/pwned" LINUXBEE_OS_RELEASE="$T/os-arch" sh "$T/one.sh" --dry-run > "$T/out" 2>&1 \
    && no "a repository beginning with - was accepted"
[ -e "$T/pwned" ] && no "a repository beginning with - was run as an option to git"
same "a repository that is an option"

# A folder spelled with .. is refused, for an install and for an uninstall alike (I-USER).
env -i HOME="$H/../home" PATH="$FAKE:$PATH" TMPDIR="$T/tmp" LINUXBEE_REPO="file://$REPO" LINUXBEE_OS_RELEASE="$T/os-arch" sh "$T/one.sh" > "$T/out" 2>&1 && no "a home spelled with .. was accepted"
env -i HOME="$H" XDG_DATA_HOME="$H/.local/./share" PATH="$FAKE:$PATH" sh "$T/one.sh" --uninstall > "$T/out" 2>&1 && no "a data folder spelled with /./ was accepted for an uninstall"
same "folders spelled with dots"

# Not an Arch-family system: it stops (I-NEEDS).
OSR="$T/os-other" run "$T/one.sh" && no "it installed on another system"
grep -qi 'arch' "$T/out" || no "the refusal does not say which systems it installs on"
same "another system"
OSR="$T/os-arch2" run "$T/one.sh" --dry-run || no "ID=arch alone was not taken for Arch"
OSR="$T/nowhere" run "$T/one.sh" && no "it installed with no os-release file"

# Something missing: all of it said, the pacman line printed, nothing run, nothing changed.
NB="$T/nobin"; mkdir -p "$NB"
for d in $(printf '%s' "$PATH" | tr ':' ' '); do ln -s "$d"/* "$NB"/ 2> /dev/null; done
rm -f "$NB/ffmpeg" "$NB/ninja" "$NB/git" "$NB/sudo" "$NB/pkexec" "$NB/su" "$NB/doas"
printf '#!/bin/sh\ncase "$*" in *taglib*) echo 1.13.1 ;; *Qt6Multimedia*) exit 1 ;; *) exec "%s" "$@" ;; esac\n' "$(command -v pkg-config)" > "$FAKE/pkg-config.off"
cp "$FAKE/pkg-config.off" "$NB/pkg-config.new" && rm -f "$NB/pkg-config" && mv "$NB/pkg-config.new" "$NB/pkg-config" && chmod +x "$NB/pkg-config"
env -i HOME="$H" PATH="$FAKE:$NB" TMPDIR="$T/tmp" LINUXBEE_REPO="file://$REPO" LINUXBEE_OS_RELEASE="$T/os-arch" sh "$T/one.sh" > "$T/out" 2>&1 \
    && no "it went on with git, ffmpeg, ninja, Qt Multimedia and TagLib 2 missing"
grep -q 'pacman -Syu --needed ' "$T/out" || no "no 'pacman -Syu --needed' line for what is missing (Arch: never a partial upgrade)"
for word in git ffmpeg ninja 'TagLib' '2\.0' 'Multimedia'; do grep -v 'pacman' "$T/out" | grep -q "$word" || no "the list of what is missing lacks: $word"; done
grep 'pacman' "$T/out" | grep -q 'qt6-multimedia' || no "the pacman line lacks qt6-multimedia"
grep -qi 'command not found\|cannot ask' "$T/out" && no "with git missing it tried the repository first: $(grep -i 'not found\|cannot ask' "$T/out" | head -n 1)"
grep -q 'CMake\|Qt 6\.5\|Sql\|DBus\|compiler' "$T/out" && no "it lists as missing what is there: $(grep 'CMake\|Qt 6\.5\|Sql\|DBus\|compiler' "$T/out" | head -n 1)"
same "missing"
[ -e "$T/escalated" ] && no "it ran: $(cat "$T/escalated")"

# Root is refused (I-USER). Needs a user namespace; said when it cannot be made.
if unshare -r true 2> /dev/null; then
    env -i HOME="$H" PATH="$FAKE:$PATH" TMPDIR="$T/tmp" LINUXBEE_REPO="file://$REPO" LINUXBEE_OS_RELEASE="$T/os-arch" unshare -r sh "$T/one.sh" > "$T/out" 2>&1 \
        && no "it ran as root"
    grep -qi 'root' "$T/out" || no "the refusal does not say root"
    env -i HOME="$H" PATH="$FAKE:$PATH" unshare -r sh "$T/one.sh" --uninstall > "$T/out" 2>&1 && no "it uninstalled as root"
    same "root"
else
    echo "SKIPPED: no user namespace here, so the refusal to run as root was NOT checked"
fi

# A file in the way that it did not install: named, and nothing changed (I-INSTALL).
mkdir -p "$B"; echo mine > "$B/linuxbee"; state > "$T/state0"
run "$T/one.sh" && no "it overwrote a file it had not installed"
grep -q "$B/linuxbee" "$T/out" || no "the file in the way is not named"
same "a file in the way"
rm -r "$B"; rmdir "$H/.local/bin" 2> /dev/null; state > "$T/state0"

# An install that stops partway (a full disk, Ctrl-C): nothing is left that the script cannot account for.
REALCP=$(command -v cp); REALMV=$(command -v mv)
[ -x "$REALCP" ] && [ -x "$REALMV" ] || no "no cp or mv to stand in for"
for x in "$REALCP" "$REALMV"; do printf '#!/bin/sh\ncase "$*" in *"%s"*linuxbeectl*) echo "$0: no space left on device" >&2; exit 1 ;; esac\nexec "%s" "$@"\n' "$H" "$x" > "$FAKE/$(basename "$x")"; chmod +x "$FAKE/$(basename "$x")"; done
run "$T/one.sh" && no "an install whose copy failed said it succeeded"
[ -e "$B/linuxbee" ] || echo "NOTE: the copy failed before the first file was in place; the partway case was not reached"
run "$T/one.sh" --uninstall || no "the uninstall after a partway install failed: $(tail -n 2 "$T/out" | tr '\n' ' ')"
samefiles "a partway install, then an uninstall"
run "$T/one.sh" && no "an install whose copy failed said it succeeded"
rm -f "$FAKE/cp" "$FAKE/mv"
run "$T/one.sh" || no "the install could not be run again after it stopped partway: $(tail -n 3 "$T/out" | tr '\n' ' ')"
[ "$("$B/linuxbeectl" 2> /dev/null)" = ctl ] || no "running it again after a partway install did not finish it"
[ -z "$(find "$H" -name '*.lbnew' -o -name '*.tmp' -o -name '*.partial' | head -n 1)" ] || no "a partway install left a temporary name: $(find "$H" -name '*.lbnew' -o -name '*.tmp' -o -name '*.partial' | head -n 2 | tr '\n' ' ')"
run "$T/one.sh" --uninstall > /dev/null 2>&1
samefiles "a partway install, run again, then an uninstall"

# The install (I-INSTALL).
run "$T/one.sh" || no "the install failed: $(tail -n 3 "$T/out" | tr '\n' ' ')"
[ "$("$B/linuxbee" 2> /dev/null)" = one ] || no "linuxbee is not installed"
[ "$("$B/linuxbeectl" 2> /dev/null)" = ctl ] || no "linuxbeectl is not installed"
[ -f "$D/applications/x.desktop" ] && [ -f "$D/icons/hicolor/scalable/apps/x.svg" ] || no "the desktop files are not installed"
printf '%s\n' "$B/linuxbee" "$B/linuxbeectl" "$D/applications/x.desktop" "$D/icons/hicolor/scalable/apps/x.svg" | sort > "$T/want"
sort "$M" 2> /dev/null | cmp -s - "$T/want" || no "the list is not exactly what was installed: $(tr '\n' ' ' < "$M" 2> /dev/null)"
grep -q '^+ ' "$T/out" || no "the install prints no action"
grep -q "PATH" "$T/out" || no "it does not say the programs' folder is not on PATH"
[ -z "$(ls -A "$T/tmp")" ] || no "the build folder was left: $(ls "$T/tmp" | head -n 2 | tr '\n' ' ')"
(cd "$H" && find . -type f | sort) > "$T/files1"
[ "$(wc -l < "$T/files1")" = 9 ] || no "files besides the four, the list and what was there: $(tr '\n' ' ' < "$T/files1")"
kept > "$T/kept1"; cmp -s "$T/kept0" "$T/kept1" || no "the install touched the library, the settings or the music"

# The next version over it: replaced, and what it no longer has is gone.
run "$T/two.sh" || no "installing over an install failed: $(tail -n 3 "$T/out" | tr '\n' ' ')"
[ "$("$B/linuxbee" 2> /dev/null)" = two ] || no "linuxbee was not replaced"
[ -e "$B/linuxbeectl" ] && no "a file the new version no longer has was left"
grep -q 'linuxbeectl' "$M" && no "the list still names the removed file"

# --uninstall trusts the list only so far (I-UNINSTALL).
{ echo "$H/Music/song.flac"; echo "$D/linuxbee/library.db"; echo "$B/../../Music/song.flac"; echo "$H/.config/linuxbee/settings.ini"; echo "$D/applications/../linuxbee/playlists/a.m3u8"; } >> "$M"
env -i HOME="$H" PATH="$FAKE:/usr/bin:/bin" sh "$S" --uninstall > "$T/out" 2>&1 || no "the uninstall failed (from the unstamped copy, with no tools): $(tail -n 2 "$T/out" | tr '\n' ' ')"
kept > "$T/kept1"; cmp -s "$T/kept0" "$T/kept1" || no "THE UNINSTALL REMOVED the library, the settings, a playlist or music"
grep -q "song.flac" "$T/out" || no "a line it would not honour is not named"
[ -e "$B/linuxbee" ] && no "linuxbee was left"
[ -e "$D/applications/x.desktop" ] && no "the desktop entry was left"
[ -e "$M" ] && no "the list was left"
[ -d "$D/linuxbee-installer" ] && no "the list's folder was left"
[ -d "$D/icons" ] && no "an empty folder the install made was left"
[ -d "$D" ] || no "the data folder itself was removed"
[ -d "$B" ] || no "the programs' folder itself was removed"
(cd "$H" && find . -type f | sort) > "$T/files2"
[ "$(wc -l < "$T/files2")" = 4 ] || no "after the uninstall: $(tr '\n' ' ' < "$T/files2")"

# A file that could not be removed stays on the list, so a later uninstall can finish (I-UNINSTALL).
run "$T/two.sh" > /dev/null 2>&1 || no "the install before the last uninstall checks failed"
chmod a-w "$B"
run "$S" --uninstall && no "an uninstall that could not remove a file said it succeeded"
chmod u+w "$B"
[ -e "$B/linuxbee" ] && grep -qxF "$B/linuxbee" "$M" 2> /dev/null || no "a file that could not be removed is no longer on the list"
run "$S" --uninstall || no "the uninstall could not be finished afterwards"
[ ! -e "$B/linuxbee" ] && [ ! -e "$M" ] || no "the finished uninstall left the program or the list"

# Nothing installed: says so, changes nothing.
state > "$T/state0"
run "$S" --uninstall; grep -qi 'nothing' "$T/out" || no "an uninstall with no list does not say so"
same "uninstall with nothing installed"
[ -e "$T/escalated" ] && no "it ran: $(cat "$T/escalated")"

echo "cli_installer: $fail failed"
[ "$fail" -eq 0 ]
