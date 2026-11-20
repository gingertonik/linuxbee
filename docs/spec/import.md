# Importing a library

A way in for someone leaving another program: the things a music file
does not carry (when it was added, how often it was played, when it was
last played) and the playlists, read from an iTunes-format library XML
file. MusicBee can write one (Library preferences, "export the library as
an iTunes formatted XML file", W-L).

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
| W-ANS | MusicBee wiki, the pages that answer the questions under "Answered from the MusicBee wiki": "How to export MB library to iTunes": https://musicbee.fandom.com/wiki/How_to_export_MB_library_to_iTunes |

Read on 2026-11-13. No real export from MusicBee has been examined; which
of the keys below MusicBee writes is an open question. A forum guide from
2014 says ratings and play counts are exported, half-stars are dropped,
and a multi-value tag keeps only its first value. Tags are therefore not
imported at all: the music files are the truth for tags.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Library Preferences" (3.0.5976;
2017-10-12).

## I-PARSE

`parseItunesLibrary(bytes)` reads a property list (PL). It is all or
nothing: when it is not ok, `error` says why and both lists are empty.

Not ok: bytes that are not well-formed XML; a document whose root is not
`plist` holding a `dict`; no `Tracks` key, or one that is not a `dict`.

A value that is not what its element says (an `integer` that is not a
whole number, a `real` that is no number, a `date` that is no date) is
no value of that kind, as if its key were absent; the file is still
read. (Found on a real export: MusicBee writes a tempo with a fraction
as `<integer>94.034</integer>`, and the whole file was refused for it.)

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
| `Love` | string | `love`: the text, without the space round it (MusicBee writes `L`; `B` is a ban). Any other type: empty |
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

`rating` and `love` are passed along untouched for the caller, which
checks them and does not write them (I-CHECK).

The plan is made for a library of tens of thousands of tracks: an
imported track is found by looking its path up, not by walking the
library.

Each imported playlist becomes a list of library paths, in order, with
repeats kept; items whose track was not found are counted in `missing`.
Every playlist is reported, even when empty.

## I-CHECK: ratings and Love are checked, not brought

(Seen in a real export of 16,797 tracks, 2026-11-17: every one of its
77 loves and every one of its ratings above none was already in the
music file's own tag, where MusicBee had written it and where
linuxBee reads it. A rating of `0` in the export was a file with no
rating.)

The library holds no rating and no Love of its own: it shows what it
read from the file, and reads it again when the file changes. So the
import **writes neither**, to the library or to a file. For every
matched track it compares what the export says with what the library
has (`Rating` and `Love` among the track's fields), each of the two
on its own:

| | |
|---|---|
| the same | both have one, and they are equal (ratings as numbers; Love as text) |
| differ | both have one, and they are not |
| only in the file given | the export has one, the library none |
| only in the library | the library has one, the export none |

A track where neither has one is in no row.

## I-CLI

`linuxbeectl library --db FILE import EXPORT [--map OLD=NEW]... [--apply] [--list]`

- `--map OLD=NEW` is one pair of I-MAP, cut at the first `=`; given
  several times, they are tried in the order given. `--map Z:=/` is
  the whole machine as Wine shows it. A pair with nothing before the
  `=`, or no `=`, is a usage mistake.
- **Without `--apply` nothing is changed.** With it, the matches whose
  `changes` is true are stored by `Library::mergeStatistics`
  (library.md, L-STATS): one change, all or none.
- A track the library does not have is never added.

Standard output, these six lines first, in this order:

```
tracks in the file: N
in the library: N
not in the library: N
would change: N
ratings: N the same, N differ, N only in the file given, N only in the library
love: N the same, N differ, N only in the file given, N only in the library
```

With `--apply` the fourth line is `changed: N`, the number the library
changed. Then, each only when it has an entry, a heading and its
entries, two spaces in:

| Heading | An entry |
|---|---|
| `ratings that differ:` | `PATH: 5 in the file given, 2 in the library` |
| `ratings only in the file given:` | `PATH: 2.5` |
| `love that differs:` | `PATH: L in the file given, B in the library` |
| `love only in the file given:` | `PATH: L` |
| `not in the library:` | `PATH` (the mapped path) |

A rating is written as stars, without a needless `.0`. Of each list
the first ten are shown, then `  and N more (--list shows all)`;
`--list` shows all of every list.

Standard error, without changing the exit status: for each pair that
fitted no track, `linuxbeectl: --map OLD=NEW fits no track in the
file`; and when no track of the file is in the library, a line that
names `--map` and shows how the paths in the file begin.

Exit 0 when it ran. Exit 2 for a usage mistake (no export, more than
one, a bad `--map`, no `--db`), with nothing on standard output and
nothing done. Exit 3, with the reason on standard error and nothing on
standard output, when the export cannot be read or is not such a file,
or the library cannot be opened. Exit 3 also when the library refused
the change: `linuxbeectl: the import was not stored: ` and the
library's words; the first three lines and the two checks may have
been printed, the fourth line is not, and nothing was stored.

## Not in this version

- Playlists: the plan has them; storing them is not done yet.
- A rating or Love held by the library apart from the file.
- Tags, artwork, the equaliser and anything else in the file.
- Reading MusicBee's own library, playlist or settings files.
- Playlist folders: playlists come in flat.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| I-PARSE | A real export: which keys are there? Is `Date Added` written, is `Skip Count`, are half-star ratings? |
| I-LOCATION | The form of `Location` MusicBee writes (`file://localhost/C:/...`?). |

### Answered from the MusicBee wiki (checked 2026-11-20)

| Rule | Question | What the wiki says |
|---|---|---|
| I-PARSE | Are auto-playlists exported, and how are they marked? | Yes, as plain playlists: "How to export MB library to iTunes" says every auto-playlist and static playlist comes in as a static playlist, and the Inbox as a playlist named inbox. |
