# The library

The library knows which files the user has, what their tags say, and the
things only linuxBee knows about them: when each was added, how often it
was played, which category it is in. It is one SQLite file.

This document is the contract for `src/core/library/library.h` and for
`linuxbeectl library`. The locked tests `tests/locked/tst_library.cpp` and
`tests/locked/cli_library.sh` check it on copies of the fixtures in
`tests/data/tags/`.

It builds on `docs/spec/tag-reading.md` (what is read from a file),
`docs/spec/track-fields.md` (fields for templates) and the list of music
files from `docs/spec/organise.md` (O-MUSIC).

## Sources

| Tag | Source |
|---|---|
| W-L | MusicBee wiki, "Library Preferences" and "Inbox": https://musicbee.fandom.com/wiki/Library_Preferences , https://musicbee.fandom.com/wiki/Inbox |
| L-… | A linuxBee decision. |

Pages were read on 2026-11-13.

What MusicBee documents (W-L): a library is a list of files with their
tags cached; the nodes Music, Inbox, Audiobooks, Podcasts and Videos; files
whose location is lost are shown as missing and can be relocated; monitored
folders are scanned for new files; statistics such as play count, skip
count, last played and date added are kept per file. How MusicBee stores
its library is not documented and is not copied: the file format here is
linuxBee's own.

## 1. The file

### L-OPEN

`Library::open(path)` opens the file, creating it and its tables when it
does not exist. The folder must exist already. `":memory:"` gives a library
that is never written to disk. Several libraries can be open at once.

Open fails, with a message and without changing the file, when the file
cannot be opened or created, is not an SQLite database, or has a schema
newer than this program's (L-SCHEMA).

Everything the library holds survives closing and opening again.

### L-SCHEMA

The schema version is SQLite's `user_version`. This version is 1. A file
with a lower version is brought up to date when opened; a file with a
higher version is refused.

The table layout is the implementation's own business. Nothing outside
`src/core/library/` may read the file directly.

### L-STORE

For each track the library stores what `readTags` returned: `fields`,
`other`, `unsupported`, `format`, `tagTypes` and `audio`, exactly, plus the
number of pictures. Picture data is not stored; `LibraryTrack::tags.pictures`
is always empty.

It also stores the clean absolute path (unique), the file's size and
modification time when it was last read, and the library's own facts:
category, date added, play count, skip count, last played, missing.

All times are stored and returned in UTC, to the second.

### L-CATEGORY

The categories are, in this order: `Music`, `Inbox`, `Audiobooks`,
`Podcasts`, `Videos` (W-L). Every track is in exactly one. A function given
any other name does nothing.

## 2. Getting files in

### L-SCAN

`scan(folders, options)` looks at every music file (O-MUSIC, recursive) in
the folders, each file once, in ascending order of path.

- A file the library does not know is read and added with `options.category`
  and `dateAdded = options.now`. If it cannot be read as audio, its path
  goes into `notAudio` and nothing is stored.
- A known file whose size and modification time are as stored is
  `unchanged` and is not read.
- A known file that differs is read again (`updated`). Its id, category,
  date added and statistics stay.
- A known track marked missing whose file is there is `found` (and read
  again if it differs). It is not also counted as unchanged or updated.

After the files: every known track that is not missing, whose path lies
inside one of the scanned folders, and whose file no longer exists is
marked missing (`nowMissing`). Tracks outside the scanned folders are not
looked at. This step is left out when the scan was cancelled.

Before each file the progress function is called with the number of files
already looked at and the total; returning false stops the scan with
`cancelled` true. What was stored stays.

A scan with an invalid `now` or an unknown category does nothing.

Ids in `added` are in the order added. Ids are positive, grow, and are
never used again, even after `remove`.

### L-NAMES

A file or folder whose name is not valid UTF-8 cannot be opened by
linuxBee (every path in it is text). A scan does not pretend such names
are not there: it counts them.

- `unreadableNames` is the number of entries, files and folders alike and
  whatever their ending, inside the scanned folders whose own name is not
  valid UTF-8. Such a folder is counted once and not gone into.
- `unreadableIn` is the folders holding them, each once, in ascending
  order of path.
- Each entry is counted once, however often its folder was named or
  reached. Nothing is stored for these entries and they are not in
  `notAudio`.
- A cancelled scan reports none (it did not finish looking).

Finding them needs the system's own directory calls, since Qt's listing
leaves such names out without a word.

### L-ADD

`addFile(path, category, now)` adds one file whatever its extension, and
returns its id. A file already in the library returns its id and changes
nothing. A file that is not there or not audio, an unknown category or an
invalid `now` returns 0.

### L-MISSING

A missing track keeps everything: tags, statistics, id. It comes back by a
scan that finds the file, or by `setPath` / `relinkPaths` to where the file
now is. `setPath` and `relinkPaths` set `missing` by whether a file exists
at the new path.

### L-COUNT

`categoryCounts()` answers, for every name of `categories()`, how many
tracks the library holds in it (missing ones included); a section with
none is there with 0. The numbers add up to `count()`. It asks the store
to count and reads no track.

`tracksRead()` is how many tracks this library object has handed out
since it was opened: each track in an answer of `tracks()`, and each
answer of `track()` and `trackByPath()` that found one, counts one,
whoever asked. Counting (`count()`, `categoryCounts()`) adds nothing.
It exists so that a test, or a person timing the program, can see how
much a part reads; nothing in the program decides anything by it.

## 3. Changing what is known

### L-EDIT

- `remove(ids)` forgets tracks. No file is touched.
- `setCategory(ids, category)` moves tracks to a category.
- `refresh(id)` reads the track's file again now. False, and no change,
  when the id is unknown or the file cannot be read.
- `setPath(id, newPath)` records that the file moved. False when the id is
  unknown or another track has that path.
- `relinkPaths(oldPrefix, newPrefix)` replaces the folder `oldPrefix` by
  `newPrefix` at the start of every path under it. Prefixes are folders: a
  trailing `/` is ignored, and `/music/a` does not match `/music/ab/x.mp3`.
  A track whose new path belongs to another track is left alone. An empty
  prefix does nothing.

`remove`, `setCategory` and `relinkPaths` return the number of tracks they
changed; an id that is unknown, or already as asked, does not count.

### L-STATS

`recordPlay(id, when)` adds one to the play count and sets last played to
`when` if that is later than what is stored. `recordSkip(id, when)` adds one
to the skip count and changes nothing else. Both return false, changing
nothing, for an unknown id or an invalid time.

`setStatistics(id, statistics)` replaces date added, play count, skip
count and last played in one go (an invalid last played means never
played). It is for imports. It returns false, changing nothing, for an
unknown id, an invalid date added or a negative count.

### L-SIGNALS

Each call that changes tracks emits at most one signal of each kind, after
the change is stored, carrying the ids: `tracksAdded`, `tracksRemoved`,
`tracksUpdated` (for anything else that changed about a track, including
becoming missing or found). A call that changes nothing emits nothing.

## 4. Fields

### L-FIELDS

`LibraryFieldSource` gives a library track to a template or a rule. Names
ignore case. It answers every name `TrackFieldSource` answers (built from
the track's stored tags and path), and these:

| Name | Value |
|---|---|
| Date Added | `yyyy-MM-ddTHH:mm:ss`, UTC |
| Date Modified | the file's modification time, same form |
| Last Played | same form; blank when never played |
| Play Count | a whole number |
| Skip Count | a whole number |
| Category | the category name |
| Size | megabytes (1,048,576 bytes) to one decimal and ` MB`: `5.5 MB` |

`libraryFieldNames()` returns these seven names in this order.

## 5. Command line

### L-CLI

```
linuxbeectl library --db FILE count
linuxbeectl library --db FILE scan [--category C] [--now WHEN] FOLDER...
linuxbeectl library --db FILE list [--category C] [--format TEMPLATE]
```

- `count` prints the number of tracks, missing ones included.
- `scan` prints one line:
  `added A, updated U, unchanged N, missing M, found again F, not music X`.
  `--now` is an ISO 8601 date and time; without it the current time is used.
  `--category` defaults to `Music`.
  When the scan passed over names (L-NAMES) it prints after that line,
  also on standard output:

  ```
  passed over N names that are not valid UTF-8, in:
    FOLDER
    FOLDER
  ```

  with `1 name that is` for one, and each folder indented by two spaces.
  The exit status does not change.
- `list` prints one line per track in ascending order of path: the path,
  or with `--format` the template evaluated on the track's
  `LibraryFieldSource`.
- Exit 0 on success. Exit 2, doing nothing, for a usage mistake: no `--db`,
  no or an unknown subcommand, `scan` without a folder, an extra argument,
  an unknown category, a `--now` that is not a date, a template that does
  not parse (`error at N: message`). Exit 3, with a message on standard
  error, when the library file cannot be opened.

## Not in this version

- Queries. Callers get the whole list and filter it themselves (with the
  rules engine); a library of 100,000 tracks is small enough for that. A
  query layer comes only if a measured need appears.
- Monitored folders and automatic scans: the application drives `scan`.
- Playlists, the play history and the play queue.
- Custom tags and virtual tags defined in preferences.
- More than one library file open in one window.
- Reading a MusicBee library file. Import goes through an iTunes-format
  XML export.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| L-SCAN | Does a rescan re-read a file whose modification time changed but whose size did not? linuxBee: yes. |
| L-MISSING | Does MusicBee keep a missing file's statistics when it is relocated? linuxBee: yes. |
| L-STATS | Does a skip change "last played"? linuxBee: no. |
| L-FIELDS | How MusicBee writes Date Added and Size in a template. linuxBee: ISO 8601 UTC, and megabytes to one decimal. |
