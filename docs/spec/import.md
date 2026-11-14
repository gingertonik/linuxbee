# Importing a library

A way in for someone leaving another program: the things a music file
does not carry (when it was added, how often it was played, when it was
last played) and the playlists, read from an iTunes-format library XML
file. MusicBee can write one (Library preferences, "export the library as
an iTunes formatted XML file").

This document is the contract for `src/core/import/itunesxml.h`. The
locked test `tests/locked/tst_import.cpp` checks it against
`tests/data/import/library.xml`, a hand-written file in the publicly known
layout. It builds on `docs/spec/library.md`.

The import never adds a track to the library and never touches a music
file. The user first points linuxBee at their music (a scan); the import
then decorates the tracks the library already has.

## Sources

| Tag | Source |
|---|---|
| W-L | MusicBee wiki, "Library Preferences": https://musicbee.fandom.com/wiki/Library_Preferences |
| PL | The Apple property list XML format (`plist`, `dict`, `key`, `array`, `string`, `integer`, `real`, `date`, `data`, `true`, `false`): https://en.wikipedia.org/wiki/Property_list |
| IT | The iTunes library XML layout, as widely described: a top-level `dict` with `Tracks` (a `dict` of track `dict`s keyed by track ID) and `Playlists` (an `array` of `dict`s with `Name` and `Playlist Items`). |
| I-… | A linuxBee decision. |

Read on 2026-11-13. No real export from MusicBee has been examined; which
of the keys below MusicBee writes is an open question. A forum guide from
2014 says ratings and play counts are exported, half-stars are dropped,
and a multi-value tag keeps only its first value. Tags are therefore not
imported at all: the music files are the truth for tags.

## I-PARSE

`parseItunesLibrary(bytes)` reads a property list (PL). It is all or
nothing: when it is not ok, `error` says why and both lists are empty.

Not ok: bytes that are not well-formed XML; a document whose root is not
`plist` holding a `dict`; no `Tracks` key, or one that is not a `dict`.

A track is read from each `dict` in `Tracks`, in file order. Keys may come
in any order; keys not listed here are ignored.

| Key | Type | Becomes |
|---|---|---|
| `Track ID` | integer | `trackId` |
| `Location` | string | `location` (I-LOCATION) |
| `Name`, `Artist`, `Album` | string | the same; empty when absent |
| `Rating` | integer 0 to 100 | `rating`: the number divided by 10, rounded to the nearest whole number (a half goes up), divided by 2. Below 10 there is no rating. |
| `Rating Computed` | true | the rating is the album's, not the track's: no rating |
| `Play Count`, `Skip Count` | integer | the counts; a negative number is 0 |
| `Date Added` | date | `dateAdded`, UTC |
| `Play Date UTC` | date | `lastPlayed`, UTC |

A track with no `Track ID` above 0, or whose location is empty after
I-LOCATION (no `Location`, or a stream), is left out.

A playlist is read from each `dict` in `Playlists`, in file order, with
its `Name` trimmed and the `Track ID` of each of its `Playlist Items`, in
order, whether or not the file has such a track. Left out: a playlist with
`Master` true, with a `Distinguished Kind`, with `Smart Info`, with
`Folder` true, or with no name. A missing `Playlists` key is no playlists.

## I-LOCATION

`pathFromItunesLocation(url)`: the text is trimmed. It must start with
`file:` (any case); anything else gives an empty text.

- After `file:` may come `//host`. The host `localhost`, or none, means
  this machine. Any other host makes a network path: `//host/share/...`.
- The rest is percent-decoded as UTF-8. A `+` is a plus sign.
- A leading `/` before a drive letter is dropped: `/C:/Music` is
  `C:/Music`.
- The result is cleaned (no `.`, `..` or doubled slashes, no trailing
  slash; the two leading slashes of a network path stay).

## I-MAP

The file holds the paths of the machine it was made on. A `PathMapping`
is a list of pairs: an old folder and a new one.

`mapImportedPath(path, mapping)`: backslashes become slashes and the path
is cleaned. The pairs are tried in order; a pair fits when the path lies
under its old folder (the folder itself does not fit, and `Music2` is not
under `Music`). The first that fits replaces the old folder by the new
one. Old folders that start with a drive letter or `//` are compared
ignoring case, others exactly. A pair with an empty old folder fits
nothing. A path nothing fits is returned cleaned and otherwise unchanged.

## I-PLAN

`planImport(imported, libraryTracks, mapping)` decides what would be done.
It changes nothing. An import that is not ok gives an empty plan.

For each imported track, in order, the mapped path is looked up in the
library: an exact path first; failing that, ignoring case, but only when
exactly one library track fits. No fit: the mapped path goes to
`unmatched`.

When several imported tracks land on the same library track, only the
first gets a match; the others still count as found for playlists.

A match says what the library should hold afterwards:

| Value | Rule |
|---|---|
| date added | the file's, if it has one; else the library's |
| play count, skip count | the larger of the file's and the library's |
| last played | the later of the two; the library's if the file has none |

`changes` is false when that is what the library holds already. Running
the import twice therefore changes nothing the second time, and plays
counted in linuxBee since the first import are not lost.

`rating` is passed along untouched for the caller. MusicBee stores ratings
in the music files by default, so a caller should not write it unless the
user asks.

Each imported playlist becomes a list of library paths, in order, with
repeats kept; items whose track was not found are counted in `missing`.
Every playlist is reported, even when empty.

## Not in this version

- A command or dialog that applies the plan (`Library::setStatistics`,
  `PlaylistStore`): the window's import page does that.
- Tags, artwork, the equaliser and anything else in the file.
- Reading MusicBee's own library, playlist or settings files.
- Playlist folders: playlists come in flat.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| I-PARSE | A real export: which keys are there? Is `Date Added` written, is `Skip Count`, are half-star ratings? |
| I-PARSE | Are auto-playlists exported, and how are they marked? |
| I-LOCATION | The form of `Location` MusicBee writes (`file://localhost/C:/...`?). |
