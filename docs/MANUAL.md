The full manual for linuxBee. The landing page is [../README.md](../README.md).

# linuxBee

A music manager and player for Linux, built for people who are moving from
MusicBee on Windows and want their habits to keep working: the same kind of
tag editor, the same file-naming templates, the same organise and convert
tools, and, in time, their library data carried across.

linuxBee is an independent project. It is not affiliated with or endorsed by
MusicBee or its author, and it contains no MusicBee code or artwork. How
that is kept true is written down in [docs/CLEAN-ROOM.md](CLEAN-ROOM.md).

## Status

linuxBee is usable for early testing. It is not released, and expect
changes to commands, settings and the library file.

## What works now

Each item is specified in `docs/spec/` and wired into the window, the
command-line tool, or both.

### Library and browsing

- Add files to the library, and scan folders for new files (File menu).
  The scan looks at the files on a thread of its own, so the window stays
  usable while it runs. The status bar shows the scan's progress as a bar
  and a count of files, such as `120 of 800 files`, with a Stop button at
  its right end. Stop asks first, and the files found so far stay in the
  library. File > Stop Scan does the same. `linuxbeectl library` scans
  folders into a library file, and counts or lists it.
- `linuxbeectl library import FILE --map Z:=/` brings the date added, play
  counts, skip counts and last played from a library exported by MusicBee,
  and checks its ratings and Love against the files. It changes nothing
  unless `--apply` is given, and never a music file. Scan your music into
  the library first: the import only decorates tracks the library has, and
  never adds one. `--map OLD=NEW` says where a folder of the file is now,
  and may be given several times; for MusicBee run under Wine, `--map Z:=/`
  is the whole machine. Ratings and Love are only compared with what the
  library read from the files, and listed where they differ. `--list` shows
  every entry of every list instead of the first ten. For example:
  `linuxbeectl library --db library.db import "iTunes Music Library.xml"
  --map Z:=/` shows what would change, and the same with `--apply` stores it.
- The navigator shows the library sections (Music, Inbox, Audiobooks,
  Podcasts, Videos), the playlists and Playing Tracks.
- The track list shows one track in one row. It sorts by column and narrows
  by a search box.
- The main panel has a Tracks view and an Album and Tracks view, chosen from
  the drop-down box at the top of the main panel. Album and Tracks shows the
  tracks under a heading for each album, in album order, with a jump bar of
  letters. Each heading shows the album's cover, when the album has one, before
  its name. The cover is read from the music file or from a picture file beside
  it. Activating a track plays from there, and activating an album's heading
  plays that album.
- The artist browser sits beside the track list. It browses by Album Artist,
  Artist, Album, Composer, Genre, Grouping or Year.
- Send To (right-click menu) sends tracks to the Inbox, Music Library or
  Audiobooks section. Remove from Library drops tracks from the library and
  deletes no files.
- Delete Files (File menu, Shift+Del) moves the selected tracks' files to the
  desktop trash, after a question. The tracks whose files were moved leave the
  library. A file the trash does not take stays where it is, and the message
  says how many files were not moved. To take a file back, restore it from the
  trash. Nothing is deleted for good. `linuxbeectl trash FILE...` does the same
  for files named on the command line.
- The Delete key takes the selected tracks out of the view on show. In a
  playlist it takes them out of the playlist and asks nothing. In Playing
  Tracks it takes them out of the queue. Anywhere else it asks first, then
  removes them from the library. The files stay in every case.
- Folders listed on the Library page of the preferences are watched, and
  files that arrive in them are scanned in.

### Playlists

- New Playlist (File > Playlists) and the navigator's right-click menu make
  playlists. Rename, move and delete are on that menu.
- Add to Playlist… (the right-click menu on the track list) adds the selected
  tracks to the end of a playlist you choose. A track that is already in the
  playlist is added again.
- A double-click on a track in a playlist plays from the playlist's own
  order.
- Auto-playlists: each `.lbauto` file in the `auto-playlists` folder beside
  the `playlists` folder shows in the navigator with the tracks its rules
  pick, and stays up to date as the library changes. Its row's menu deletes
  it.
- `linuxbeectl playlist show` prints a playlist file, and
  `linuxbeectl playlist convert` converts between M3U, M3U8 and PLS.

### Playback

- Play/Pause, Stop, Previous Track and Next Track, a seek bar, volume and
  mute, in the player bar and the Controls menu.
- Shuffle, Repeat and Stop After Current.
- Controls > Replay Gain: Off, Track Gain or Album Gain, applied to the
  track that is playing at once and kept for next time. A boost stops at
  full volume.
- Play Now, Queue Next and Queue Last for the selected tracks.
- Files sent to the trash leave Playing Tracks, except the one playing. A file
  that is playing when it is sent to the trash plays to its end.
- In Playing Tracks, the Delete key takes the selected tracks out of the queue.
  The track that is playing stays in the queue until it has played to its end.
- Files are decoded with the `ffmpeg` program and played to the default
  audio device.

### Tags

- `linuxbeectl tags read` prints a file's tags, pictures and audio properties.
- `linuxbeectl tags write` sets or clears fields and other keys, makes an
  image the front cover, or removes every picture. `--id3v2.3` and
  `--id3v2.4` choose the ID3v2 version of the tag (see below).
- MP3 and WAV saves keep the file's own ID3v2 version. An ID3v2.4 tag stays
  ID3v2.4, and an ID3v2.3 tag stays ID3v2.3. A file with no ID3v2 tag, or with
  ID3v2.2, gets ID3v2.4. `--id3v2.3` or `--id3v2.4` forces one version whatever
  the file had. ID3v2.3 has no frame for some values, such as the mood, the
  disc subtitle and the release date. When a save to ID3v2.3 cannot keep a
  value, the save still goes ahead, and standard error gets one line for each
  value that was not kept.
- Edit Tags (File menu) edits the tags of one track or of many at once.
- linuxBee does not change the tags of a file that has hard links (more than one
  name). A save would replace one name and leave the others with the old tags, so
  it is refused, before anything is written, and the message says how many
  names the file has. To tag in linuxBee, keep the library as plain files: copy,
  do not link, from a download or seeding folder. To tag one such file, make it a
  file of its own first, with `cp -p FILE FILE.new && mv FILE.new FILE` (not
  `cp --remove-destination FILE FILE`, which answers "are the same file" and does
  nothing). The other names keep the old tags. linuxBee gives the count of names
  and does not search for them; `find FOLDER -samefile FILE` lists them. Playing,
  browsing and scanning such files work as ever.
- A save is also refused, with a message to try again, when another program
  changed the file while linuxBee was saving it, or when another save of the same
  file is under way. The file is then as the other program left it.
- Edit > Undo (Ctrl+Z) takes back the last tag change, whether it came from the
  tag editor or from one of the tagging tools. It is one step: the next tag
  change, or closing linuxBee, makes the change final. Pictures that a tool
  removed come back too. Organise is not undone.
- Tools > Tagging Tools, in the Tools menu, opens five tagging tools in the
  window: Search and Replace…, Capitalise…, Remove Tags…, Renumber Tracks…, and
  Infer and Update Tags From Filename…. Each shows a preview of what would change, and
  Apply writes it. Remove Tags asks first when it would remove every tag or the
  embedded pictures.
- `linuxbeectl tagtool` runs bulk replace, case, remove, renumber, infer, copy
  and swap.
- `linuxbeectl gain` measures, writes and removes volume levelling
  (ReplayGain) tags.
- `linuxbeectl cover` shows which picture stands for a track's cover, and
  extracts it to a file.

### Organise and convert

- Organise Files (File menu) moves files to the folders a naming template
  gives, with a preview before anything moves. `linuxbeectl organise` does
  the same, with `--copy` to copy and `--dry-run` to preview.
- `linuxbeectl template` evaluates and checks a naming template, and
  `linuxbeectl path build` builds a relative path from one.
- `linuxbeectl rules` checks a rule set, and tests a track against it.
- Convert Format (Tools menu) and `linuxbeectl convert` convert files with
  presets. `linuxbeectl convert --list-presets` lists them.

### Settings, skins and import

- Edit > Preferences opens a Hotkeys page and a Library page, where the
  monitored folders are chosen.
- The window remembers which sidebars and the status bar were shown.
- View > Skins > Choose Skin… asks for a skin file and uses its colours.
  View > Skins > No Skin goes back to linuxBee's own look. View > Skins >
  Add Skin… copies a skin file into the skins folder and uses it. The skin in
  use is stored as `appearance/skin` in `linuxbee.ini`. The colours are applied
  when the file is a plain `.xml` skin. `linuxbeectl skin show` lists the
  colours it takes.
- `linuxbeectl settings import` carries the hotkeys and the organiser's
  naming template over from a MusicBee settings file.

### Desktop integration

- linuxBee registers on the session bus as an MPRIS player, so desktop media
  keys and player widgets can control playback.

## Not yet

The gaps a MusicBee user will notice first:

- **Album Covers, Artists and Jukebox views.** These views are not built. The
  main panel has the Tracks and Album and Tracks views only.
- **Covers outside the album headings, and downloads.** Only the album headings
  in the Album and Tracks view show a cover. No other part of the window does,
  and pictures are not downloaded.
- **Making or editing an auto-playlist in the window.** An auto-playlist is
  shown, and a double-click plays the tracks it shows, but it can only be
  made by putting a file in its folder.
- **Drag and drop.** Tracks cannot be dragged, for example onto a playlist.
- **Gapless playback, crossfade and an equaliser.**
- **Importing a MusicBee library from the window.** The import is a command
  only (`linuxbeectl library import`, above). Ratings and Love are checked
  against the files, not copied, because linuxBee reads them from the tags.
- **Undo for Organise.** Organise cannot be undone. Undo covers tag changes
  only, and only one step.
- **Auto-tagging, and Copy and Swap in the window.** Auto-tagging from online
  services is not built. Copy and Swap are on the command line only.
- **Compiled skins, and plugins.** Compiled skins (`.xmlc`), and a skin's
  pictures and layout, are not used. Plugins are not built.

## Try it

### Install a release

```sh
curl -fsSL https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh | sh
```

That address takes you to the `install.sh` of the release marked latest. To
read the script before running it:

```sh
curl -fsSL -o install.sh \
  https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh
less install.sh
sh install.sh --dry-run
sh install.sh
```

A named release installs from the same file under its tag, at
`https://github.com/gingertonik/linuxbee/releases/download/<tag>/install.sh`.

The script builds a release from the tagged source in a temporary folder and
installs into `~/.local`, keeping the list of what it put there. Each release
carries its own copy of the script, stamped with the tag and the commit it was
cut from, and the script refuses if the tag does not point at that commit.

It checks what the build needs before it fetches anything. On Arch and the
Arch family, anything missing is listed with the one `pacman -Syu --needed`
line that gets it, and the script stops there without running that line.
Anywhere else it stops too, and says this script installs on Arch-family
systems only.

It refuses to run as root and never uses sudo: everything it writes goes into
your home. `install.sh --dry-run` makes every check, prints what it would do
and changes nothing.

- `install.sh --uninstall` removes what the installer put there, from its own
  list.
- Settings, the library, playlists and music files are never touched by the
  uninstall.

With the one-liner there is no file to run, so:

```sh
curl -fsSL https://github.com/gingertonik/linuxbee/releases/latest/download/install.sh | sh -s -- --uninstall
```

The installer prints that same line for its own release when it finishes.

### Build from source

After `sh tools/check.sh` prints `rc=0`, two programs are in `build/dev`:

- `build/dev/src/app/linuxbee` opens the window.
- `build/dev/src/cli/linuxbeectl` is the command-line tool.

`linuxbeectl --help` lists its commands. The output is:

```
usage: linuxbeectl <command> [arguments]

commands:
  convert    Convert music files to another format.
  cover      Show or extract a track's cover picture.
  gain       Measure, write or remove volume levelling (ReplayGain) tags.
  library    Scan and list a music library.
  organise   Move or copy music files to where a naming template says.
  path       Build a relative file path from a naming template.
  playlist   Show a playlist file, or convert one to another format.
  rules      Check a rule set, or test a track against it.
  settings   Carry over hotkeys and the naming template from a MusicBee settings file.
  skin       Show the colours linuxBee takes from a MusicBee skin file.
  tags       Read the tags, pictures and audio properties of a music file.
  tagtool    Bulk tag tools: replace, case, remove, renumber, infer, copy, swap.
  template   Fill in or check a naming template.
  trash      Move music files to the trash.

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

linuxBee keeps its settings in `$XDG_CONFIG_HOME/linuxbee/linuxbee.ini`
and its library in `$XDG_DATA_HOME/linuxbee/library.db`. The playlists
are in the `playlists` folder beside the library. `$XDG_CONFIG_HOME` and
`$XDG_DATA_HOME` default to `~/.config` and `~/.local/share`.

Builds up to 0.7 kept them one folder deeper (`.../linuxbee/linuxbee/`). At the
first start of a newer build, what is in that inner folder is moved up. The
library goes whole, with its log files, or not at all: it is settled first,
and it is never moved while another program has it open. A name already taken
in the new place is left where it is and named on standard error. If an older
library is still waiting and cannot be taken over, linuxBee does not start on an
empty library: it says why and ends with status 6. Back up both folders, with
linuxBee closed, before the first start of a newer build.

One program uses a profile at a time. A second start on the same profile is
refused, with `already running` on standard error and status 5.

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
and the `ffmpeg` program. [packaging/README.md](../packaging/README.md) lists what the
install puts where.

## Contributing

Read [docs/CLEAN-ROOM.md](CLEAN-ROOM.md) before changing code. Each part of
linuxBee is built and tested against the specification in `docs/spec/`.

## Licence

GPL-3.0-or-later. See [LICENSE](../LICENSE).
