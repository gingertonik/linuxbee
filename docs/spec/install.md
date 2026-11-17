# The installer

`install.sh`, at the root of the tree, builds linuxBee from source and
installs it for one user. Locked by `tests/locked/cli_installer.sh`.
It is run as

    curl -fsSL https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh | sh

or downloaded, read, and run. A script fetched from the network and
piped into a shell deserves suspicion, and every rule below is there
to earn the trust that line asks for.

## I-STAMP

The copy in the tree carries the two lines `TAG=unstamped` and
`COMMIT=unstamped`, each exactly once, and installs nothing: it says
it is not a release copy and where a release copy is.

`tools/stamp-install.sh <tag> <commit>` reads `install.sh` on its
input and writes a release copy: the two lines carry the tag
(`v1.2.3`, or `v1.2.3-rc1`) and the full 40-digit commit. It refuses
anything else, and a script without exactly those two lines.

A release copy clones that tag and nothing else, and goes on only if
the commit it got is `COMMIT`. Once it knows the tools are there
(I-NEEDS comes first: without `git` there is nothing to ask with), it
asks the repository for the tag's commit before it fetches or changes
anything (`git ls-remote`), so `--dry-run` refuses a wrong commit
too; and it checks again after cloning (`git rev-parse HEAD`). A tag that was moved after the release
therefore installs nothing.

`LINUXBEE_REPO` names another repository to clone (a mirror, or a
test's). The commit must match there too. A value that `git` would
read as an option (it begins with `-`) is never run as one.

## I-CLOSED

The body is functions. Nothing runs until the last line, which is
`{ main "$@"; }`, so a download cut off anywhere does nothing at all:
no beginning of that line is a command. (A bare `main "$@"` cut
after `main` would run without the options it was given, and a dry
run would become an install.) It runs under `set -eu`. It is at most
220 lines, so that it can be read
before it is run. It is clean under `shellcheck` and runs in a POSIX
shell.

## I-USER

It refuses to run as root and it never runs `sudo`, `su`, `doas` or
`pkexec`. Everything it writes is under

- `${XDG_BIN_HOME:-$HOME/.local/bin}`: the two programs;
- `${XDG_DATA_HOME:-$HOME/.local/share}`: the desktop entry, the
  AppStream file, the icon, the licence, and its own list
  (`linuxbee-installer/manifest`);
- a temporary folder for the build, removed when it ends, whether it
  succeeded or not.

It never writes under `linuxbee/` in the data, settings or cache
folders: those hold the library, the settings and the playlists.

The two folders must be absolute and spelled plainly: one with `..`
or `/./` in it is refused, for an install and an uninstall alike.
(The uninstall distrusts such lines in its list; an install into such
a folder could then never be removed.)

## I-NEEDS

Before it clones, it looks for what a build needs, and says all that
is missing at once, with the version wanted:

| Needed | How it is looked for |
|---|---|
| `git`, `ninja`, `pkg-config`, `ffmpeg` | on the path |
| CMake 3.25 or later | `cmake --version` |
| a C++20 compiler | `c++ -std=c++20` accepts a line of C++20 |
| Qt 6.5 or later | `pkg-config --modversion Qt6Widgets` |
| Qt 6 Multimedia, Sql and DBus | `pkg-config --exists` for each |
| TagLib 2.0 or later | `pkg-config --modversion taglib` |

It installs none of them. On an Arch-family system (`ID` or `ID_LIKE`
in `/etc/os-release` names `arch`) it prints the one line that gets
them, `pacman -Syu --needed` and the packages (with `-Syu`: Arch does
not support installing against a system that is not up to date), for
the user to run, and stops. On any other system it
stops whether or not anything is missing: it prints the list, and that
this script installs on Arch-family systems only. (Other systems name
these packages differently and ship different versions; a line that is
wrong on some of them is worse than none.)

`LINUXBEE_OS_RELEASE` names another file to read in place of
`/etc/os-release`.

`--help` names the options and both of these, so that neither is a
secret switch. Neither gets round I-USER or the commit check.

## I-DRY

Every action is printed before it is done, as `+ ` and the command.
`--dry-run` makes the checks above (all of them, I-STAMP's included),
prints what it would do, and changes nothing: no file, no folder, no
clone.

## I-INSTALL

It builds the `release` preset (build.md, B-PRESETS) in the temporary
folder, installs into a folder there, and copies from it. So the list
it writes is exactly what it put in place: one full path a line.

A file already in the way that is not on the list of an earlier
install is not overwritten: the installer names it and stops, having
changed nothing. Installing over an earlier install replaces its
files and removes those the new version no longer has.

**Stopped partway.** The list is written before the first file is put
in place: everything this run may put there, with what an earlier
list held. When the run ends well, the list is rewritten as exactly
what is in place. So after a full disk or Ctrl-C during the copy,
running it again finishes the install, and `--uninstall` removes
what was put there; neither meets a file it does not know. No
temporary name is left beside a file by a run that ended well.

If the folder for the programs is not on `PATH`, it says so at the end.

### The last word

An install ends by saying how to remove it, in a form that works for
someone who piped the script and has no file:
`curl -fsSL https://github.com/gingertonik/linuxbee/releases/download/<its tag>/install.sh | sh -s -- --uninstall`.
It does not tell the user to run `sh install.sh`. The README gives the
same with `latest`.

## I-UNINSTALL

`--uninstall` removes the files on the list, then the list. It needs no
stamp, no network and no build tools. It removes a folder only when the
install made it empty and it is not one of the two roots above.

It trusts the list no further than this: a line is honoured only if it
is under one of the two roots, has no `..` in it, and is not under
`linuxbee/` in the data folder. Any other line is named and left. It
never removes the settings, the library, playlists or music.

A file it may remove and cannot (the folder is read-only) stops the
uninstall with an error, and the list is kept, so that it can be run
again. A line it would not honour is not kept: it was never this
script's file.

With no list, it says that nothing was installed by it and changes
nothing.

## Known and left

- **A folder below the data folder that is a link elsewhere** (a
  `~/.local/share/applications` kept by a dotfiles manager): the
  install writes through the link, as any program would, and the
  uninstall then will not follow it, names the line, and leaves the
  file. The user's link is the user's; the script does not remove
  through one.
- **A file changed by hand after the install** is removed by the
  uninstall like any other. No checksum is kept.

## Not in this version

- Installing on other systems (an AppImage is planned after 1.0).
- Checking a signature. The release job publishes a `.sha256` beside
  the script, which guards against a broken download, not a hostile
  one; the commit check is what binds the script to the source.
- Running the tests on the user's machine.
