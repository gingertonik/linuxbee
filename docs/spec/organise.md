# Organising files

The organiser moves or copies music files to where a naming template says
they belong: `Adele/21/01-11 Someone like You.mp3`. It works in two steps
so that a person, or a script, can look before anything happens:

1. `planOrganise(files, options)` reads each file's tags and says where it
   would go. It changes nothing on disk.
2. `executeOrganise(plan, options)` carries the plan out and reports what
   happened to each file.

This document is the contract for `src/core/organise/organiser.h`,
`src/core/files/musicfiles.h` and `linuxbeectl organise`. The locked tests
`tests/locked/tst_organise.cpp` and `tests/locked/cli_organise.sh` check it
in temporary folders, on copies of the fixtures in `tests/data/tags/`.

It builds on `docs/spec/paths.md` (names from templates),
`docs/spec/track-fields.md` (the fields) and `docs/spec/tag-reading.md`.

## Sources

| Tag | Source |
|---|---|
| W-O | MusicBee wiki, "File Organization": https://musicbee.fandom.com/wiki/File_Organization |
| W-H | MusicBee wiki, "How to Create a File-Naming Template": https://musicbee.fandom.com/wiki/How_to_Create_a_File-Naming_Template |
| O-… | A linuxBee decision. Listed under "Open parity questions" where MusicBee's behaviour is unknown. |
| W-ANS | MusicBee wiki, the pages that answer the questions under "Answered from the MusicBee wiki": "File Organization": https://musicbee.fandom.com/wiki/File_Organization |

Pages were read on 2026-11-13.

What MusicBee documents (W-O): a "to folder"; a naming template, default
`<Album Artist>\<Album>\<Disc-Track#> <Title>`; a preview of each new name
with a status; duplicates get `_#` appended unless "allow existing files to
be replaced" is ticked, in which case the replaced file goes to the Recycle
Bin; "do not reorganize files with missing naming tags", ticked by default;
"also move remaining non-media files", unticked by default.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "File Organization" (3.0.5993;
2019-12-18); "How to Create a File-Naming Template" (no version given;
2016-08-09).

## 1. Which files are music

### O-MUSIC

`hasMusicExtension(name)` is true when the text after the last dot of the
file name is, ignoring case, one of: `mp3 flac ogg oga opus m4a m4b mp4 aac
wav wv ape mpc wma aiff aif tta dsf`. A name with no dot, or whose only dot
is its first character, has no extension.

`findMusicFiles(folder, recursive)` returns the absolute paths of the files
in the folder that have a music extension, in ascending order of path. With
`recursive` it descends into folders. Files and folders whose name starts
with a dot are skipped, and a symbolic link to a folder is not followed. A
folder that does not exist gives an empty list.

## 2. Planning

### O-PLAN

`planOrganise` returns one `PlannedMove` for each different input, in
input order. `source` is the input as a clean absolute path.

**An input given more than once is planned once.** Inputs are told
apart by that clean absolute path. One whose path an earlier input
has is left out: it gets no entry, and the entry of the first naming
stands where it stood. A link and the file it leads to are two
paths, and two inputs, as before. (Found by a probe of 2026-11-19: a
folder named with the folder above it, or named twice, had each file
planned once for each naming. A Copy made `x.mp3` and `x_1.mp3`. A
Move moved the file, then reported `the file is gone` for the second
entry and failed.)

Known and left: one file reached by two paths that differ as text (a
folder and a link to that folder) is two inputs.

On a terminal that shows standard output and standard error together,
the lines of standard error (`failed:`) can come after the last line
of standard output: the two are flushed apart. On standard output
itself the order is as said here.

For each file, in order:

1. `useTemplate` is true and the template is not valid: `InvalidTemplate`.
2. `readTags` (without pictures) says the file is not there: `NotFound`. Not
   audio: `NotAudio`.
3. Work out the target (O-TARGET). `MissingTags` (with `missingFields`) and
   `EmptyName` come from `buildRelativePath`.
4. The target is the source: `AlreadyInPlace`.
5. Settle conflicts (O-CONFLICT): `Ready`, or `AlreadyInPlace` when the name
   the file would be given is the one it has.

Planning creates no folder and writes no file.

### O-TARGET

The name comes from `buildRelativePath(options.namingTemplate,
TrackFieldSource(tags, source, options.fieldOptions), extension,
options.pathRules)`, where `extension` is the source file's own, case
unchanged. With `useTemplate` false the name is the file's current name.

- `targetRoot` set: the target is the root, a `/`, and the whole relative
  path.
- `targetRoot` empty: the file stays in its folder. The target is that
  folder, a `/`, and the last component of the relative path.

Targets are clean absolute paths.

### O-CONFLICT

A target is *taken* when an earlier entry of this plan has it as its
target (ready or already in place), or when something exists there on disk
that is not about to leave. A file is about to leave when an earlier entry
of this plan moves it away (`Mode::Move`, `Ready`).

*A file of the run* is the source of any entry of the plan, whatever that
entry's status and wherever it stands in the plan: one that is moved
later, one that is already in place, one that is skipped.

- Not taken: the target stands.
- Taken on disk only, by something that is **not a file of the run**, and
  `replaceExisting` is true: the target stands and `replaces` is true.
- Otherwise the file gets the first free name from `uniqueRelativePath`
  (`name_1`, `name_2`, ...: P-DUP), where the file's own current path
  counts as free, and `renamed` is true. If that name is the file's own
  current path, the entry is `AlreadyInPlace`.

Two files of one run never replace each other, whatever `replaceExisting`
says. So where the target is a file of the run that has not left, the
newcomer is numbered, with `replaceExisting` as without it. (Until
0.8.0 only an *earlier* entry's file was safe: with `replaceExisting`, a
file that a later entry was to move was replaced by an earlier one, and
the later entry then moved the newcomer on in its place. A chain of
names, a to b and b to c, lost b.)

A chain or a swap of names therefore ends with one numbered name, and
loses nothing. A second run finds the wanted name free and gives it.
Doing it in one run (the moves put in order, a spare name for a cycle)
is not in this version.

## 3. Carrying a plan out

### O-EXECUTE

`executeOrganise` goes through the plan in order. Before each entry it
calls the progress function with the entry's index and the number of
entries; if that returns false the run stops there with `cancelled` true,
and what was done stays done.

**A file of the run is never replaced, whatever the plan says.** Before a
file at a target is put aside to be replaced, it is compared, as a file
(its device and inode) and not as a path, with the source of every
entry of the plan as they were when the run began. If it is one of them
and not the entry's own source, the entry fails with a message that
contains `another file of this run`, and nothing is moved for it. A plan
is checked this way wherever it came from: a path spelled through a
linked folder is the same file.

**A link and its own file in one run.** (`Mode::Move` only.) What each
source that is a link leads to is noted when the run begins, as the
sources themselves are. When that file is the source of another `Ready`
entry of the plan, the link is its link:

- The link is carried out **after its file**, wherever the two stand in
  the plan. One planned before its file waits for it; the progress
  function is asked about each entry once, in the plan's order, as
  before.
- It is then asked what "A source that is a link" (O-ACROSS) asks, of
  **the file where it now is**: where the link's text would lead from
  the link's target folder must be that file, at the place its own
  entry moved it to, or where it was when its move failed. If not, the
  link's entry fails with
  `the link would not lead to its file from there` and the link stays.
- A link whose file has been moved away is there, though it leads
  nowhere for the moment: it is not `the file is gone`.
- The outcomes are in the plan's order, as always. When the run is
  stopped while a link waits for its file, the link is carried out
  then, against its file where it is, so that the outcomes are still
  the plan's first entries, each with its outcome.

(Found by the review of the rule on links, 2026-11-19. A link planned
before its file, both going to one folder, was refused, because its
file was not there yet; the file then moved, and the link was left
behind leading nowhere. Before that rule both had moved and the link
had worked. A link planned after its file had always failed with `the
file is gone`.)

A file that was moved to another file system is a new file there; its
link is judged against the new one, and a test holds it.

Known and left, each seen by a review and none a loss:

- A link that leads to another link, planned before it: the first is
  refused (`the link would not lead to its file from there`) and
  stays at its source, where it leads nowhere once the other has
  moved.
- A link whose text begins with `/`, and whose file this run moves,
  is refused and stays, leading nowhere, with that message.
- A run that is killed between the move of a file and the move of
  its link leaves the link at its source, leading nowhere. A second
  run passes it over as `not found`.
- A link whose file went to another file system, and which would
  have to follow it there, fails with `could not copy the file`: a
  link never crosses (O-ACROSS).
- A link whose folder and text together are longer than a path may
  be (4096 bytes) is refused though it would lead.

- An entry that is not `Ready` is `Skipped`. Its message is the status in
  words: `already in place`, `missing tags: A, B`, `the template leaves no
  file name`, `the template is not valid`, `not found`, `not a music file`.
- A `Ready` entry is checked again: if the source is gone, or something is
  at the target that the plan did not say would be replaced, the entry is
  `Failed` and nothing is touched.
- Otherwise the target's folders are created and the file is moved
  (`Mode::Move`) or copied (`Mode::Copy`). A move that cannot be done as a
  rename (another file system) is a copy, a check that the copy has the
  source's size, and then the removal of the source. A copy keeps the
  source's modification time.
- A file being replaced is put aside first and dealt with only after the
  new file is in place: sent to the trash (`Replaced::MoveToTrash`, the
  default, as W-O) or deleted (`Replaced::Delete`). If the newcomer cannot
  be put in place, the old file is put back. O-REPLACED says what becomes
  of it and what the caller is told.
- A failure makes that entry `Failed`, with a message, leaves its source
  where it was, and does not stop the run.

### O-ACROSS: a move to another file system

(Found by an outside review of the code, 2026-11-16: the move was
left to the toolkit's own fallback, which keeps neither the time nor
any of the checks below.)

A move is tried as a rename by the system's own call, which never
copies. On one file system that is all: the file is the same file
under its new name. It never replaces a file that is at the target.

Only when the system says the target is on another file system is
the file copied, in this order:

1. the bytes are copied to a file made new for them under a spare
   name in the target's folder (O-SPARE);
2. the copy has the source's size, or the move fails;
3. the copy is given the source's modification time and mode;
4. the copy is flushed to the disk (`fsync`), and its folder too, so
   that nothing is written to it after the flush;
5. the copy is closed, and a close that reports an error fails the
   move;
6. only then is the source removed. Nothing that could fail the move
   comes after it: only descriptors are let go.

Between the flush of the copy and the flush of its folder the copy is
given the target's name (O-SPARE), so of the two flushes the folder's
is the one that puts the name on the disk.

A failure at any step leaves the source as it was (bytes, time and
mode), removes whatever of the target was written, and is reported
as that entry's failure. Any other refusal of the rename (no
permission, no such file) is a failure at once, with no copy tried.

A moved file keeps its modification time in both cases, so a later
scan does not take it for a changed file.

A target the move did not make itself is never removed: if a file or
a link appears at the target after the first look, the move fails
and that file is left as it is.

**Taking the copy back.** When a step fails, the copy is removed only
while the target's name still holds the very file this move made
(the same device and file number). The move holds the file open, by
a descriptor of its own, from the moment it is made until the move
returns, and the look and the removal happen while it is held: a
file number that is in use cannot pass to another file, so a file
that someone else put there in the meantime is never taken for the
copy. This holds for a failure after the copy was closed too (the
close itself failing, or the source that cannot be removed).

**A source that changes while it is copied.** After the bytes are
copied the source's size, modification time and change time are
what they were before; if not, the move fails with `the file
changed while it was copied`, the copy is taken back and the source
stays.

**No room.** When the target's disk is full or over its quota, the
message is `there is no room for the file at the target`.

**A source that is a link.** It is not moved to another file
system: the move fails, the link stays, and nothing is copied.
Within one file system the link itself is renamed, as any file, and
its text is not changed, **but only where it still leads to its
file from there**:

- A link that leads to a file now is asked where its text would
  lead from the target's folder. When that is nothing, or another
  file (by device and file number, not by name), the move fails with
  `the link would not lead to its file from there`, the link stays,
  and nothing is at the target.
- A link whose text begins at the root leads where it led. So does a
  relative one moved to a folder from which its text reaches the
  same file.
- A link that leads nowhere already is renamed as before. (So for
  the move itself. A plan made by `planOrganise` passes such a link
  over before that: `skip: not found`.)
- A Copy reads what the link leads to, as before, and asks nothing.

(Found by the write-path audit, part 2, F5, 2026-11-18: a relative
link was renamed into another folder and led nowhere from there. The
move was reported as done, and the library then marked the track
missing.)

**Where the never-replacing rename is not known.** Some file systems
(network and user-space ones) answer `EINVAL` to it. Then the file
is given a second name at the target (`link`, which also never
replaces) and the first name is removed; if that removal fails the
second name is taken back. If the second name is refused as on
another file system, the copy above is made. Only where second
names are not possible at all (`EPERM`, `EOPNOTSUPP`, `ENOSYS`) is
the plain rename used, after one more look that nothing is at the
target. Any other answer of the first rename is a failure at once.

Known and left: extended attributes and access lists are not carried
to another file system. The folder is flushed once for each file,
not once for a run.

Known and left too, each seen by the audit of the write paths:

- A file too large for the target (a disk or a limit that answers
  "file too large", as a FAT disk does above 4 GB) fails with `could
  not copy the file`, not with the words for no room. Nothing is
  left at the target and the source stays.
- A source that another program rewrites in place during the copy,
  to the same size and with its times put back, passes the check.
- With a source folder that cannot be written, the whole file is
  copied before the removal of the original is refused; the copy is
  then taken back.
- When the library cannot be told of a move (another program holds
  it), the status line says so and nothing asks again: the track
  stays at its old path until a scan.

### O-SPARE: a copy is made under a spare name

(Found by the write-path audit, part 2, 2026-11-18. F4: the copy was
made under the target's own name, so a run killed while it copied
left a cut-off file there; the next run took it for a file in the way
and numbered the good one, and a scan would have added the cut-off
one. F3: Copy went by the toolkit's copy, and when that failed it
removed whatever was at the target, also a file somebody else had put
there a moment before.)

**Both a move to another file system and every Copy** are made the
way O-ACROSS lists, with the same checks and the same messages. A
Copy differs in two things only: no rename of the source is tried
first, and the source is not removed at the end. (A Copy of a link
reads what the link points to, as before.)

- The bytes go to a file named `.lbmove-` and six characters of `a`
  to `z` and `0` to `9`, in the target's folder, made new (never a
  file or link that is there). It is hidden, and its name ends in
  nothing a scan takes for music.
- One look comes before the bytes: if anything is at the target (a
  link counts, a dangling one too), the transfer fails with `the
  target already exists` and nothing is read.
- **The copy gets the target's name only when it is whole:** all
  bytes, checked against the source, with the source's mode and time,
  flushed, and closed for writing. Nothing is done to the file after
  it has its name.
- **The name is given by a call that never replaces:** the
  never-replacing rename; where that is not known (`EINVAL`), a
  second name (`link`) and the spare name removed; only where second
  names are not possible either (`EPERM`, `EOPNOTSUPP`, `ENOSYS`), the
  plain rename after one more look that nothing is there. If
  something has come to be at the target meanwhile, it is somebody
  else's: it is left as it is, the copy is taken back, and the
  message is `the target already exists`.
- Taking the copy back (O-ACROSS) goes by whichever name the copy has
  at that moment, the spare one or the target's.

**The bytes are shared where the disk can.** (Found by the review
of this rule's first version, 2026-11-18: a Copy of a gigabyte on
one disk went from five milliseconds to four tenths of a second, and
took the room a second time. The toolkit's copy had asked the disk
to share the bytes; the careful copy read and wrote them.)

- The first thing done to the spare file is to ask the disk to share
  the source's bytes with it (a clone, `FICLONE`). Where the disk
  does that, no byte is read or written.
- Whatever else the disk answers (it cannot, the files are on two
  disks, it does not know the call), the bytes are read and written
  as before. A refusal is nothing to report.
- Everything after is the same for both: the source is as it was,
  the copy has its size, its mode and time are given, it is flushed
  and closed, and only then named. A clone that answers "done" over
  a file of another size fails as any copy of the wrong size does,
  with `the copy is not the size of the file`.
- It is asked for a move to another file system too; there the disk
  says no, and that costs one call.

Known and left: a disk's "done" is taken for the bytes, as a write
that reports its count is. Only the size is looked at.

So at whatever moment the program is killed, the target's name holds
nothing or the whole file. What a kill can leave is the hidden spare
file. **It is not swept in this version:** to tell a dead run's
leftover from a living run's copy needs a rule of its own. It is not
in the way of the next run.

### O-IO

(Added after the review of the first version showed that three
plausible mistakes lose a file and no test could see them.)

Every system call a move makes on the way (the renames, `link`,
`read`, `write`, `fchmod`, `futimens`, `fsync`, `close`, `clone`, `unlink`)
goes through a `TransferIo` (`organise/transfer_io.h`, a locked
file), whose default is the real calls. `transferFile` is
`transferFileWith` with the default. A test gives one whose calls
fail, and the rule it checks is the one above: whichever call
fails, the source is as it was and nothing is left at the target.
Nothing else in the program uses it.

`moved` is set on an outcome when its file was moved away from its
source, and not when it was copied. The report's outcomes are in the
order the moves were made.

`done`, `skipped` and `failed` count the outcomes, O-OTHER files included.

### O-REPLACED

A replaced file must be findable afterwards, and whoever asked must be
told that a file was replaced.

- The outcome of an entry that replaced a file has `replaced` true.
- Sent to the trash, the old file is there under its own name (the name
  it had at the target; the trash adds a number if that name is taken
  there), and its trash record names the target path as the place it came
  from, so that restoring it puts it back where it was. No name the
  organiser used while putting it aside shows in either. `replacedNow` is
  its path in the trash.
- Deleted, `replacedNow` is empty.
- Nothing the organiser made while putting the file aside is left in the
  target's folder: no file and no folder.
- If the old file could be neither trashed nor deleted, the entry is still
  `Done`; `replacedNow` is where the old file was left and `message` says
  so.

### O-OTHER

Only for `Mode::Move` with `moveOtherFiles` true (W-O: "also move remaining
non-media files"). After the planned moves, for each folder that at least
one track was moved out of, in the order those folders first appear in the
plan:

- if the tracks moved out of it went to more than one folder, or to the
  folder itself, or if a music file is still directly in it, nothing more
  happens to it;
- otherwise every remaining file directly in it (hidden ones included, in
  ascending order of name) is moved to the folder the tracks went to. A file
  whose name is taken there is `Skipped` and stays.

Each of these is reported as an outcome with `otherFile` true, after the
planned entries. Files in folders below it are not touched.

### O-EMPTY

Only for `Mode::Move` with `removeEmptyFolders` true. It is false unless
asked for: MusicBee leaves emptied folders (the owner, from memory,
2026-11-14), and so does linuxBee by default. After the moves, each
folder a track was moved out of is removed if it is now completely empty,
then its parent if that is now empty, and so on upward. It stops at the
first folder that still holds anything, and it stops at a floor.
`removedFolders` lists them in the order they were removed.

**Floors.** A folder is removed only if it lies strictly below a floor. A
floor itself, and everything above it, is never removed, however empty,
**and whatever other floor lies above it**: of a folder and the folder
above it, both named, neither goes. (Until this was said, the lower of
the two was removed as a folder below the upper one.)

- With `sourceRoots` given, those folders are the floors: the folders the
  user named. A track that was moved out of a place below none of them
  leaves its folder standing.
- With `sourceRoots` empty, there is one floor: the deepest folder that
  holds every planned track that was moved. (One album: the album's
  folder stays, empty. Several albums of one artist: the album folders
  go, the artist's folder stays.)

So a run can never remove the folder it was pointed at, nor climb out of
it: organising the last files out of an inbox leaves the inbox, and the
music folder above it, where they are.

## 4. Command line

### O-CLI

```
linuxbeectl organise [--to DIR] [--template T | --keep-names] [--copy]
                     [--replace] [--delete-replaced] [--allow-missing]
                     [--other-files] [--remove-empty-folders] [--dry-run]
                     FILE_OR_FOLDER...
```

- A folder argument stands for `findMusicFiles(folder, true)`.
- The source roots (O-EMPTY) are every folder argument and, for every file
  argument, the folder that file is in. So nothing named on the command
  line is ever removed.
- `--to` sets `targetRoot`. `--template` replaces the default template.
  `--keep-names` sets `useTemplate` false and needs `--to`.
- `--allow-missing` turns `skipWhenTagsMissing` off. The other options set
  the option of the same name.
- `--dry-run` prints the plan and changes nothing: per entry
  `would move: SOURCE -> TARGET` (or `would copy:`), with ` (replaces)`
  added when it would replace a file, or `skip: SOURCE: REASON`. Exit 0.
- Otherwise the plan is carried out and each outcome printed in order:
  `moved: SOURCE -> TARGET` or `copied: SOURCE -> TARGET` and
  `skip: SOURCE: REASON` on standard output, `failed: SOURCE: MESSAGE` on
  standard error; then one `removed folder: PATH` line per removed folder.
- After the `moved:` or `copied:` line of an entry that replaced a file
  (O-REPLACED), on standard output: `replaced: TARGET (the old file is in
  the trash: PATH)`, or `replaced: TARGET (the old file was deleted)`, or,
  when it could be neither, `replaced: TARGET (the old file is still at
  PATH)`.
- **Names that are not valid UTF-8.** linuxBee cannot open such a
  file (every path in it is text), and a folder argument's search
  leaves it out. That is said: after every other line, with
  `--dry-run` too, on standard output, when the folder arguments
  hold such entries:
  `passed over N name that is not valid UTF-8, in:` (for one) or
  `passed over N names that are not valid UTF-8, in:`, then one line
  for each folder that holds them, two spaces and its path, each
  once, in ascending order. They are counted as L-NAMES counts them
  (library.md): files and folders alike, whatever their ending; such
  a folder once, and not gone into; hidden folders and links not
  gone into; each entry once, however often its folder was named or
  reached. A file argument says nothing of its neighbours. The exit
  status does not change. (Found by the write-path audit, part 2,
  F6: such a file was left where it was with no line and exit 0.)
- Exit 0 when nothing failed (skips are not failures), 3 when anything did.
- Usage mistakes exit 2 and do nothing: no input; `--template` with
  `--keep-names`; `--keep-names` without `--to`; an unknown option; a
  template that does not parse (`error at N: message`).

## Not in this version

- Exceptions: a rule that selects a different template for matching files.
  They arrive with the rules engine.
- Undo. The report holds every source and target, which is what an undo
  needs.
- Updating the library when files move, auto-organise and the sweep of
  monitored folders: these belong to the library.
- Folders (Music, Audiobooks, Videos) with a template each, and per-drive
  settings.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| O-EMPTY | Settled 2026-11-14 (the owner, from memory): MusicBee does not remove a folder that organising left empty. linuxBee: the same by default; `removeEmptyFolders` and `--remove-empty-folders` ask for it, and never the folder it was pointed at or anything above it. |
| O-OTHER | When a folder's tracks go to two different folders, where do its cover and cue files go? linuxBee: they stay. |
| O-OTHER | Does an other file replace one of the same name at the target? linuxBee: never. |
| O-CONFLICT | With "allow existing files to be replaced" and two new files of the same name, which wins? linuxBee: the first replaces, the second is numbered. |

### Answered from the MusicBee wiki (checked 2026-11-20)

| Rule | Question | What the wiki says |
|---|---|---|
| O-TARGET | With "to folder" unticked and a template that has folders, are the folders used? linuxBee: no, only the file name. | No: with "to folder" unticked the current folders are used ("File Organization"). O-TARGET already does so. |
