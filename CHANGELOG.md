# Changelog

linuxBee is numbered below 1.0 until it is ready to replace MusicBee for
daily use. Every release before 1.0 is for early testing: commands,
settings and the library file may still change.

## Unreleased

- linuxBee can be installed for one user with `install.sh`, and removed with `--uninstall`.
- A change made while another program has the library open waits for it. A scan that cannot
  store stores nothing (the window and `linuxbeectl` do not yet say so).
- Organise says when the library could not record its moves, and then records none of the run.
- A play that cannot be counted, and every other change the library database refuses, is now
  reported (`failed`) and stores nothing; remapping a folder is all or nothing; a library that
  cannot be written to says so, and `linuxbeectl library scan` says when it could not store.
- The desktop entry no longer offers linuxBee for opening audio files, which
  it could not open yet.

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
