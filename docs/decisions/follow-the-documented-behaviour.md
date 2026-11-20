# Where MusicBee's documented behaviour is followed

**Status:** Decided by the owner, 2026-11-20. To be built: until its package lands, each spec says what the program does now.

## The question

The same review found rules that differ from what the public pages document.
For these the documented behaviour is to be followed.

## The decision

- Queue Next: a later one goes after the earlier ones.
- A rule's `matches` heeds case, with a second form that does not.
- A search covers all fields, if the library's speed bound holds.
- `+` in a search widens it as OR does (read from one example).
- Auto-playlist duplicates are the same artist and title.
- `$Date` takes dates written with slashes; `$Group` gives the first n
  letters; a missing Disc-Track# is written `#` when organising.
- The words ignored when sorting apply to title, artist and album only.
- A warning when a track already in a playlist is added to it again, on by
  default. A hotkey that is taken warns and moves.
- Repeat is on by default.
- Display Artist is read from its own tag, and is the artists joined when
  there is none. The total of discs is written as `TOTALDISCS`; both names
  are read.
- Covers are looked for as Cover, Artwork, Front, the file's own name, the
  embedded picture, and then `folder.jpg`, which is linuxBee's own.
- A command that only reads refuses a library of an older schema and says
  why; only a command that writes brings it up to date.

## What it allows

The packages that build these change the specs and the tests together.

## Where it is relied on

- `docs/spec/play-queue.md`
- `docs/spec/rules.md`
- `docs/spec/search.md`
- `docs/spec/auto-playlists.md`
- `docs/spec/templates.md`
- `docs/spec/paths.md`
- `docs/spec/track-list.md`
- `docs/spec/playlist-editing.md`
- `docs/spec/preferences.md`
- `docs/spec/player-bar.md`
- `docs/spec/track-fields.md`
- `docs/spec/tag-writing.md`
- `docs/spec/covers.md`
- `docs/spec/library.md`, L-OPEN
