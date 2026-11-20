# Releases

How a version gets out. Locked by `tests/locked/cli_release.sh` (and
`cli_ci.sh` for the workflow's rights). The tools are
`tools/bump-version.sh` and `tools/release.sh`; the workflow is
`.github/workflows/release.yml`. All three are locked files.

A release is a tag `vX.Y.Z` on `main` and a GitHub Release with two
files: `install.sh`, stamped for that tag and commit (install.md,
I-STAMP), and `install.sh.sha256`. Nothing else is published: the
installer builds from source.

## R-BUMP

`tools/bump-version.sh X.Y.Z` makes the version commit's changes, and
nothing else does:

- `CMakeLists.txt`'s `project(linuxbee VERSION ...)`;
- `CHANGELOG.md`: what was under `## Unreleased` is now under
  `## X.Y.Z (the date)`, and `## Unreleased` is empty above it;
- the version test.

It refuses a version that is not after the present one, and it
refuses when nothing is under `## Unreleased`.

Between releases, every merge that changes what a user sees adds a
line under `## Unreleased`.

## R-CHECK

`tools/release.sh check <tag>`, which the release job runs first: the
tag is `v` and the version in `CMakeLists.txt`; the changelog has
lines under that version's heading; and nothing is left under
`## Unreleased`. A tag that fails any of these publishes nothing.

A tag with a suffix (`v1.0.0-rc1`) is a pre-release: the version is
the part before the suffix, and its notes may still be under
`## Unreleased`.

## R-INSTALLS

Before anything is published, the release job stamps the installer
for the commit it is on and runs it: into a home folder of its own,
from a clone of the tree it has. Both programs must say the version;
the uninstall must leave no file; the build folder must be gone.
(`tools/ci.sh install`.) A release whose installer does not install is
not made.

## R-PUBLISH

`tools/release.sh publish <tag> <commit>` stamps the installer, writes
its `.sha256`, and makes the GitHub Release with those two files and
the changelog's section as its notes. Notes for a version before 1.0
begin by saying it is for early testing.

- An ordinary tag is published as the latest release, so that
  `https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh`
  is its installer.
- A pre-release is marked so and is not the latest; its installer is
  at `…/releases/download/<tag>/install.sh`.

A tag whose job failed has no Release, so "latest" stays where it was.

**The commit.** (From the review of these tools: the job tested an
installer stamped with the commit it had checked out and published one
stamped with the commit it was told, and nothing held the two to be
the same. Had they differed, every machine would have refused the
installer.) `publish` refuses, publishing nothing, unless the commit it
is given is the commit checked out, and is the commit the tag names at
`origin` (for an annotated tag, the commit the tag's object points
to). A tag `origin` does not have is refused.

## R-RIGHTS

Releases are made one at a time, each to its end
(`concurrency`, group `release`, never cancelled): of two tags pushed
together the later is published last, and so is the latest.

The release workflow runs on a pushed tag `v*` and on nothing else.
Its token reads the source; the one job asks for `contents: write`,
which making a Release needs, and for nothing more. The token is in
the environment of the last step alone (`GH_TOKEN`), so the steps that
build and run the tree never see a token that can write. It uses no
action, no secret but that token, and the same pinned image as CI
(ci.md). The tag's name reaches the scripts from the environment,
never pasted into them.

## R-README

The README's install section has the one fixed line

    curl -fsSL https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh | sh

and beside it: how to download the script, read it, and then run it;
the form for a named release (`…/releases/download/<tag>/install.sh`);
and how to uninstall. It never names the `main` branch or the raw
file host as a place to fetch the script from.

## Not in this version

- Signed tags or signed release files.
- Packages for any distribution; an AppImage (planned after 1.0).
- Building release binaries at all.

## R-CHANGELOG

`CHANGELOG.md` begins with `## Unreleased`, once, and every other
heading of that level is `## X.Y.Z (date)`, each version once. A line
for a change goes under `Unreleased`.

**A released section is not changed.** (Found at 0.7.0: a branch cut
before a release carries its new line just above the older heading;
git merges it under the heading of the release made meanwhile, and
reports no conflict.) Where the tag `vX.Y.Z` is there, the section
`X.Y.Z` is the same, line for line, as in that tag's `CHANGELOG.md`;
and a section a tag released is not taken out.

Where the tree is not a git checkout, or has no such tags (an
archive; the shallow checkout of the CI), this is not checked, and the
test says so. It is checked where the mistake is made: in a worktree,
at a merge.

A section is released by the tag, not by the bump: between
`bump-version.sh` and the tag the new section may still be put right.

## Sources

None outside this project: this document is engineering only, and every
rule in it is a linuxBee decision. Nothing in it describes MusicBee.
