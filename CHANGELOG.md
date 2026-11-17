# Changelog

linuxBee is numbered below 1.0 until it is ready to replace MusicBee for
daily use. Every release before 1.0 is for early testing: commands,
settings and the library file may still change.

## Unreleased

- A scan that cannot store what it finds, because another program has the library, stops at once and says so; it no longer holds the window for five seconds at every part.
- A change the library database refuses (a play that cannot be counted among them) stores
  nothing, and a change with nothing to do asks for no lock. Remapping a folder is all or
  nothing. `linuxbeectl library scan` says when it could not store, and a library in a
  read-only folder says it cannot be written to.
- The settings and the library now live in `~/.config/linuxbee/` and `~/.local/share/linuxbee/`, with the name once; the files of an older build are moved there at the first start, and one program uses a profile at a time (a second start on the same profile is refused). An older library is taken along whole, with its log, or not at all: it is settled first, never moved while another program has it open, and the start is refused (status 6) rather than shown an empty library while it waits.
- The window now says in the status bar when the library could not store a change (a play, added files, saved tags, a scan), once for each refusal or run of them, and never in a dialog.
- A tag save is refused on a file that has more than one name (a hard link, such as a library file linked from a seeding folder). Nothing is written, and the message says how many names the file has and how to make it a file of its own.
- A file that another program changed while linuxBee was saving it is no longer overwritten: the save is refused, with a message to try again, and the other program's change stays. A second save of the same file, while one is under way, is refused the same way.
- A play that cannot be counted because another program has the library no longer holds the window for five seconds, and is counted when the library is free again (while linuxBee stays open; up to 1,000 are kept).
- Sound is decoded and played on a thread of its own, so it goes on while the window is busy.
- On a library kept on a network disk, a play that could not be counted because another program had the library is no longer lost. Plays that were waiting are counted in one step.

## 0.7.1 (2026-11-17)

- A tag save with no room left (a full disk, a quota, a size limit) no longer replaces the song with a cut-off file and says saved; it now fails and leaves the song as it was.
- Still known: a save that changes one field can rewrite other MP3 frames (lyrics, comments,
  label and publisher values can be merged or lost). Keep a backup of music you tag.

## 0.7.0 (2026-11-17)

- linuxBee can be installed for one user with `install.sh`, and removed with `--uninstall`.
- A change made while another program has the library open waits for it. A scan that cannot
  store stores nothing (the window and `linuxbeectl` do not yet say so).
- Organise says when the library could not record its moves, and then records none of the run.
- The desktop entry no longer offers linuxBee for opening audio files, which
  it could not open yet.
- Love (a track marked loved or banned in its tags) is read from and written to files as a field, and can be used in templates and auto-playlist rules.
- Love can be set in the tag editor (L or B), and a Love column shows it as a heart. The window
  cannot add the column yet, and a library scanned before this version shows Love only once
  its files are read again.

## 0.6.0 (2026-11-17)

The first numbered version. It marks the state of `main` once the core
feature set was in place and the first set of data-safety fixes had
landed.

### What is in it

- A library that scans folders off the window's thread, with progress and
  Stop; Send To; Remove from Library; folders watched for new files.
- A track list, an Album and Tracks view with covers at each heading, and
  an artist browser.
- Playlists (including Add to Playlist) and auto-playlists that follow the
  library.
- Playback with a play queue, shuffle, repeat, ReplayGain (Controls >
  Replay Gain) and MPRIS, so desktop media keys work.
- A tag editor, five tagging tools in the window, and one step of undo for
  tag changes (Edit > Undo).
- Organise Files with naming templates and a preview; Convert Format with
  presets; Delete Files to the desktop trash, after a question.
- MusicBee settings import (hotkeys and the naming template), and plain
  `.xml` skins chosen from View > Skins.
- `linuxbeectl`, the command-line tool, for tags, the library, organising,
  converting, playlists, templates, rules, skins and the trash.

### Data safety

- An MP3 or WAV save keeps the file's own ID3v2 version, so ID3v2.4 tags
  (dates, mood and others) are no longer cut down to ID3v2.3.
- Remove from Library, Send To and Delete Files work on a selection of any
  size, all or nothing, and say so when the library refuses.
- A move to another disk is copied, checked and flushed before the
  original is removed, and a move that fails at any step loses no file.
- A file that Organise moves keeps its place in playlists, the play queue
  and the player.
- The undo keeps its pictures in its own folder in the cache, cleaned at
  start.
- linuxBee ends cleanly on SIGTERM, SIGINT and SIGHUP.

### Not yet

See the "Not yet" section of the README.
