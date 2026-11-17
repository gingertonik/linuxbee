            . - ~ ~ - .
                   >-)))'>
██     ██████ ██  ██ ██  ██ ██  ██ █████  ██████ ██████
██       ██   ███ ██ ██  ██  ████  ██  ██ ██     ██
██       ██   ██ ███ ██  ██   ██   █████  █████  █████
██       ██   ██  ██ ██  ██  ████  ██  ██ ██     ██
██████ ██████ ██  ██  ████  ██  ██ █████  ██████ ██████

  A music manager for the rest of us.
  Version 1.0 | November 2026 | pre-release

  THE MUSIC STAYS. THE HABITS COME WITH.

  by gingertonik

[![Licence: GPL-3.0](https://img.shields.io/badge/licence-GPL--3.0-blue)](LICENSE)
[![Language: C++20](https://img.shields.io/badge/language-C%2B%2B20-blue)](docs/spec/)

The full manual, every feature and command:
**[docs/MANUAL.md](docs/MANUAL.md)**. Everything below is the short version.

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
│ Status     : usable for early testing. Unreleased. Expect changes.       │
│ Licence    : GPL-3.0-or-later. Built clean-room from public specs.       │
└──────────────────────────────────────────────────────────────────────────┘
```

## 1. What is this?

You know how you keep your music. The naming templates, the auto-playlists,
the tag editor you reach for every day. linuxBee is that, on Linux, built for
people moving off MusicBee on Windows so their habits keep working. Usable for
early testing. Not released. Expect changes to commands, settings and the
library file.

## 2. What you get

- Scan folders into the library on a thread of its own. The window stays
  usable, the bar counts `120 of 800 files`, and Stop keeps what it found.
- The navigator carries the library sections, the playlists, and Playing Tracks.
- One row per track. Sort by column, narrow with the search box.
- The Album and Tracks view shows albums in order, covers read from the files,
  and a letter jump bar. Click an album heading and the album plays.
- The artist browser browses by Album Artist, Artist, Album, Composer, Genre,
  Grouping, Year.
- Playlists, and auto-playlists that keep their rows filled from rules as the
  library changes.
- Play, queue, shuffle, repeat, volume, mute. Files decode with `ffmpeg` to
  your default audio device.
- A tag editor, five tagging tools with previews, undo for tag changes, and the
  same bulk tools on the command line: replace, case, remove, renumber, infer,
  copy, swap, plus ReplayGain measurement and cover extraction.
- Organise Files: naming templates, with a preview before anything moves.
  Convert with presets. `Delete Files` goes to the desktop trash, not for
  good.
- Preferences, remembered sidebars, skin files, and `settings import` carries
  your MusicBee hotkeys and naming template over.
- An MPRIS player on the session bus, so desktop media keys work.

The complete, checked list is in [docs/MANUAL.md](docs/MANUAL.md).

## 3. Getting started

Needs CMake 3.25+, Ninja, a C++20 compiler, Qt 6.5+ and clang-format:

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
`~/.config/linuxbee/linuxbee/linuxbee.ini`, the library in
`~/.local/share/linuxbee/linuxbee/library.db`, playlists beside it. Yes, the
name `linuxbee` appears twice in each path; the manual says why. To carry
over from a MusicBee 3 settings file: `linuxbeectl settings import FILE`
prints what it would take, `--apply --to INI` writes it.

`linuxbeectl --help` lists every command; the manual documents them all.

## 4. Frequently asked

**Can I import my MusicBee library?** Not yet. The settings file, yes: hotkeys
and the naming template. The import code is in the core, with no command or
window running it yet.

**Does it undo?** One step, tag changes only, from the editor or the tools.
Organise cannot be undone.

**Covers everywhere, downloading art?** Covers show in the album headings, read
from the files. Nothing downloads.

**Gapless, crossfade, equaliser?** Not built. ReplayGain tags get written; the
player does not apply them yet.

**Drag and drop?** Not yet. Auto-playlists are shown and playable; you make one
by dropping a `.lbauto` file in its folder, not in the window.

**Compiled skins, plugins?** Plain `.xml` skins use their colours. No `.xmlc`,
no skin pictures, no plugins.

**Is it GPL?** Yes. Section 5.

## 5. The fine print

linuxBee is an independent project. It is not affiliated with or endorsed by
MusicBee or its author, and it contains no MusicBee code or artwork. How
that is kept true is written down in [docs/CLEAN-ROOM.md](docs/CLEAN-ROOM.md).

Read [docs/CLEAN-ROOM.md](docs/CLEAN-ROOM.md) before changing code. Each part
of linuxBee is built and tested against the specification in `docs/spec/`.

GPL-3.0-or-later. See [LICENSE](LICENSE).

## 6. Where the real details are

- **[docs/MANUAL.md](docs/MANUAL.md)**: the full manual. Every feature,
  command, path and setting, checked line by line against the specs.
- [docs/spec/](docs/spec/): the specifications the code is tested against.
- [docs/CLEAN-ROOM.md](docs/CLEAN-ROOM.md): how the clean-room rule is kept.
- [packaging/README.md](packaging/README.md): what the install puts where.

```
────────────────────────────────────────────────────────────────────────────

  Questions, fixes, and the whole list of what is left:
  the repo's Issues page, and the manual's "Not yet".

                                    - gingertonik

────────────────────────────────────────────────────────────────────────────
```
