#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# The release tools, on a small tree made here, with a stand-in for gh; and the README's install lines.
# Rules: docs/spec/release.md. (The release workflow's rights are in cli_ci.sh.)
# usage: cli_release.sh /path/to/linuxbeectl   (the argument is not used)
set -u
SRC=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
no() { echo "FAIL: $1"; fail=$((fail + 1)); }
C= # the commit of the small tree, set by tree
GIT="git -c user.name=t -c user.email=t@example.invalid -c init.defaultBranch=main"

tree() { # a fresh small tree at 1.2.3, with two lines unreleased
    rm -rf "$T/t"; mkdir -p "$T/t/tools" "$T/t/tests"
    cp "$SRC/tools/release.sh" "$SRC/tools/bump-version.sh" "$SRC/tools/stamp-install.sh" "$T/t/tools/"
    cp "$SRC/install.sh" "$T/t/install.sh" 2> /dev/null || no "no install.sh"
    printf 'cmake_minimum_required(VERSION 3.25)\nproject(linuxbee VERSION 1.2.3 LANGUAGES CXX)\n' > "$T/t/CMakeLists.txt"
    printf '        QCOMPARE(linuxbee::versionString(), QStringLiteral("1.2.3"));\n' > "$T/t/tests/tst_version.cpp"
    printf '# Changelog\n\nWords.\n\n## Unreleased\n\n- A new thing.\n- Another.\n\n## 1.2.3 (2026-01-02)\n\n- The old thing.\n\n## 1.2.30 (2020-01-01)\n\n- A decoy.\n' > "$T/t/CHANGELOG.md"
    # R-PUBLISH, "the commit": the tree is a git checkout with an origin, as the release job's is.
    rm -rf "$T/origin.git"
    $GIT init -q --bare "$T/origin.git" && (cd "$T/t" && $GIT init -q . && $GIT add -A && $GIT commit -qm tree \
        && $GIT remote add origin "$T/origin.git" && $GIT push -q origin HEAD:refs/heads/main) || no "the test could not make its git tree"
    C=$(cd "$T/t" && git rev-parse HEAD)
}
tagged() { (cd "$T/t" && $GIT push -q -f origin "$2:refs/tags/$1") || no "the test could not push the tag $1"; } # a plain tag at origin
in_tree() { (cd "$T/t" && "$@"); }
FAKE="$T/fake"; mkdir -p "$FAKE" "$T/assets"
cat > "$FAKE/gh" << EOF
#!/bin/sh
echo "\$*" >> "$T/gh.args"
for a in "\$@"; do case "\$a" in */install.sh | */install.sh.sha256 | */notes.md) cp "\$a" "$T/assets/" ;; esac; done
[ -n "\${GH_FAIL:-}" ] && exit 1
exit 0
EOF
chmod +x "$FAKE/gh"
pub() { rm -f "$T/gh.args" "$T/assets"/*; (cd "$T/t" && PATH="$FAKE:$PATH" sh tools/release.sh publish "$@" > "$T/out" 2>&1); }

# R-CHECK
tree
in_tree sh tools/release.sh check v1.2.3 > /dev/null 2>&1 && no "check: lines still under Unreleased, and it passed"
in_tree sh tools/bump-version.sh 1.3.0 2026-02-03 > /dev/null 2>&1 || no "bump failed"
in_tree sh tools/release.sh check v1.3.0 > "$T/out" 2>&1 || no "check refused a good tag: $(cat "$T/out")"
in_tree sh tools/release.sh check v1.2.3 > /dev/null 2>&1 && no "check: a tag that is not the version passed"
in_tree sh tools/release.sh check v1.3 > /dev/null 2>&1 && no "check: v1.3 passed"
in_tree sh tools/release.sh check 1.3.0 > /dev/null 2>&1 && no "check: a tag without v passed"
in_tree sh tools/release.sh check main > /dev/null 2>&1 && no "check: a branch name passed"

# R-BUMP: what it changed
grep -q '^project(linuxbee VERSION 1.3.0 LANGUAGES CXX)$' "$T/t/CMakeLists.txt" || no "bump: CMakeLists.txt"
grep -q 'QStringLiteral("1.3.0")' "$T/t/tests/tst_version.cpp" || no "bump: the version test"
grep -q 'VERSION 3.25' "$T/t/CMakeLists.txt" || no "bump: it changed another number"
printf '# Changelog\n\nWords.\n\n## Unreleased\n\n## 1.3.0 (2026-02-03)\n\n- A new thing.\n- Another.\n\n## 1.2.3 (2026-01-02)\n\n- The old thing.\n\n## 1.2.30 (2020-01-01)\n\n- A decoy.\n' > "$T/want"
cmp -s "$T/want" "$T/t/CHANGELOG.md" || no "bump: the changelog is not as specified: $(diff "$T/want" "$T/t/CHANGELOG.md" | head -n 4 | tr '\n' ' ')"
cp "$T/t/CHANGELOG.md" "$T/before"
in_tree sh tools/bump-version.sh 1.4.0 > /dev/null 2>&1 && no "bump: went on with nothing unreleased"
in_tree sh tools/bump-version.sh 1.3.0 > /dev/null 2>&1 && no "bump: the same version again"
printf '\n- More.\n' >> "$T/t/CHANGELOG.md"; sed -i 's/^## Unreleased$/## Unreleased\n\n- Newer./' "$T/t/CHANGELOG.md"; cp "$T/t/CHANGELOG.md" "$T/before"
in_tree sh tools/bump-version.sh 1.2.9 > /dev/null 2>&1 && no "bump: went backwards"
in_tree sh tools/bump-version.sh 1.4 > /dev/null 2>&1 && no "bump: took 1.4 for a version"
in_tree sh tools/bump-version.sh 1.4.0 tomorrow > /dev/null 2>&1 && no "bump: took a word for a date"
cmp -s "$T/before" "$T/t/CHANGELOG.md" || no "bump: a refused bump changed the changelog"
grep -q 'VERSION 1.3.0 ' "$T/t/CMakeLists.txt" || no "bump: a refused bump changed the version"
in_tree sh tools/bump-version.sh 1.10.0 2026-03-04 > /dev/null 2>&1 || no "bump: 1.10.0 is after 1.3.0"

# notes: the section, the whole section and nothing but it
tree; in_tree sh tools/bump-version.sh 1.3.0 2026-02-03 > /dev/null 2>&1
in_tree sh tools/release.sh notes v1.3.0 > "$T/notes" 2>&1
grep -q 'A new thing' "$T/notes" && grep -q 'Another' "$T/notes" || no "notes: the section is not there"
grep -q 'old thing\|decoy\|Words\|^## ' "$T/notes" && no "notes: more than the section"
grep -qi 'early testing' "$T/notes" && no "notes: 1.x called early testing"
sed -i 's/VERSION 1.3.0 /VERSION 0.7.0 /; s/^## 1.3.0 /## 0.7.0 /' "$T/t/CMakeLists.txt" "$T/t/CHANGELOG.md"
in_tree sh tools/release.sh notes v0.7.0 | head -n 1 | grep -qi 'early testing' || no "notes: a version before 1.0 does not begin by saying early testing"
in_tree sh tools/release.sh notes v1.2.3 > /dev/null 2>&1 && no "notes: given for a tag that is not the version"
# a heading is matched whole (1.2.3 is not 1.2.30), and a heading with nothing under it is not a release
tree; printf '# Changelog\n\n## Unreleased\n\n## 1.2.30 (2026-01-03)\n\n- A decoy.\n\n## 1.2.3 (2026-01-02)\n\n- The old thing.\n' > "$T/t/CHANGELOG.md"
in_tree sh tools/release.sh notes v1.2.3 > "$T/notes" 2>&1 || no "notes: refused 1.2.3 beside 1.2.30"
grep -q 'old thing' "$T/notes" || no "notes: 1.2.3's own section is not given"
grep -q 'decoy' "$T/notes" && no "notes: 1.2.30's section given for 1.2.3"
printf '# Changelog\n\n## Unreleased\n\n## 1.2.3 (2026-01-02)\n\n## 1.2.2 (2026-01-01)\n\n- Older.\n' > "$T/t/CHANGELOG.md"
in_tree sh tools/release.sh check v1.2.3 > /dev/null 2>&1 && no "check: a version with nothing under its heading passed"
tree; in_tree sh tools/bump-version.sh 1.3.0 2026-02-03 > /dev/null 2>&1
sed -i 's/VERSION 1.3.0 /VERSION 0.7.0 /; s/^## 1.3.0 /## 0.7.0 /' "$T/t/CMakeLists.txt" "$T/t/CHANGELOG.md"

# R-PUBLISH
tagged v0.7.0 "$C"
pub v0.7.0 "$C" || no "publish failed: $(tail -n 2 "$T/out" | tr '\n' ' ')"
[ "$(wc -l < "$T/gh.args" 2> /dev/null)" = 1 ] || no "publish: gh was not called exactly once"
a=$(cat "$T/gh.args" 2> /dev/null)
case "$a" in "release create v0.7.0 "*) ;; *) no "publish: not 'gh release create v0.7.0': $a" ;; esac
for word in '--verify-tag' '--latest' '--notes-file' 'linuxBee 0.7.0'; do case "$a" in *"$word"*) ;; *) no "publish: gh was not given $word" ;; esac; done
case "$a" in *--prerelease* | *--latest=false* | *--draft*) no "publish: an ordinary tag is not published as the latest" ;; esac
grep -qx 'TAG=v0.7.0' "$T/assets/install.sh" 2> /dev/null && grep -qx "COMMIT=$C" "$T/assets/install.sh" || no "publish: the installer is not stamped for the tag and commit"
(cd "$T/assets" && sha256sum -c --quiet install.sh.sha256 > /dev/null 2>&1) || no "publish: install.sh.sha256 does not match install.sh"
[ "$(cut -c67- "$T/assets/install.sh.sha256" 2> /dev/null)" = install.sh ] || no "publish: the .sha256 names another file"
grep -q 'A new thing' "$T/assets/notes.md" 2> /dev/null || no "publish: the notes are not the changelog's section"
grep -qx 'TAG=unstamped' "$T/t/install.sh" || no "publish: it stamped the tree's own install.sh"
[ -z "$(cd "$T/t" && find . -newer "$T/gh.args" -type f 2> /dev/null)" ] || no "publish: it wrote into the tree"
# refused: nothing published
pub v0.7.1 "$C" && no "publish: a tag that is not the version went out"
[ -e "$T/gh.args" ] && no "publish: gh was called for a tag that is not the version"
pub v0.7.0 abc && no "publish: a short commit went out"
[ -e "$T/gh.args" ] && no "publish: gh was called with a short commit"
GH_FAIL=1 pub v0.7.0 "$C" && no "publish: gh failed and it said nothing"
# "the commit": what is published is what is checked out, and what the tag names at origin
O=$(cd "$T/t" && $GIT commit-tree -m other 'HEAD^{tree}') # another commit; HEAD stays
tagged v0.7.0 "$O"
pub v0.7.0 "$C" && no "publish: went out although the tag at origin names another commit"
[ -e "$T/gh.args" ] && no "publish: gh was called for a tag that names another commit"
pub v0.7.0 "$O" && no "publish: a commit that is not the one checked out went out"
[ -e "$T/gh.args" ] && no "publish: gh was called for a commit that is not checked out"
(cd "$T/t" && $GIT push -q origin :refs/tags/v0.7.0)
pub v0.7.0 "$C" && no "publish: went out although origin has no such tag"
[ -e "$T/gh.args" ] && no "publish: gh was called for a tag origin does not have"
# an annotated tag names its commit through the tag's own object
(cd "$T/t" && $GIT tag -f -a v0.7.0 -m v0.7.0 "$C" > /dev/null && $GIT push -q -f origin refs/tags/v0.7.0)
[ "$(cd "$T/t" && git ls-remote origin refs/tags/v0.7.0 | cut -c1-40)" != "$C" ] || no "the test's annotated tag is not an object of its own"
pub v0.7.0 "$C" || no "publish: an annotated tag was refused: $(tail -n 2 "$T/out" | tr '\n' ' ')"
grep -qx "COMMIT=$C" "$T/assets/install.sh" 2> /dev/null || no "publish: an annotated tag's installer is not stamped with the commit"
(cd "$T/t" && $GIT tag -f -a v0.7.0 -m v0.7.0 "$O" > /dev/null && $GIT push -q -f origin refs/tags/v0.7.0)
pub v0.7.0 "$C" && no "publish: went out although the annotated tag names another commit"
# a pre-release: marked, and not the latest; its notes may still be unreleased
tree; sed -i 's/VERSION 1.2.3 /VERSION 2.0.0 /' "$T/t/CMakeLists.txt"
tagged v2.0.0-rc1 "$C"
pub v2.0.0-rc1 "$C" || no "publish: a pre-release failed: $(tail -n 2 "$T/out" | tr '\n' ' ')"
a=$(cat "$T/gh.args" 2> /dev/null)
case "$a" in *--prerelease*) ;; *) no "publish: a pre-release is not marked so" ;; esac
case "$a" in *"--latest=false"*) ;; *) no "publish: a pre-release is not kept from being the latest" ;; esac
grep -qx 'TAG=v2.0.0-rc1' "$T/assets/install.sh" 2> /dev/null || no "publish: the pre-release's installer is not stamped with its tag"
grep -q 'A new thing' "$T/assets/notes.md" 2> /dev/null || no "publish: a pre-release's notes are not what is unreleased"
pub v2.0.0 "$C" && no "publish: an ordinary tag went out with its lines still unreleased"

# R-README
R="$SRC/README.md"
grep -qxF 'curl -fsSL https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh | sh' "$R" || no "README: not the one fixed line, on a line of its own"
grep -qF 'releases/download/<tag>/install.sh' "$R" || no "README: no form for a named release"
grep -qF -- '--uninstall' "$R" || no "README: not how to uninstall"
grep -qxF 'curl -fsSL https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh | sh -s -- --uninstall' "$R" \
    || no "README: no way to uninstall for someone who used the one line and has no file"
grep -qF -- '--dry-run' "$R" || no "README: not how to see what it would do"
grep -qiE 'read (it|the script)' "$R" || no "README: not download, read, run"
grep -q 'raw.githubusercontent\|linuxbee/main/\|blob/main/install' "$R" && no "README: the script is fetched from the main branch"
grep -q 'It is not released' "$R" && no "README: still says it is not released"

echo "cli_release: $fail failed"
[ "$fail" -eq 0 ]
