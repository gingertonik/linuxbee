# Playlists

Two things: playlist files that other programs read and write (M3U, M3U8,
PLS), and the folder where linuxBee keeps the user's own playlists.

This document is the contract for `src/core/playlists/playlistformat.h`,
`src/core/playlists/playliststore.h` and `linuxbeectl playlist`. The locked
tests `tests/locked/tst_playlists.cpp` and `tests/locked/cli_playlist.sh`
check it.

## Sources

| Tag | Source |
|---|---|
| W-P | MusicBee wiki, "Playlists" and "Library Preferences": https://musicbee.fandom.com/wiki/Playlists , https://musicbee.fandom.com/wiki/Library_Preferences |
| FMT | The M3U and PLS layouts are de facto formats with no standard body. The description used: https://en.wikipedia.org/wiki/M3U and https://en.wikipedia.org/wiki/PLS_(file_format) |
| Y-… | A linuxBee decision. |

Read on 2026-11-13. What MusicBee documents (W-P): playlists live in a
`Playlists` folder inside the library folder, and can be put in playlist
folders; library playlists are saved as MBP (its own format) or M3U;
exports can be M3U, M3U (#EXT), M3U8, ASX, PLS, WPL or XSPF, with options
for relative paths; only static playlists can be imported. The MBP format
is not published and is not read or written here.

## 1. Playlist files

### Y-FORMAT

`.m3u`, `.m3u8` and `.pls`, by the end of the file name, case ignored.
Anything else is not a playlist file.

### Y-DECODE

A UTF-8 byte order mark at the start is dropped. `.m3u8` is UTF-8; a byte
sequence that is not valid becomes U+FFFD. `.m3u` and `.pls` are read as
UTF-8 when all of the bytes are valid UTF-8, and as Latin-1 otherwise.

Lines end with `\r\n`, `\n` or `\r`. Every line is trimmed.

### Y-READ

M3U and M3U8 (FMT):

- An empty line is skipped.
- `#EXTINF:SECONDS,TITLE` (the tag in any case) describes the next entry.
  SECONDS, trimmed, is a number; it is rounded to the nearest second, and
  anything negative or unreadable is -1 (not known). TITLE is what follows
  the first comma, trimmed; with no comma there is no title.
- Any other line that starts with `#` is ignored.
- Any other line is an entry's location (Y-PATH). It takes the pending
  `#EXTINF`, if there is one; an `#EXTINF` with no location after it is
  dropped.

PLS (FMT): lines of `Key=Value`, split at the first `=`, key and value
trimmed, keys in any case. `FileN`, `TitleN` and `LengthN` with N of 1 or
more describe entry N. Entries come out in ascending order of N. An entry
with no `FileN` is dropped. A length that is negative or not a whole
number is -1. Every other line is ignored.

### Y-PATH

A location, in this order:

1. Starts with `file://`: the local path the URL names (percent-decoded),
   cleaned.
2. Is a URL (letters, digits, `+`, `-` or `.`, at least two of them, then
   `://`): kept exactly.
3. Otherwise it is a path. If it has a backslash and no forward slash, its
   backslashes become forward slashes.
4. A path that starts with `/`, or with a letter, `:` and `/` (a Windows
   drive), is absolute: cleaned and kept. A drive path cannot be opened
   here, but it is kept so that the library's relink can repair it.
5. Any other path is relative to the playlist's folder, and is resolved
   against it and cleaned.

"Cleaned" is `QDir::cleanPath`: no `.` or `..` parts, no doubled slashes.
Nothing on disk is looked at.

### Y-WRITE

Output is UTF-8 with `\n` line ends, for all three formats.

M3U and M3U8: with `extended`, the first line is `#EXTM3U`, and an entry
that has a title or a known duration gets a line
`#EXTINF:SECONDS,TITLE` (-1 for an unknown duration) before its location.
Without `extended` there are only locations.

PLS:

```
[playlist]
File1=LOCATION
Title1=TITLE          (only when there is a title)
Length1=SECONDS       (-1 when not known)
NumberOfEntries=N
Version=2
```

A location is written as it is, unless `relativePaths` is set and
`baseFolder` is not empty and the location is a path starting with `/`:
then it is written relative to `baseFolder`, with `../` where it must.

Reading back what was written, in the same folder, gives the same entries.

## 2. The playlist folder

### Y-STORE

A `PlaylistStore` is given a folder. Every `.m3u8` file in it or below it
is a playlist, and every folder below it is a playlist folder. Files and
folders whose name starts with a dot are not seen, nor is anything inside
such a folder. The store keeps nothing in memory: each call reads the
disk, so a file put there by hand shows up.

- A playlist's id is its path below the store's folder without the
  extension: `Rock/90s/Favourites`. Ids are exact, case included. An id
  with an invalid part (Y-NAME) names nothing: no call ever reaches a file
  outside the store's folder.
- `playlists()` and `folders()` are in ascending order, case ignored.
- `create` makes an empty playlist, and its folders if needed. `rename`
  keeps the folder. `moveToFolder` keeps the name; an empty folder means
  the top. `remove` deletes the file. Folders are never removed.
- A new id that differs from an existing playlist's only by case is
  refused (so the folder can be copied to a file system that ignores
  case). Renaming a playlist to its own name in another case is allowed.
- A rename or move to where the playlist already is succeeds and changes
  nothing.
- The store's own files hold one absolute location per line and no
  `#EXT` lines.
- `append`, `removeAt` and `moveTrack` are as their comments in the
  header say. `moveTrack` with a position outside the list fails.
- `listChanged` is emitted after a playlist or folder is created, renamed,
  moved or removed; `tracksChanged(id)` after a playlist's entries change.
  A call that changes nothing emits nothing.
- Files are written whole and then moved into place (`QSaveFile`), so a
  crash cannot leave half a playlist.

### Y-EDIT: a list that was not read whole and true is not written

(Found by the write-path audit, part 4, 2026-11-18. A playlist that
could be written but not read was taken as empty: one track added,
and the file held that track alone. And a `.m3u8` that is not UTF-8,
which Y-DECODE reads with U+FFFD in the place of what it cannot read,
was written back that way by any edit: every entry with such a letter
then named no file, for good.)

`editProblem(id)` says whether a playlist's entries can be changed:

| The file | Answer |
|---|---|
| no such playlist (Y-STORE) | `there is no such playlist` |
| cannot be opened or read | `the file could not be read` |
| is not UTF-8: a byte sequence that is not valid UTF-8, a UTF-16 byte order mark at the start, or a zero byte anywhere | `the file is not UTF-8` |
| anything else, an empty file too | empty: it can be edited |

`append`, `removeAt`, `moveTrack` and `setTracks` do nothing to a
playlist that has a problem: they answer 0 or false, not a byte of the
file changes, nothing is left beside it, and `tracksChanged` is not
emitted.

**One read, and a whole one.** (Found by the review of fix-86,
2026-11-18: a read that stopped after a few bytes with a disk error
left no error behind, so those bytes were taken for the list and
written back with the new entry; and the check and the edit read the
file apart, so a file that could be read once and not again was
emptied.)

- `editProblem` and each of the four calls open and read the file
  **once**. The bytes that are judged are the bytes the entries are
  taken from, and the bytes the result is compared with to see
  whether anything is to be written (Y-STORE).
- The read is whole or it is `the file could not be read`: the file
  is read to its end, every read is asked whether it failed, and the
  count of bytes read is the size the file had when it was opened. A
  read that fails part-way, or ends early, is not a short list.
- `setFileOpener` tells the store how a playlist file is opened for
  reading (it answers an open device, or nothing). With none given
  the file itself is opened. It is there for the test, which hands
  in a device whose reads fail; the program gives none.
- `relink` reads each playlist the same way: once, through the
  opener, and whole (Y-RELINK).

**Not a file.** A playlist's name can be had by something that is
not a file: a pipe, a device. Opening a pipe waits until somebody
writes to it. Such a thing is never opened: `editProblem` is `the
file could not be read`, `tracks` gives none, the four calls do
nothing, and `relink` passes it over. A link to a file is the file.

Reading is as it was: `tracks` gives what Y-DECODE makes of the file,
so such a playlist is still shown and played as far as it can be.
`relink` works on the bytes and never decodes; it is as it was.
`rename`, `moveToFolder` and `remove` do not touch the entries and are
as they were.

Not in this version: converting such a file to UTF-8 for the user;
editing it by its byte lines.

### Y-SAME: a playlist is written only over the file that was read

(Found by the write-path audit, part 4, 2026-11-18. A call read the
file, worked out the new list and put it in the file's place, and
asked nothing of the file in between. What another program wrote
there meanwhile was replaced, and the call answered success: of two
writers making 300 additions each, 300 entries were left.)

The other writer is another program. linuxBee has one writer of the
store, the window, and the window runs once a profile; so there is no
lock, which only a program that asks for it would heed.

`append`, `removeAt`, `moveTrack`, `setTracks`, and `relink` for each
playlist, go this way:

1. The first look, before the file is opened for reading: what the
   system says of the name. Which file it is (device and file
   number), its size, when its contents were last changed and when
   its record was.
2. The one read (Y-EDIT), through the opener.
3. When there is something to write, the new bytes are written
   beside the file, whole.
4. `setBeforeReplace`'s hook is called with the file's path, if one
   is set. It is there for the test, which is the other program at
   that moment; the program sets none.
5. The second look, straight before the file is replaced. When the
   name is gone, or the two looks differ in any of the five, nothing
   is replaced: what was written beside the file is removed, the
   file is as the other program left it, the call answers 0 or false
   and `tracksChanged` is not emitted.
6. The file is replaced.

A call with nothing to write (Y-STORE) ends before step 3: it writes
nothing beside the file, calls no hook and takes no second look.

A call refused at step 5 is refused once. The same call made again
reads the file as it now is.

`relink` goes on to the next playlist after one that was refused at
step 5; that one is among `relinkPassedOver()` (Y-RELINK).

`lastEditProblem()` says why the last `append`, `removeAt`,
`moveTrack` or `setTracks` did nothing:

| The last of the four calls | Answer |
|---|---|
| wrote, or had nothing to do (a position outside the list too) | empty |
| was refused by Y-EDIT | `editProblem`'s line |
| was refused at step 5 | `the file was changed by another program` |
| could not write beside the file, or could not replace it | `the file could not be written` |

Each of the four begins by forgetting the line of the call before.

The five are compared because a file put in another's place can be
given its size and the time of its contents. On Linux the time of
the record moves with every change, so no test here can tell a
comparison of all five from one of that time alone; the other four
are for a file system that keeps that time badly.

Known and left:

- A change made after the second look and before the replacing is
  still lost. Nothing stands between the two but the replacing
  itself; no test reaches it.
- Two changes inside one tick of the file system's clock that leave
  the size as it was are one state to the looks.
- `removeAt` and `moveTrack` are given positions. A file changed
  since the caller read it, and before the call began, is edited at
  those positions as it now is. Not in this version.
- `create` looks through `playlists()` and then writes; a file made
  at that name in between is replaced by the empty one. Not in this
  version.
- The window still says `the file could not be written` for every
  refusal. Not in this version.

### Y-RELINK

`relink(oldToNew)` (library.md, L-MOVED): in every playlist of the
store, each entry that is one of the old paths becomes its new path.
Each entry is looked up once, in the list as it was, so two files
that swap places swap. Position and repeats are kept. A playlist in
which nothing changed is not written. It returns the number of
playlists written, and `tracksChanged` is emitted for each.

(Found by the review of fix-86b, 2026-11-18: `relink` read with one
unchecked call. A read that stopped part-way gave it a short list,
and when the line of a moved file was in that part, the short list
was written over the playlist.) Each playlist is read once and whole
(Y-EDIT, "One read, and a whole one"). One that was not read whole,
or could not be opened, is not written, not counted and not
announced; the others are relinked. `relinkPassedOver()`
gives the ids of those the last `relink` left, in the order of
`playlists()`: each one that was not read whole or could not be
opened (whether it names a moved file or not: nobody knows), and
each one that was read, had a line to change and could not be
written. One read whole in which nothing changed is not among them.
A `relink` with no moves looks at nothing and leaves none.

The store lists a link, and a link may lead to a pipe; `relink`
meets that one, does not open it (Y-EDIT, "Not a file"), and names
it among those left.

### Y-NAME

A playlist or folder name is refused when it: is empty or only white
space; begins or ends with white space; contains `/` or `\`; begins with a
dot; contains a control character (below U+0020); or is longer than 200
bytes in UTF-8. A folder path is names joined by `/`, each one valid.

## 3. Command line

### Y-CLI

```
linuxbeectl playlist show FILE
linuxbeectl playlist convert [--relative] [--plain] [--force] IN OUT
```

- `show` prints one line per entry: location, a tab, the duration in
  seconds (-1 when not known), a tab, the title.
- `convert` reads IN and writes OUT in the format of OUT's extension, then
  prints `N entries`. `--relative` writes locations relative to OUT's
  folder. `--plain` leaves out the `#EXT` lines.
- **`convert` never writes the file it reads.** When OUT is IN (the
  same file by another spelling, through a linked folder, by a link
  to it or by a second name of it): `failed: OUT: this is the file
  being read`, exit 3, with `--force` as without it. (A list read and
  written again is not the same list: comments, a relative path and
  the line ends are not kept. Until 0.8.0 this was done without a
  word.)
- **`convert` writes over a file only when asked.** When anything is
  at OUT (a link to nothing counts): `failed: OUT: the file is there
  already (--force writes over it)`, exit 3. With `--force` it is
  written over. In both refusals nothing is on standard output and no
  byte of OUT or IN changes.
- **The path written is the path checked.** OUT is read in two
  steps, and the path that comes out, which has no link left in it,
  is the one every check looks at and the one that is written:
  1. as text: made absolute, `.` and `..` taken out without asking
     the disk;
  2. then by the system: when something is at that path, its real
     path, every link followed; when nothing is, the real path of
     its folder and the name.
  A link that leads to nothing is not written through, with `--force` as without it
  (without it, the answer is that something is there already):
  `failed: OUT: it is a link to nothing`, exit 3, and nothing is
  made where it points. A link to a file that is there is followed:
  the file is written and the link stays a link.
  (The review of fix-87, 2026-11-18: the toolkit's writer follows a
  link by reading its text and cleaning that as text. For a link
  whose own text goes through `..`, that is another file than the
  one the system finds, so the file checked was not the file
  written, and the file being read was written over.)
  `--relative` is relative to the folder of that path.
- Exit 2 for a usage mistake, including a file name that is not `.m3u`,
  `.m3u8` or `.pls`. Exit 3, with `failed: FILE: ...` on standard error,
  when IN cannot be read or OUT cannot be written. OUT is not left
  half-written.

## Not in this version

- Auto-playlists (rules, limits, live updating): their own plan.
- ASX, WPL, XSPF and MBP.
- Dead entries: the store returns locations and does not check them.
- Mapping base paths on export.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| Y-DECODE | Which encoding does MusicBee write for "M3U" and for "M3U (ascii)"? linuxBee writes UTF-8 for all. |
| Y-STORE | Can two playlists in one folder differ only by case? |
| Y-WRITE | What does the #EXTINF title hold in MusicBee's export: "Artist - Title"? |
