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

A library whose folder cannot be written to does not open (its
write-ahead log cannot be made), and the message says that it `cannot
be written` to and names the file. It does not say that the file is
not a linuxBee library.

Known and not yet so (found by the review, 2026-11-17): a read-only
*file* in a folder that can be written to opens, and every change is
then refused and said; a read-only mount and a full disk at opening
are still answered with "not a linuxBee library". What open says in
each of these is a package of its own, judged by the database's error
code and not by its words. Opening a library to read only is not in
this version.

Everything the library holds survives closing and opening again.

**The file is shared.** Another program may have it open: `linuxbeectl`
beside the window, or a second start. (Found by an outside review of
the code, 2026-11-16, and measured before this was written.)

- A library on disk keeps its journal as a write-ahead log (SQLite's
  `journal_mode=WAL`, with the default `synchronous`), so a program
  that is reading, however long, never holds up or fails a change.
  While a library is open there are two more files beside it
  (`-wal`, `-shm`); when the last program closes it, it is one file
  again. A copy of a library is taken while it is closed. Where the
  file system cannot do this (some network disks) the library opens
  all the same, with the journal it had.
- Two programs opening a library that does not exist yet both get a
  library: the tables are made by whichever is first, under the
  write lock, and the other looks again once it has the lock and
  finds them made.

### L-SCHEMA

The schema version is SQLite's `user_version`. This version is 1. A file
with a lower version is brought up to date when opened; a file with a
higher version is refused.

A later version of the schema comes with its own step from the one
before it, added with the first change that needs it. A step runs with
the write lock held and in one transaction with the new
`user_version`, so a file is at one version or the next, never
between, and two programs started together do not both run it
(L-OPEN). Nothing here needs version 2 yet, so there is no step.

A step from one version to the next is added with the first version
that needs one: it runs when the file is opened, under the write lock
and in one transaction, so that a file is at one version or the
other and never between. Version 1 has none to run.

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

### L-PARTS

A scan is two kinds of work: looking at files, which is slow and needs
no library, and storing what was found, which is quick and must happen
on the library's own thread. The three functions below are those two
kinds apart, so that a window can do the slow kind on another thread.

- `scanKnown()` is what the looking needs to know: for every track of
  the library, by its stored path, its id, size, modification time and
  whether it is marked missing. It reads no track (L-COUNT).
- `findForScan(folders, known, options)` looks at the files as L-SCAN
  says, and at the names as L-NAMES says, against `known`. It touches no
  library and may be called on any thread. It answers `ScanFindings`:
  the files not known, read (`fresh`); the known files that differ, read
  again (`reread`, each with the id and whether the track was marked
  missing); the ids marked missing whose file is there (`found`); the
  ids not missing, inside the folders, whose file has gone
  (`nowMissing`); the count `unchanged`; `notAudio`; the unreadable
  names; and `cancelled`. `options.progress` is called and obeyed as in
  L-SCAN.
- `applyScan(findings, options)` stores them and answers the report:
  each `fresh` file is added with `options.category` and `options.now`
  (one whose path the library holds by now is left alone); each `reread`
  replaces the reading and counts as `updated` unless the track was
  marked missing; `found` and `nowMissing` set the mark. It refuses what
  `scan` refuses (an invalid `now`, an unknown category) by doing
  nothing, and it signals as L-SIGNALS says: at most one signal of each
  kind for one call.

`scan(folders, options)` is, in its report, in what the library holds
afterwards and in its signals, exactly `applyScan` of what `findForScan`
finds for the folders against `scanKnown()`, with `options.progress` as
its progress function. One thing differs: a scan that would be refused
(an invalid `now`, an unknown category) is refused before any file is
looked at, and its progress function is never called.

The order of ids. In the findings and in the report, `nowMissing` is in
ascending order of path, and `fresh`, `reread` and `found` are in the
order the files were looked at. `tracksAdded` names the ids of the
report's `added`; `tracksUpdated` names `found`, then `updated`, then
`nowMissing`.

**A library that changed meanwhile.** The looking takes time, and the
library can change before what was found is stored. So the findings
carry, in `judged`, for each id they name in `reread`, `found` and
`nowMissing`, what `known` held of that track: its path, size,
modification time and missing mark. `applyScan` acts on such a finding
only while the library still holds a track of that id with those four
values; otherwise the finding is passed over: nothing is stored for it,
its id is in no list of the report and in no signal, and the report's
`passedOver` counts it once. The track is left for the next scan. A
finding whose id has no entry in `judged` is acted on as before. The
library is asked for the four values by id, without reading the
track. In parts, each part carries the entries its own findings need.
`addedUp` adds `passedOver` up.

This is what keeps a track that `setPath` moved during a scan from
being marked missing, and a reading made before `refresh` from
replacing the later one.

**In parts.** With `options.partSize` above 0 and `options.part` set,
`findForScan` hands `part` what it found each time it has looked at
`partSize` more files, and starts afresh; it answers what is left: the
findings since the last part, and with them `nowMissing`, the unreadable
names and `cancelled`, which no earlier part carries. Applying the parts
in order and then the answer stores what one `applyScan` of the whole
would; the reports add up to its report (`addedUp(a, b)`: the lists of
`a` then of `b`, the counts added, `cancelled` if either). Without a
`part` function, or with a size of 0, nothing is handed over early.

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
none is there with 0, and a category the store holds that is not one of
those names is there too, with its count. So the numbers add up to
`count()`. It asks the store
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

### L-MANY

(Found by an outside review of the code, 2026-11-16: one expression
naming every id is refused by the database from 1,000 or 10,000 ids
on, and the call then did nothing and said nothing.)

The number of ids given to `remove` and `setCategory` has no limit:
20,000 work as 2 do. The database is asked in pieces. The whole call
is one transaction: either every piece is stored or none is.

Inside a batch that is open already, the call undoes its own pieces
and leaves the batch's earlier work. Known and left: a failure that
makes the database end the whole transaction (a full disk, an I/O
error) takes the batch's earlier work with it. The only batch today
is the scan's, and it does not call these.

### L-WAIT

Only one program writes at a time. Every change the library makes
takes the write lock **at its start** and waits for it up to 5
seconds (`setWriteWait` sets another length; tests use a short one).
It never reads first and asks for the lock afterwards: SQLite
refuses that at once, without waiting, when another program is
writing, which is how a change used to be refused for no reason.
This holds for `remove`, `setCategory`, the storing step of a scan,
and making a new library's tables. A change that is one statement
waits by itself.

A change that has waited that long is refused whole (L-FAILED).

### L-FAILED

When the database refuses a `remove` or a `setCategory` (it is held
by another program, the disk is full), nothing is changed, the call
returns 0, no change signal is sent, and `failed(what)` is emitted
once, with the database's own words. A call that merely had nothing
to do does not emit it.

**Every change, not those two alone.** (Found by the reviews of fix 60:
`relinkPaths` passed over a refused step and went on, and the other
calls answered a refusal as they answer "no such track", so a play
that could not be counted was lost without a word.) `refresh`,
`setPath`, `relinkPaths`, `recordPlay`, `recordSkip`, `setStatistics`
and `addFile` behave alike: when the database refuses, nothing is
changed, the call answers as it does when it did nothing (`false`, 0;
`addFile` 0), no change signal is sent, and `failed` is emitted once
with the database's words. A call that had nothing to do (an unknown
id, a file that cannot be read, a path that is another track's, a
track already there) emits nothing, whatever an earlier call was
refused for.

**Nothing to do is found out before the lock is asked for.** (Found by
the review of this rule's first build: a play for an unknown id
waited out another program's lock and then said `failed`.) A call
with nothing to do neither waits for another program nor says
anything, whoever holds the library: an unknown id, an empty list, a
list of ids none of which is known, tracks all in the section
already, a move that names no track of ours. This holds for `remove`,
`setCategory` and `recordMoves` as for the calls named above. A call
with something to do for even one track is a change, and is refused
whole and said when the library is held.

`relinkPaths` is one change, as `recordMoves` is: it takes the write
lock first (L-WAIT), the first refused step ends it, nothing of it is
stored, and the same call made again later moves every track. A
library with half a folder at the new place is what this is against.

The storing step of a scan (`applyScan`, and so `scan`) is such a
change. When it cannot begin, or cannot be committed, nothing of it
is stored, the report names no track, its report says why in `refused`,
`failed` is emitted once with the same words, and no change signal is
sent. The same scan run again later stores what it finds then.

**A refusal partway.** (Found by the review of fix 60, with a file-size
limit standing in for a full disk: the scan's storing skipped a write
the database refused and went on. The database had ended the
transaction itself, so each write after that was stored on its own:
21 tracks were stored and the report said none.) The storing of a
scan is one change. The first write the database refuses ends it: no
statement runs after the refusal, the transaction is given up,
nothing of the scan is stored, the report names no track and says
why in `refused`, `failed` is emitted once with the same words, and
no change signal goes out. A write that had nothing to do (the track
is gone by now) is not a refusal. The same scan made again later
stores everything it finds.

### L-MOVED

(Found by an outside review of the code, 2026-11-16: organised files
fell out of every playlist and the queue, and no rule said otherwise.)

**A file the program moves keeps its place everywhere the program
holds its path.** The library is where a move is recorded, so the
library says it: `pathsMoved(oldToNew)`, old path to new path, for
every track whose path a call changed.

- `setPath` emits it for its one track, when the path changed.
- `recordMoves(moves)` takes file moves, `{from, to}`, **in the order
  they happened**, and does to the tracks what was done to the files,
  one move after the other:
  - the track that is at `to` at that moment goes (its file was
    replaced), unless it is the very track being moved;
  - the track that is at `from` at that moment, if there is one, is
    at `to` from then on, with everything the library knows of it.
  So a chain is followed: `c` to `a`, then `a` to `n`, leaves the
  track of `c` at `n` and removes the track that was at `a`; in the
  other order both tracks stay. A move whose `from` and `to` are the
  same is nothing.
  When all are done, each track that was moved is missing or not by
  whether a file is at its path now. The call is one transaction. It
  emits at most one `tracksRemoved`, one `tracksUpdated` and one
  `pathsMoved`, in that order; `pathsMoved` maps the path each
  remaining track had before the call to the one it has after, and
  leaves out a track that ended where it began. It returns the size
  of that map.
  (A first version took `{id, newPath}` pairs after removing every
  target's track; a chain then lost the moved track. It is gone.)
  **Whole or not at all.** (Found by the review of the first version:
  a step the database refused was skipped and the run went on, so a
  later move took the wrong track along and its play counts ended on
  another file.) If any step is refused (a removal, a move, the
  beginning or the end of the transaction), the call stops at once,
  nothing of it is stored, it returns 0, sends no change signal, and
  emits `failed` once (L-FAILED). It does not go on after a refusal:
  the database may have ended the transaction itself, and what
  followed would be stored step by step. The library is then as it
  was before the call, and the same call made again later records
  the run.
  Known and left: until then the library is behind the disk. The
  moved tracks show as missing after a scan, and their files as new.
- `relinkPaths` emits one, with every path it changed.

A call that changed no path does not emit it. It comes after the
change is stored and after `tracksUpdated`.

Not covered: a file moved by another program. The scan sees a file
that is gone and a new one.

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
- A scan that could not be stored (L-FAILED) prints no result line. On
  standard error: `linuxbeectl: the scan was not stored: ` and the
  database's words. Exit 3.
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
