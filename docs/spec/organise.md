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

Pages were read on 2026-11-13.

What MusicBee documents (W-O): a "to folder"; a naming template, default
`<Album Artist>\<Album>\<Disc-Track#> <Title>`; a preview of each new name
with a status; duplicates get `_#` appended unless "allow existing files to
be replaced" is ticked, in which case the replaced file goes to the Recycle
Bin; "do not reorganize files with missing naming tags", ticked by default;
"also move remaining non-media files", unticked by default.

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

`planOrganise` returns one `PlannedMove` per input, in input order.
`source` is the input as a clean absolute path. For each file, in order:

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

- Not taken: the target stands.
- Taken on disk only, and `replaceExisting` is true: the target stands and
  `replaces` is true.
- Otherwise the file gets the first free name from `uniqueRelativePath`
  (`name_1`, `name_2`, ...: P-DUP), where the file's own current path
  counts as free, and `renamed` is true. If that name is the file's own
  current path, the entry is `AlreadyInPlace`.

Two files of one run never replace each other, whatever `replaceExisting`
says.

## 3. Carrying a plan out

### O-EXECUTE

`executeOrganise` goes through the plan in order. Before each entry it
calls the progress function with the entry's index and the number of
entries; if that returns false the run stops there with `cancelled` true,
and what was done stays done.

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
floor itself, and everything above it, is never removed, however empty.

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
| O-TARGET | With "to folder" unticked and a template that has folders, are the folders used? linuxBee: no, only the file name. |
