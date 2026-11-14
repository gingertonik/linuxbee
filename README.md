# linuxBee

A music manager and player for Linux, built for people who are moving from
MusicBee on Windows and want their habits to keep working: the same kind of
tag editor, the same file-naming templates, the same organise and convert
tools, and their library data carried across.

linuxBee is an independent project. It is not affiliated with or endorsed by
MusicBee or its author, and it contains no MusicBee code or artwork. How
that is kept true is written down in [docs/CLEAN-ROOM.md](docs/CLEAN-ROOM.md).

## Status

linuxBee is usable for early testing. It is not released, and expect
changes to commands, settings and the library file.

## What works now

Each item is specified in `docs/spec/` and wired into the window, the
command-line tool, or both.

### Library and browsing

- Add files to the library, and scan folders for new files (File menu).
  `linuxbeectl library` scans folders into a library file, and counts or
  lists it.
- The navigator shows the library sections (Music, Inbox, Audiobooks,
  Podcasts, Videos), the playlists and Playing Tracks.
- The track list sorts by column and narrows by a search box.
- The artist browser sits beside the track list. It browses by Album Artist,
  Artist, Album, Composer, Genre, Grouping or Year.
- Send To (right-click menu) sends tracks to the Inbox, Music Library or
  Audiobooks section. Remove from Library drops tracks from the library and
  deletes no files.
- Folders listed on the Library page of the preferences are watched, and
  files that arrive in them are scanned in.

### Playlists

- New Playlist (File > Playlists) and the navigator's right-click menu make
  playlists. Rename, move and delete are on that menu.
- A double-click on a track in a playlist plays from the playlist's own
  order.
- `linuxbeectl playlist show` prints a playlist file, and
  `linuxbeectl playlist convert` converts between M3U, M3U8 and PLS.

### Playback

- Play/Pause, Stop, Previous Track and Next Track, a seek bar, volume and
  mute, in the player bar and the Controls menu.
- Shuffle, Repeat and Stop After Current.
- Play Now, Queue Next and Queue Last for the selected tracks.
- Files are decoded with the `ffmpeg` program and played to the default
  audio device.

### Tags

- `linuxbeectl tags read` prints a file's tags, pictures and audio properties.
- `linuxbeectl tags write` sets or clears fields and other keys, makes an
  image the front cover, or removes every picture. MP3 files get ID3v2.3 by
  default.
- Edit Tags (File menu) edits the tags of one track or of many at once.
- `linuxbeectl tagtool` runs bulk replace, case, remove, renumber, infer,
  copy and swap.
- `linuxbeectl gain` measures, writes and removes volume levelling
  (ReplayGain) tags.

### Organise and convert

- Organise Files (File menu) moves files to the folders a naming template
  gives, with a preview before anything moves. `linuxbeectl organise` does
  the same, with `--copy` to copy and `--dry-run` to preview.
- `linuxbeectl template` evaluates and checks a naming template, and
  `linuxbeectl path build` builds a relative path from one.
- `linuxbeectl rules` checks a rule set, and tests a track against it.
- Convert Format (Tools menu) and `linuxbeectl convert` convert files with
  presets. `linuxbeectl convert --list-presets` lists them.

### Settings and import

- Edit > Preferences opens a Hotkeys page and a Library page, where the
  monitored folders are chosen.
- The window remembers which sidebars and the status bar were shown.
- `linuxbeectl settings import` carries the hotkeys and the organiser's
  naming template over from a MusicBee settings file.

### Desktop integration

- linuxBee registers on the session bus as an MPRIS player, so desktop media
  keys and player widgets can control playback.

## Not yet

The gaps a MusicBee user will notice first:

- **Album view.** The track list shows tracks, not albums with their tracks.
- **Cover art.** Pictures are read from files, but no part of the window
  shows them.
- **Auto-playlists.** The code is in the core. The window and `linuxbeectl`
  do not make or show one yet.
- **Adding tracks to a playlist.** Playlists can be made, renamed, moved and
  deleted, but no command adds tracks to one, and there is no drag and drop.
- **Gapless playback, crossfade and an equaliser.**
- **ReplayGain in playback.** `linuxbeectl gain` writes the tags, but the
  player does not apply them yet.
- **Background scanning.** A scan blocks the window until it finishes.
- **Importing a MusicBee library.** Only the settings file can be imported.
  The import code is in the core, with no command or window that runs it.
- **Undo, and Delete Files.** Organise, tag edits and tag tools cannot be
  undone, and files cannot be deleted from the window.
- **Tagging tools in the window, and auto-tagging.** The tagging tools are on
  the command line only. Auto-tagging from online services is not built.
- **Skins and plugins.** Neither is specified or built.

## Try it

After `sh tools/check.sh` prints `rc=0`, two programs are in `build/dev`:

- `build/dev/src/app/linuxbee` opens the window.
- `build/dev/src/cli/linuxbeectl` is the command-line tool.

`linuxbeectl --help` lists its commands. The output is:

```
usage: linuxbeectl <command> [arguments]

commands:
  convert    Convert music files to another format.
  gain       Measure, write or remove volume levelling (ReplayGain) tags.
  library    Scan and list a music library.
  organise   Move or copy music files to where a naming template says.
  path       Build a relative file path from a naming template.
  playlist   Show a playlist file, or convert one to another format.
  rules      Check a rule set, or test a track against it.
  settings   Carry over hotkeys and the naming template from a MusicBee settings file.
  tags       Read the tags, pictures and audio properties of a music file.
  tagtool    Bulk tag tools: replace, case, remove, renumber, infer, copy, swap.
  template   Fill in or check a naming template.

linuxbeectl --version prints the version.
```

Some commands to try:

```sh
linuxbeectl tags read ~/Music/song.mp3
linuxbeectl library --db ~/linuxbee-test.db scan ~/Music
linuxbeectl library --db ~/linuxbee-test.db count
linuxbeectl path build --field "Album Artist=Adele" --field "Album=21" --field "Title=Someone like You" --ext mp3 '<Album Artist>\<Album>\<Title>'
```

The last command prints `Adele/21/Someone like You.mp3`.

linuxBee keeps its settings in `$XDG_CONFIG_HOME/linuxbee/linuxbee/linuxbee.ini`
and its library in `$XDG_DATA_HOME/linuxbee/linuxbee/library.db`. The playlists
are in the `playlists` folder beside the library. `$XDG_CONFIG_HOME` and
`$XDG_DATA_HOME` default to `~/.config` and `~/.local/share`. The name
`linuxbee` appears twice in each path.

To carry over the hotkeys and the organiser's naming template from a MusicBee 3
settings file, close linuxBee and run `linuxbeectl settings import FILE`. Without
other options it prints what it would carry over and writes nothing. Add
`--apply --to INI` to write those settings into the settings file `INI`, which is
made if it does not exist.

## Build

Needs CMake 3.25+, Ninja, a C++20 compiler, Qt 6.5+ and clang-format.

```sh
sh tools/check.sh
```

The last line it prints is `rc=0` when the tree lints, builds and passes
every test.

At run time the programs need Qt 6 (Core, Sql, Widgets and Multimedia), TagLib 2
and the `ffmpeg` program. [packaging/README.md](packaging/README.md) lists what the
install puts where.

## Contributing

Read [docs/CLEAN-ROOM.md](docs/CLEAN-ROOM.md) before changing code. Each part of
linuxBee is built and tested against the specification in `docs/spec/`.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
