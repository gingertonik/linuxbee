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
| W-L | MusicBee wiki, "Library Preferences" and "Navigator" (the Inbox): https://musicbee.fandom.com/wiki/Library_Preferences , https://musicbee.fandom.com/wiki/Navigator#Inbox |
| L-… | A linuxBee decision. |
| W-ANS | MusicBee wiki, the pages that answer the questions under "Answered from the MusicBee wiki": "Special Fields": https://musicbee.fandom.com/wiki/Special_Fields |

Pages were read on 2026-11-13.

What MusicBee documents (W-L): a library is a list of files with their
tags cached; the nodes Music, Inbox, Audiobooks, Podcasts and Videos; files
whose location is lost are shown as missing and can be relocated; monitored
folders are scanned for new files; statistics such as play count, skip
count, last played and date added are kept per file. How MusicBee stores
its library is not documented and is not copied: the file format here is
linuxBee's own.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Library Preferences" (3.0.5976;
2017-10-12); "Navigator" (3.0.5918; 2020-07-30).

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
code and not by its words. A command that only reads opens the library
to read only (L-READ opens it so).

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

### L-READ

(The owner's ruling of 2026-11-20: a command that only reads refuses an
older library and says why; only a writing command brings it up to
date. This reverses the departure that let every command step it.)

What opens the library, and how:

| Opens the library | Kind |
|---|---|
| `linuxbeectl library count` | reads |
| `linuxbeectl library list`, with or without `--category` and `--format` | reads |
| `linuxbeectl library import EXPORT` without `--apply` (with or without `--list`) | reads |
| `linuxbeectl library import EXPORT --apply` | writes |
| `linuxbeectl library scan` | writes |
| the window, at start | writes |

No other command opens the library.

A reading command opens the file through the database's read-only
mode. It never writes to the library file:

- **No file.** When there is no file at the path, nothing is made; the
  reason is `there is no such file`.
- **An older schema** is not brought up to date and no copy is made;
  the reason is `it is at schema N and this linuxBee reads schema V. A
  command that only reads does not bring it up to date; library scan,
  library import --apply or the window does, after a copy.` A newer
  schema is refused as for every command (L-SCHEMA).
- No switch to a write-ahead log, no write lock, and rows deleted long
  ago are not taken away (L-DELETED): that is the next writer's.
- **Beside a writer.** Another program may have the library open and
  be writing it: the reading command reads what was committed, and an
  open transaction does not hold it up. (Not `immutable`: that is
  unsafe while another program writes.)
- **The side files.** The bytes of the library file are unchanged by
  a read. The database may leave its two side files beside a closed
  library, `-wal` (empty) and `-shm`: they are its normal files, and
  the next writer to close last (the window, a scan) takes them away.
  The program never removes them itself (the orchestrator, 2026-11-21:
  removing them rests on the database's private locks, and is the one
  act that could harm a library in use).
- **The wait.** A library another program holds (a writer with the
  whole file locked, a write-ahead log being recovered) is waited for
  as L-WAIT waits, up to 5 seconds, and then read. Still held then:
  `the library is busy: another program holds it; try again`.
- **Its words.** (Found by the review of the first building.) A file
  with no `tracks` table, an empty file or another program's database
  at any version, is `That file is not a linuxBee library: it holds no
  library`, not "at schema 0". A folder that cannot be written to,
  with no `-shm` file beside the library, is `the library cannot be
  read here: its folder cannot be written to, and the database needs
  its side files (-wal, -shm) beside it`: the database needs that file
  to read a library kept with a write-ahead log. A disk mounted read-only, with no
  `-shm` file beside the library, is `the library cannot be read here:
  the disk it is on is mounted read-only, and the database needs its
  side files (-wal, -shm) beside it`, not "not a linuxBee library"; with
  them beside it, it is read (fix-d2; 70d's review, note 2). It is not
  read as `immutable`, for the reason above.

Each refusal is exit 3 on the command line, the code every library
that cannot be opened gets, newer schemas included, so a script sees
one code for "this library cannot be read as it is".

### L-SCHEMA

The schema version is SQLite's `user_version`. This version is 3. A file
with a lower version is brought up to date when a writing command (L-READ)
opens it; a command that only reads refuses it. A file with a higher
version is refused by every command.

A later version of the schema comes with its own step from the one
before it, added with the first change that needs it. A step runs with
the write lock held and in one transaction with the new
`user_version`, so a file is at one version or the next, never
between, and two programs started together do not both run it
(L-OPEN).

**A copy before a step.** (Audit of the library, F7. The owner,
2026-11-19: a step may run without a question, after a whole copy,
with one line of notice.) A step changes the file for good, and an
older program then refuses it. So the library as it was is kept.

- A copy is made when a step is to run on a file that holds a schema
  of 1 or more, and then only. A new file and a library in memory
  have nothing to copy. One opening makes one copy, however many
  steps it runs.
- **When.** With the write lock held and the version looked at again,
  before the first step. So no change can come between the copy and
  the step, and of several programs started together one makes a
  copy.
- **How.** By the database itself (`VACUUM INTO`), on a second
  connection, which the lock does not hold up. The copy is one file
  and holds everything committed, also what is still in the
  write-ahead log. It is not a copy of the library's bytes: a file
  copied beside its log would lack what the log holds.
- **Where, and its name.** In the library's own folder, under the
  library's file name with `.schema-N` after it, N being the version
  found: `library.db.schema-1`. A name that is taken, by a file, a
  folder or a link, is never written over: the copy is then
  `.schema-N-2`, `.schema-N-3` and so on.
- **Whole.** The copy is made under a spare name (`.lbcopy-` and six
  characters, in that folder) and gets its name only when the
  database said it was made, it opens as a database, its version is
  N and it holds as many tracks as the library. It is flushed to the
  disk before it gets its name.
- **Whose.** (Found by the review of the first building: a library
  kept for its owner alone got a copy everyone could read.) The spare
  file is made by this program, new, for its owner alone (0600), and
  the database writes into that file. Before it gets its name it is
  given the mode of the library's file. So no one can read the copy
  who cannot read the library, at any moment.
- **Kept.** When the copy has its name, the folder is flushed to the
  disk, before the first step, so that a power cut after the step
  cannot leave a library of the new version beside no copy. Where the
  disk knows no such flush (`EINVAL`, `ENOTSUP`) this is passed over.
  Any other failure is as when no copy could be made: the copy is
  taken away, no step runs, and the reason is `its folder could not
  be flushed to the disk`. No test can cut the power: this is checked
  by reading the code.
- A library opened through a link: the copy lies beside the link,
  under the link's name, and is a file of its own.
- **When no copy can be made, no step runs.** The library does not
  open. The message is `The library was not brought up to date: no
  copy of it could be made in FOLDER (REASON)`. The file is at the
  version it had, and the spare file is taken away. The copy needs
  as much room as the library.
- When the step fails after the copy was made, the copy is taken
  away again: a copy lies only beside a library whose step ran.
- The program never opens, reads or removes a copy afterwards. That
  is the person's.
- **What is said.** `Library::stepAtOpening()` gives the version
  found, the version now and the place of the copy; all empty when
  no step ran. `schemaStepNotice()` makes the one line of it: `The
  library was brought up to date (schema 1 to 2). A copy of it as it
  was before: PLACE`. `linuxbeectl library` writes that line to
  standard error after `linuxbeectl: `; what it writes to standard
  output does not change. The application writes it to standard
  error after `linuxbee: ` and shows it in the status bar of the
  window. No question is asked.

Known and left:

- A run that is killed between the naming of the copy and the end of
  the step leaves the copy beside a library of the old version; the
  next opening makes another, under the next name. A run killed
  while the copy is made leaves a `.lbcopy-` file, which is not
  swept.
- The line in the status bar is replaced by the next thing the
  window says. Where the media keys cannot be had the window says so
  at once, and the line is not seen. It is on standard error always.
- When no copy could be made the library's rows and version are as
  they were; its first bytes may differ (the switch to a write-ahead
  log comes before the step).

**Version 2** holds, for each track, the number of the reader that
read its file (L-SCAN, "the reader's number"). The step from 1 gives
every track the number 0, which is lower than any reader's: nothing
else of a track changes, and no file is read. While another program
holds the write lock the step cannot run: the library does not open,
says why, and is left at version 1. Several programs that open a
version 1 file together all open it; one of them ran the step. A
program of version 1 refuses a file of version 2, as any older
program refuses a newer file.

**Version 3** holds, for each track, when its file was deleted
(L-DELETED): whole seconds, and -1 for a track that was not. The step
from 2 gives every track -1 and changes nothing else; from 1, the
step to 2 and this one run in the one transaction. The copy made
before (below) has the name of the version found. A program of
version 2 refuses the file afterwards.

A step from one version to the next is added with the first version
that needs one: it runs when the file is opened, under the write lock
and in one transaction, so that a file is at one version or the
other and never between.

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
- A known file whose size and modification time are as stored, and
  which was read by this reader or a later one, is `unchanged` and is
  not read.
- **The reader's number.** (Found 2026-11-17: a library scanned
  before this program read Love showed no heart on any track, and
  no scan could bring one, since every file was as stored.) What the
  reader makes of a file changes when the program learns a tag. The
  reader has a number, `kReaderVersion`, raised by one with each such
  change (it is 3; 1 read no Love, and 2 read some Ogg files a page
  short, R-AUDIO), and the library holds for each
  track the number of the reader that read it, stored with every
  reading: a scan, `addFile`, `refresh`. A known file as stored that
  was read by an older reader is read again. If what is read is what
  the library holds (the fields, the properties of the sound, the
  count of pictures) the track is `unchanged`, no signal names it,
  and the reader's number is stored so that it is not read a third
  time; if it differs, it is `updated`. So the first scan after such
  a change reads every file, once, and names only what changed.
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
  the library, by its stored path, its id, size, modification time,
  whether it is marked missing, and the number of the reader that read
  it (`reader`; an entry made by hand has this reader's). It reads no
  track (L-COUNT).
- `findForScan(folders, known, options)` looks at the files as L-SCAN
  says, and at the names as L-NAMES says, against `known`. It touches no
  library and may be called on any thread. It answers `ScanFindings`:
  the files not known, read (`fresh`); the known files that differ, read
  again (`reread`, each with the id, whether the track was marked
  missing, and `onlyTheReader`: the file is as stored and was read
  again for the reader's number alone); the ids marked missing whose file is there (`found`); the
  ids not missing, inside the folders, whose file has gone
  (`nowMissing`); the count `unchanged`; `notAudio`; the unreadable
  names; and `cancelled`. `options.progress` is called and obeyed as in
  L-SCAN.
- `applyScan(findings, options)` stores them and answers the report:
  each `fresh` file is added with `options.category` and `options.now`
  (one whose path the library holds by now is left alone); each `reread`
  replaces the reading and counts as `updated` unless the track was
  marked missing, or it is `onlyTheReader` and reads as the library
  holds it, which counts as `unchanged`; `found` and `nowMissing` set the mark. It refuses what
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
report's `added`; `tracksUpdated` names `found`, then `updated`,
then `followed`, then `nowMissing`. (L-FOLLOW adds `maybeMoved` to the findings and
`followed` to the report.)

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

### L-FOLLOW: a scan follows a file that another program moved

(Found by the write-path audit, part 5, F1, 2026-11-18. A file renamed
or moved by any other hand was a track gone and a track new. The
plays, the skips, the last play and the date added stayed on a row
marked missing, and nothing in the program could bring them to the
file.)

**The looking** (`findForScan`).

- What has gone (L-SCAN, "after the files") is found before the
  files are looked at. It is reported as before: `nowMissing`, in the
  last part, and not when the scan was cancelled.
- The rows a file may be are every track marked missing **at whose
  path nothing is**, wherever its path lies, and every track just
  found gone. That is asked of the disk, once for each track marked
  missing, whether or not its folder is scanned. (Found by the
  review of this rule's first build, 2026-11-19: a track marked
  missing whose file was back at its own path was such a row. A copy
  alike in size, time and tags took it, in one call and not in parts
  of one; the plays and the playlists went to the copy, and the file
  that was the track came back as a new one.) Such a track is found
  again by the scan of its folder, as before (L-SCAN).
- A file the library does not know, whose size and modification time
  (to the second, as stored) are those of one or more such rows, is
  **held back**. It is read as any new file is, and it is not in
  `fresh`: it is in `maybeMoved`, with the ids of those rows in
  ascending order. One that cannot be read as audio is in `notAudio`
  as before. `judged` carries what was known of each row named.
- `maybeMoved` is carried by the last part only, in the order the
  files were looked at. A cancelled scan carries none: nothing is
  stored for those files, not a new track either, and the next scan
  sees them again.
- Every other new file is `fresh`, as before. A scan in which no new
  file has the size and time of a missing or gone row does what it
  did before.

**The storing** (`applyScan`), after `fresh` and `reread`, before
`nowMissing`.

- For each held-back file, the rows that are its own: those of its
  rows that the library still holds as they were looked at (L-PARTS,
  `judged`), **at whose path nothing is by now**, and whose stored
  reading is the file's reading: the same fields, the same
  properties of the sound, the same count of pictures. A row is
  never moved away from a file that is there.
- **One to one, or not at all.** When a file has exactly one row of
  its own, and no other held-back file of this call has that row as
  its own, the row takes the file over: its path is the file's, it
  is not missing, and its id, its section, its date added and its
  statistics stay. Its id is in `followed`, and in no other list of
  the report: not `found`, not `nowMissing`, not `added`.
- Every other held-back file is added as a new track, as a `fresh`
  file is, after the `fresh` files. Its rows are marked missing, or
  stay so. Two files alike for one row, and two rows alike for one
  file, are such cases: the rule does not guess.
- A file whose path the library holds by now is left alone.
- Whole or not at all, as the scan is (L-FAILED).

**The signals.** `tracksUpdated` names `found`, then `updated`, then
`followed`, then `nowMissing`. After it, when a row was followed, one
`pathsMoved` with the path each such row had and the one it has
(L-MOVED), so that whatever holds the old path follows.

`addedUp` adds `followed` up.

Known and left:

- A file that was changed as well as moved (tagged again, encoded
  again) has another size or time. It is a track gone and a track
  new, as before.
- A track that is not marked missing and lies outside the scanned
  folders is not looked at. A library moved whole is `relinkPaths`.
- A row and a track that an earlier scan already made of its moved
  file are not brought together: the file is known, so it is not
  new.
- No counter holds the cost. The sizes and times come from what the
  scan knows already; the disk is asked once for each track marked
  missing; one track is read from the library for each row of a
  held-back file. With 500 files moved of 5,000 a scan took nine
  times as long as with none (the review, a debug build).
- **A disk that is away.** Its tracks are marked missing (its folder
  is there and empty, or is gone), nothing is at their paths, and copies alike in
  size, time and tags that are found elsewhere take the rows over.
  When the disk is back its files are new tracks. Nothing is lost:
  the plays are on the copies. A scan cannot tell a disk that is
  away from one that was given up for the copy, and following in the
  second case is what this rule is for.
- `linuxbeectl library scan` has no playlists: a file it follows
  keeps its old path in them until the window relinks.
- A track at whose path a link now lies is not followed: something
  is at its path, and the row stays on the link, as before this rule.

### L-LOOKIN

What a scan says of a file that is not there, and of a folder it cannot
look in (fix-d; the re-probe of row 2, N4, and the library audit, F4 to
F6).

- **Gone during the looking.** The tracks whose files have gone are
  found twice: before the files are looked at (L-FOLLOW needs them) and
  again when the looking ends, uncancelled. A track whose file went in
  between is in `nowMissing` too, in the last part. A file counted
  `unchanged` before it went is counted there as well: each count says
  what a file was when it was looked at.
- **Not there is not "not music".** A file listed and gone before it
  was looked at is in no count. Only a file that is there and cannot be
  read as audio is in `notAudio`.
- **Cannot look is not gone.** A track whose file cannot be looked at
  for any reason but "no such file" (no permission on a folder above
  it, a stale network mount, an I/O error) is left as it is: it is not
  marked missing. The nearest folder above it that can be seen is in
  `cannotLookIn`, each folder once, ascending. A folder that cannot be
  looked in and holds no known track is not named: nothing of it is
  known.
- **Held by now.** A `fresh` file whose path the library holds by the
  time it is stored (another writer stored it meanwhile) is left alone
  and counted in `passedOver`.

### L-UNPLUGGED

A library folder that looks unplugged is not looked in, and none of its
tracks is marked missing (fix-m1; the review of D, note 1). An
unmounted network share or USB disk leaves an empty folder, or none, and
a scan that "finds" a whole library gone is worse than one that says it
could not look. A library folder is one the scan is given (`folders`),
not every folder below it.

1. **A mount point inside it, not mounted.** A folder inside the
   library folder that `/etc/fstab` names as a mount point (not `/`),
   with nothing mounted there now (`/proc/self/mountinfo`), no music
   file in it, and known tracks in it (a share mounted under the music
   folder, gone). The table of mounts is read, not device numbers: an
   unmounted mount point is an ordinary folder on its parent's device,
   and `st_dev` cannot tell it from any other without a record of what
   was there, which the library does not keep. `LINUXBEE_FSTAB` names
   another table, and `LINUXBEE_MOUNTINFO` another table of what is
   mounted (the tests use them; fix-m1b). A folder that holds music is
   scanned whatever the tables say: what is there is the truth, and in
   a mount namespace (a sandbox, a container) a mount point of the
   table may not appear though its files do. An unmounted mount point
   at or above a library folder leaves it empty or absent, which rule 2
   covers; so does a disk the desktop mounted (`/run/media/...`), which
   is in no table.
2. **Empty where it held tracks.** More than half of the library
   folder's known tracks (not marked missing) have no file, and it
   holds no music file at all, or is not there. A track with no file
   that a music file found elsewhere by the same scan would follow
   (L-FOLLOW: the same size and time to the second, and not known to
   the library) was moved, not unplugged, and does not count toward the
   half (fix-m1b): a watched inbox emptied into another library folder
   by a file manager is followed.

A library folder inside another library folder is judged on its own:
emptied, it can look unplugged while the folder around it still holds
music (fix-m1b).

With `emptyMeansGone` (the command line's `--empty-means-gone`) neither
rule applies: a folder emptied on purpose has its tracks marked, as
before.

Such a folder is in `unplugged` (each once, ascending), in the last
part only. None of its tracks is in `nowMissing`, no file found
elsewhere follows one of them (L-FOLLOW), and no file in it is looked
at. A folder that lost some files and still holds music is scanned as
before.

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

### L-DELETED: a deleted track's row is kept for thirty days

(The owner, 2026-11-18 and 2026-11-19. Delete Files removed the row
with the file, and with it the plays, the skips and the date added.
A file taken back out of the trash came into the library as a new
track.)

- `markDeleted(ids, when)`: the files of these tracks went to the
  trash at `when`. Each id that is a track is marked missing and
  gets `when` as the time of its deleting, to the second. All of
  them in one change, all or none (L-FAILED, as `remove`), and one
  `tracksRemoved` with those ids in the order given. It returns how
  many. An id that is unknown, or deleted already, is not counted
  and not named, and an earlier time stands. An id given twice is
  one track, named once at its first place. With no id that is a
  track, or a `when` that is no time or lies before 1970, nothing
  happens.
- **A deleted track is no track.** `count`, `categoryCounts`,
  `tracks`, `track` and `trackByPath` do not have it, and every call
  that names its id or its path answers as for one the library does
  not know: nothing changes, nothing is signalled. So it leaves
  every list and every count at once, by the signal every view
  follows already. Its row keeps everything: tags, statistics, id,
  section, date added.
- **It comes back at its own path.** A scan that finds a file at the
  path of a deleted track brings the track back: no longer deleted,
  no longer missing, read again when the file differs from what is
  stored (as L-MISSING). It is in the report's `found`, and
  `tracksAdded` names it (not `tracksUpdated`: every view had let it
  go). `addFile` of such a path does the same and returns the
  track's id; the section given is not used. While the file is not
  there a scan says nothing of the track. In a scan's `tracksAdded`
  the new tracks come first, then those brought back; in `found`,
  the missing tracks found, then those brought back.
- **It is not followed** (L-FOLLOW): a file elsewhere that is alike
  in size, time and tags is a new track. The owner's ruling,
  2026-11-20: a person who deleted a file and kept a copy has two files in mind.
- **Its path is free.** A track that `setPath`, `recordMoves` or
  `relinkPaths` moves to the path of a deleted track takes it: the
  deleted row is removed in the same change. The file now there is
  the other track's.
- **After thirty days the row is removed**
  (`Library::deletedKeptDays()`). `expireDeleted(now)` removes every
  row whose deleting lies thirty times 86,400 seconds before `now`
  or more **and at whose path nothing is** (asked of the disk, once
  for each such row; a link counts as something). It returns how
  many and signals nothing. A `now` that is no time removes nothing.
  A row whose file is there is never removed for its age: the file
  is the track's, taken back out of the trash, and a scan brings
  the track back however long ago it was deleted. (Found by the
  proving of this rule: the window scans in parts, each stored by
  itself, and the first part removed a row whose file was in a
  later one.)
- **When.** Every storing of a scan (`applyScan`, so every part,
  and a scan that was stopped) does it with the scan's `now`, in
  the scan's own change: a refusal there refuses that storing
  (L-FAILED). `Library::open` does it with the clock's time, for a
  file that holds this version's schema: with nothing to remove it
  asks for no lock; a failure or a refusal there is no failure of
  the opening and is not said. A refusal of `expireDeleted` called
  by itself returns 0 and is not said either: nobody asked for it.

Known and left:

- A track brought back is not put back into the playlists or the
  queue it was taken out of when it was deleted.
- The thirty days are counted by the clock of the machine. A clock
  set forward by a month removes the rows at the next opening.
- A play of a track that is deleted while it plays is not counted.
- `relinkPaths` does not move a deleted row: it stays at its old
  path, and a file taken back out of the trash to the folder's new
  place is a new track.
- A deleted row whose file is at its path, in a folder that is never
  scanned, stays for good, and is asked of the disk at every storing
  of a scan and every opening.

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

**A change nobody is waiting for.** (Found by the review of the
status-bar notice: a play that could not be counted held the window
for the whole five seconds.) `recordPlay` and `recordSkip` come from
no command: a track ended. They wait the short wait, a quarter of a
second (`setQuickWait` sets another length), and are then refused as
any change is. The caller keeps what was refused and offers it again
(playback-shell.md, PS-COUNT). So does `applyScan` when
`ScanOptions::nobodyWaits` is set, which the folder monitor's scans
set and a scan the user asked for never does. Every other change
waits as above.

**`recordPlays(records)`** (L-STATS) is how the window counts: plays
and skips, each with the file's path and the time, in the order they
happened. Under the short wait:

1. each path is looked up, once for each different path. A record
   whose file the library does not have, or whose time is not valid,
   is passed over: nothing to do, found out without asking for the
   lock (L-FAILED);
2. the rest are stored in **one change**: all or none. Then one
   `tracksUpdated` names each track changed, once. Nothing to store:
   no change and no signal;
3. it returns what it refused, to be offered again: nothing when all
   was stored or passed over; when the change was refused, the
   records it would have stored, in the order given, with `failed`
   said once; when a look-up **could not be made** (step 1: the
   database was busy, which without a write-ahead log a reader can
   meet), every record with a valid time, with `failed` said once.

**A look that could not be made is not "no such track".**
`recordPlay(id)` and `recordSkip(id)` look the track up under the
short wait too, and when the look could not be made they say `failed`
and return false. (The other readers of the library still take a
failed read for nothing found, and still wait the long wait for it:
on a library without a write-ahead log that another program holds,
the end of a track holds the window for the reads the playback shell
makes then, though the play itself is now kept. Measured while this
was written: two such reads. An audit finding for 0.9.)

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

### L-READFAIL

(Road row 3, the readers audit. A read the database could not answer
was taken for "nothing there": a damaged library read as empty, a
scan's start as "nothing known", a look before a change as "no such
track", so a change went on, or was dropped, with no word.)

A read that the database cannot answer is never taken for "nothing
there".

1. **The readers say it.** The library reads through typed readers
   that give a `LibraryRead<T>` (`library/libraryread.h`; the tag
   reader's own `ReadResult` is another thing): the value, or
   that the read failed, and the database's words. It is
   `[[nodiscard]]` and has no conversion to its value, so a caller
   cannot drop it or use it as the value unnoticed. `value()` of a
   failed read is a fault of the caller (an assertion). **A read that
   fails part way is a failed read**, not a shorter answer: the end of
   the rows is checked as well as the start.
2. **Inside a change, and before one.** A look that a change, or the
   check of whether there is anything to do, could not make refuses
   the change, as a refused write does (L-FAILED): nothing is stored,
   no change signal goes out, the call answers as for a refusal, and
   `failed` is emitted once, with the database's words. This holds for
   `remove`, `markDeleted`, `expireDeleted`, `setCategory`, `refresh`,
   `setPath`, `relinkPaths`, `recordPlay`, `recordSkip`,
   `recordPlays`, `setStatistics`, `recordMoves`, `applyScan` and
   `addFile`. (`expireDeleted` at the opening stays unsaid, as
   L-DELETED has it: the opening is not refused.)
3. **An answer.** The program reads every answer through a typed
   reader: `readCount()`, `readTracks(category)`,
   `readCategoryCounts()`, `readTrack(id)` and `readTrackByPath(path)`.
   The list is all or nothing: a list that fails part way gives no
   rows.
   - **The command line** (L-CLI): `count`, `list` and `import` print
     nothing on standard output, the words on standard error, and exit
     3; `import` plans and writes nothing.
   - **The window keeps what it shows.** A node, the counts, or a
     library change that cannot be read leaves the list and the counts
     as they are. A node that cannot be read says on the status bar
     `Could not show the tracks: the library could not be read`, and
     so does a library change that cannot be read. Keeping is for the
     node on show read again: a node chosen while the library cannot be
     read is shown empty, with the line, so that no other node's tracks
     stand under its name (fix-r3b; the review of R-3, note 2).
   - **A command that needs a look refuses** before it asks or does
     anything, and says so on the status bar each time: Delete Files
     (`Could not delete files: …`), Remove from a playlist (`Could not
     remove from the playlist: …`), the tag editor and the tagging
     tools (`Could not edit the tags: …`), each ending `the library
     could not be read`.
   - **Undo** has already put the files back when its look fails, so
     it cannot be refused. Its message adds `; the library could not
     be read, so it shows the old tags until it can`.
   - **Playback:** a title or a media-keys answer that cannot be read
     is empty, as for a path the library does not know. A gain that
     cannot be read plays at unity and is not kept, so the next play
     looks again.
   - **The old answers** (`count`, `categoryCounts`, `tracks`,
     `track`, `trackByPath`, `scanKnown`) are kept for the tests and
     marked deprecated. The program builds with warnings as errors, so
     it cannot call them; the tests build with
     `-Wno-deprecated-declarations`. They read a failure as nothing and
     say nothing. The store has no other readers than the typed ones.
4. **The scan's start.** `readScanKnown()` gives what the library
   knows for a scan, as a `LibraryRead<ScanKnown>`. A scan whose start
   cannot be read looks at no file (scan-runner.md, SR-REFUSED); its
   report has `startUnread`, and the window says `Could not scan: the
   library could not be read`. It does not say that the next scan will
   find what was not stored: nothing was looked at.
5. **Said once.** An answer or a scan start that cannot be read emits
   `failed` once. Until a reading of the whole library's tracks (the
   list of all of them, or the scan's start) succeeds again, a further
   one says nothing more. The count, the counts and one section's list
   do not end the silence: under partial damage they can be read while
   the tracks cannot (fix-r3b; the review of R-3, note 1). So the monitor, whose scans start every few
   seconds, says it once, and a view read again says it once. A
   change refused for a failed look (2) is always said.
   `failedAnswers()` counts every answer that could not be read, said
   or not.

**How a test makes a read fail.** A library file made in the test,
with one page overwritten by bytes no page has, while no program has
it open: SQLite answers "database disk image is malformed" for each
read that needs that page and every other read as before. The first
leaf of the table fails the first tracks by id and every scan of the
table; the last leaf fails a scan part way; the root of the index of
paths fails every look by path. No hook is built into the program.
(One site is not reached alone: a storing's look by id, which always
comes after the look at the deleted rows, a scan of the whole table.
It is the same reader.)

### L-WORDS

(fix-70c. The words of a refusal were the database's own, with the
driver's tail: "database is locked Unable to fetch row". A read-only
library file opened and then refused every change; a full disk said
"That file is not a linuxBee library".)

The library's words for a refusal or a failed read are **a sentence a
person reads, then the database's own words** after a colon. The
sentence is chosen by the database's result code, never by a word in
its text:

| Code | Sentence |
|---|---|
| busy, locked | another program is using the library |
| read-only | the library file is read-only |
| I/O error | the disk could not be read or written |
| damaged, not a database | the library file is damaged |
| full | the disk is full |
| cannot open | the library file cannot be opened |

Any other code gives the database's words alone. The database's words
are its own text, without the driver's ("Unable to fetch row").

**Opening to write** (L-OPEN):
- A library file that exists and cannot be written to does not open:
  `The library cannot be written to: FILE (the library file is
  read-only; chmod u+w on it makes it writable)`. Nothing is made
  beside it. This covers a read-only file in a folder that can be
  written to, a library without a write-ahead log, and a read-only
  disk.
- **Where the fault is** (fix-70d), checked in this order before the
  file's own mode:
  - a disk mounted read-only: `The library cannot be written to: FILE
    (the disk it is on is mounted read-only)`, with no `chmod`;
  - a file that can be written to in a folder that cannot:
    `The library cannot be written to: FILE (its folder cannot be
    written to, and the database needs its side files (-wal, -shm)
    beside it)`, the reading open's own words.
- **The window then does not start** (SH-MAIN, exit 4): it says the
  sentence, on standard error and in a dialog, and touches nothing.
  Opening read-only instead, to browse and play, is not in 1.0: the
  folder monitor's scans and every play would each be refused, and
  saying those once needs its own design.
- A first read refused as read-only gives `The library cannot be
  written to: FILE (WORDS)`.
- A file that is no database gives `That file is not a linuxBee
  library: ` and the database's words.
- Any other failure (a full disk, a busy or damaged file) gives `The
  library cannot be opened: FILE (WORDS)`. It is not "not a linuxBee
  library".

**Opening to read** (L-READ) keeps its sentences for a busy library
and for a folder that cannot be written to, chosen by the code. A file
that is no database is no library. Any other failure gives the WORDS.

**In the window** (library-shell.md, SH-REFUSED): a failed answer is a
read, not a change. The library tells which: a failed answer that says
`failed` says `answerFailed` straight before it, with the same words; a
refused change says `failed` alone. (fix-70d; the review of 70c, B1:
the notice had judged by whether `failedAnswers()` had moved. The
library says `failed` once for a run of failed answers, so the counter
was always ahead, and a refused change after two failed reads was
called a read, or was not said at all.) The notice says `The library could not be read: `
and the words. A line that already says "the library could not be
read" is not said over.

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

Not covered by `recordMoves`: a file moved by another program. A scan
follows it (L-FOLLOW), and says `pathsMoved` then.

### L-STATS

`recordPlay(id, when)` adds one to the play count and sets last played to
`when` if that is later than what is stored. `recordSkip(id, when)` adds one
to the skip count and changes nothing else. Both return false, changing
nothing, for an unknown id or an invalid time.

`mergeStatistics(list)` is how an import is stored: for each entry (an
id and four values) the date added becomes the one given, when it is
valid; each count becomes the larger of the one given and the one
held (a negative one is no count); last played becomes the later of
the two. The comparing is done by the database inside the change, on
what the library holds at that moment, so a play counted since the
caller looked is not lost. **One change, all or none**, under the
write lock (L-WAIT, the long wait: someone asked). It returns how many
tracks it changed; an entry whose id the library does not have, or
that would change nothing, is not counted. One `tracksUpdated` names
the tracks changed, and none goes out when none was. Refused (the
lock, or any statement): nothing is stored, `failed` is said once,
and it returns -1. An empty list asks nothing of the library and
returns 0. (Unlike other changes, a list that would change nothing is
found out only under the lock; the import command gives it only what
its plan says would change.)

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
  When the scan followed files (L-FOLLOW) it prints, directly after
  the result line and before any line about names:
  `followed N files that were moved`, and for one
  `followed 1 file that was moved`. Nothing when it followed none.
  Then, when there are folders it could not look in (L-LOOKIN), on
  standard output:

  ```
  could not look in N folders; the tracks in them are left as they are:
    FOLDER
  ```

  with `could not look in 1 folder; the tracks in it are left as they
  are:` for one. Then, when there are library folders that look
  unplugged (L-UNPLUGGED):

  ```
  N folders look unplugged (not mounted, or empty where they held tracks); their tracks are left as they are:
    FOLDER
  a folder emptied on purpose: scan it again with --empty-means-gone
  ```

  with `1 folder looks unplugged (not mounted, or empty where it held
  tracks); its tracks are left as they are:` for one. Then, when it passed something over (L-PARTS):
  `passed over N files the library had changed meanwhile; the next
  scan looks again` (`1 file` for one). The exit status does not
  change.
- A scan that could not be stored (L-FAILED) prints no result line. On
  standard error: `linuxbeectl: the scan was not stored: ` and the
  database's words. Exit 3.
- A library that opens but whose tracks cannot be read (L-READFAIL 3):
  `count`, `list` and `import` print nothing on standard output. On
  standard error: `linuxbeectl: the library could not be read: ` and
  the database's words. Exit 3. `import --apply` changes nothing.
- `list` prints one line per track in ascending order of path: the path,
  or with `--format` the template evaluated on the track's
  `LibraryFieldSource`.
- Exit 0 on success. Exit 2, doing nothing, for a usage mistake: no `--db`,
  no or an unknown subcommand, `scan` without a folder, an extra argument,
  an unknown category, a `--now` that is not a date, a template that does
  not parse (`error at N: message`). Exit 3, with a message on standard
  error, when the library file cannot be opened: for a reading command
  that cannot open it as L-READ says, too.

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
| L-FIELDS | How MusicBee writes Date Added in a template, and to how many places it gives Size. linuxBee: ISO 8601 UTC, and megabytes to one decimal. |

### Answered from the MusicBee wiki (checked 2026-11-20)

| Rule | Question | What the wiki says |
|---|---|---|
| L-FIELDS | How MusicBee writes Date Added and Size in a template. linuxBee: ISO 8601 UTC, and megabytes to one decimal. | In part: Size is "File size, in MB" ("Special Fields"); to how many places is not said. How Date Added is written is still open above. |
