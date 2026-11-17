```text
            . - ~ ~ - .
                   >-)))'>
██     ██████ ██  ██ ██  ██ ██  ██ █████  ██████ ██████
██       ██   ███ ██ ██  ██  ████  ██  ██ ██     ██
██       ██   ██ ███ ██  ██   ██   █████  █████  █████
██       ██   ██  ██ ██  ██  ████  ██  ██ ██     ██
██████ ██████ ██  ██  ████  ██  ██ █████  ██████ ██████
```

  A music manager for the rest of us.
  Version pre-1.0 | November 2026

  THE MUSIC STAYS. THE HABITS COME WITH.

  by gingertonik

[![Licence: GPL-3.0](https://img.shields.io/badge/licence-GPL--3.0-blue)](LICENSE)
[![Language: C++20](https://img.shields.io/badge/language-C%2B%2B20-blue)](docs/spec/)

**[docs/MANUAL.md](docs/MANUAL.md)** has everything.
This page is the short version.

```
───────────────────────────────── CONTENTS ─────────────────────────────────

  1. What is this?
  2. What you get
  3. Getting started
  4. Frequently asked
  5. The fine print
  6. Where the real details are

```

```
┌─ At a glance ────────────────────────────────────────────────────────────┐
│ Built from : C++20, Qt 6, TagLib, ffmpeg                                 │
│ Needs      : Linux, a sound card, a music folder                         │
│ Status     : early testing. Releases below 1.0 may still change.         │
│ Licence    : GPL-3.0-or-later. Built clean-room from public specs.       │
└──────────────────────────────────────────────────────────────────────────┘
```

## 1. What is this?

linuxBee is a music manager for Linux, built for people leaving MusicBee on
Windows. The same tag editor, the same naming templates, the same organise and
convert tools, so a MusicBee user can sit down and work. It is early testing,
not a release. Expect the commands, settings and library file to change.

## 2. What you get

- Scan folders while you keep working. The window stays usable, the bar counts
  `120 of 800 files`, and Stop keeps what it found.
- The navigator holds the library sections, the playlists and Playing Tracks.
- One row per track. Sort by column, narrow with the search box.
- Albums in order, covers read from the files, a letter jump bar. Click an
  album heading and it plays.
- The artist browser browses by Album Artist, Artist, Album, Composer, Genre,
  Grouping, Year.
- Playlists, and auto-playlists that refill their rows from rules as the
  library changes.
- Play, queue, shuffle, repeat, volume, mute. Files decode with `ffmpeg` to
  your default audio device.
- A tag editor, five tagging tools with previews, one step of undo for tag
  changes. The same bulk tools on the command line: replace, case, remove,
  renumber, infer, copy, swap. ReplayGain tags and cover extraction too.
- Organise Files: naming templates, with a preview before anything moves.
  Convert with presets. `Delete Files` goes to the desktop trash, not for
  good.
- Preferences, remembered sidebars, skin files. `settings import` carries your
  MusicBee hotkeys and naming template over.
- An MPRIS player on the session bus, so desktop media keys work.

The whole list is in the [manual](docs/MANUAL.md).

## 3. Getting started

Install it:

```sh
curl -fsSL https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh | sh
```

Or read it first:

```sh
curl -fsSL -o install.sh \
  https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh
less install.sh
sh install.sh --dry-run
sh install.sh
```

It builds a release from the tagged source in a temporary folder and installs
into `~/.local`, keeping the list of what it put there. It checks what the
build needs before it fetches anything. On Arch and the Arch family, anything
missing comes with one `pacman -Syu --needed` line, and it stops there without
running that line. Anywhere else it stops and says this script installs on
Arch-family systems only. No root, no sudo: everything it writes goes into
your home. `--dry-run` shows the plan.

A named release installs from
`https://github.com/gingertonik/linuxbee/releases/download/<tag>/install.sh`.

To uninstall, if you used the one-liner and have no `install.sh` to run:

```sh
curl -fsSL https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh | sh -s -- --uninstall
```

It takes back what it installed and leaves settings, library, playlists and
music alone.

If you prefer to build from source: CMake 3.25+, Ninja, a C++20 compiler,
Qt 6.5+, TagLib 2, ffmpeg and clang-format, then

```sh
sh tools/check.sh
```

When it prints `rc=0`, the window is `build/dev/src/app/linuxbee` and the
command-line tool is `build/dev/src/cli/linuxbeectl`. A taste:

```sh
linuxbeectl tags read ~/Music/song.mp3
linuxbeectl library --db ~/linuxbee-test.db scan ~/Music
linuxbeectl library --db ~/linuxbee-test.db count
linuxbeectl path build --field "Album Artist=Adele" --field "Album=21" \
  --field "Title=Someone like You" --ext mp3 '<Album Artist>\<Album>\<Title>'
```

The last line prints `Adele/21/Someone like You.mp3`. Settings live in
`~/.config/linuxbee/linuxbee.ini`, the library in
`~/.local/share/linuxbee/library.db`, playlists beside it. Builds up to 0.7
kept them one folder deeper, and the first start of a newer build moves them
up; the [manual](docs/MANUAL.md) has the rules. To carry over from
a MusicBee 3 settings file: `linuxbeectl settings import FILE` prints what it
would take, `--apply --to INI` writes it.

`linuxbeectl --help` lists every command.

## 4. Frequently asked

**Can I import my MusicBee library?** Not yet. The settings file, yes: hotkeys
and the naming template. The import code is in the core, with no command or
window running it yet.

**Can it tag files that are hard-linked from a seeding folder?** No. A file with
more than one name is not saved, because a save would replace one name and leave
the others with the old tags. Keep the library as plain files (copy, do not
link, from a download or seeding folder). To tag one such file, make it a file
of its own first: `cp -p FILE FILE.new && mv FILE.new FILE`. `find FOLDER
-samefile FILE` lists a file's other names. Playing, browsing and scanning such
files work as ever.

**Does it undo?** One step, tag changes only, from the editor or the tools.
Organise cannot be undone.

**Can it download cover art?** No. Covers are read from the files, and only
the album headings show them.

**Gapless, crossfade, equaliser?** Not built. ReplayGain is applied: Controls >
Replay Gain, Off, Track Gain or Album Gain. A boost stops at full volume.

**Drag and drop?** Not yet. Tracks cannot be dragged onto a playlist, and an
auto-playlist is made by dropping a `.lbauto` file in its folder.

**Compiled skins, plugins?** Plain `.xml` skins use their colours. No `.xmlc`,
no skin pictures, no plugins.

**Is it GPL?** Yes. Section 5.

## 5. The fine print

linuxBee is an independent project. It is not affiliated with or endorsed by
MusicBee or its author, and it contains no MusicBee code or artwork. How
that is kept true is written down in [docs/CLEAN-ROOM.md](docs/CLEAN-ROOM.md).

Read [docs/CLEAN-ROOM.md](docs/CLEAN-ROOM.md) before changing code.

GPL-3.0-or-later. See [LICENSE](LICENSE).

## 6. Where the real details are

- [docs/MANUAL.md](docs/MANUAL.md): the full manual. Every feature, command,
  path and setting.
- [docs/spec/](docs/spec/): the specifications.
- [docs/CLEAN-ROOM.md](docs/CLEAN-ROOM.md): how the clean-room rule is kept.
- [packaging/README.md](packaging/README.md): what the install puts where.

```
────────────────────────────────────────────────────────────────────────────

  Questions and fixes go to the Issues page.
  The list of what is not built is the manual's "Not yet".

                                    - gingertonik

────────────────────────────────────────────────────────────────────────────
```
