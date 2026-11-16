# Deleting files: the trash

How linuxBee takes a music file away from the disk. Used by the
command "Delete Files" (library-shell.md, SH-DELETE), by
`linuxbeectl trash`, and by the organiser for a file it replaces
(organise.md, O-REPLACED).

Decided by the owner (2026-11-16): a file the user deletes goes to the
desktop's trash, always after a question. There is no permanent delete
in the window and none on the command line.

## Sources

- MusicBee wiki, "General Preferences", confirmations: "confirm
  permanent file deletion (using SHIFT delete)". MusicBee's Shift+Delete
  removes for good; linuxBee's does not (see "Differences").
- MusicBee wiki, "File Organization": "replaced files will be moved to
  the Recycle Bin".
- The freedesktop.org Trash specification, for where a trashed file and
  its record go.

## FD-TRASH: files to the trash

`moveFilesToTrash(paths)` in `files/trash.h` gives one outcome for each
different path, in the order first given. A path is made absolute and
clean first; an empty path is no path; a path given again, however it
is spelt, is not looked at again.

For each path, in this order:

| The path is | Outcome | |
|---|---|---|
| a symbolic link, whatever it points at, or at nothing | `Refused`, reason `a link` | the link and its target are left |
| not there | `NotThere` | no trash entry is made |
| a folder | `Refused`, reason `a folder` | nothing in it is touched |
| anything else that is not a regular file (a pipe, a device) | `Refused`, reason `not a regular file` | it is not opened |
| a regular file | sent to the trash: `Trashed`, with `pathInTrash`; or `Failed`, reason `the trash did not take it` | |

A file is never opened or read to decide this.

**To the trash, or nowhere.** A file the trash does not take stays
where it is, byte for byte. Nothing here removes a file in any other
way, and nothing here removes a folder: a folder that is empty
afterwards stays.

`sendToTrash(path, &pathInTrash)` is the one place a file is handed to
the trash. It makes the trash folders of the user's data folder
(`Trash/files`, `Trash/info` under `XDG_DATA_HOME`) when they are
missing, as a file manager does, and answers false when it cannot. The
file keeps its name in the trash (the desktop adds a number when the
name is taken), and its record names the path it came from, so that
restoring it puts it back.

A refusal or a failure does not stop the rest.

## FD-SEND: "it went" is said only when it went

The desktop library's own answer is not enough: it has said "moved"
for a file that stayed (a folder that cannot be written: the trash got
a second name for the file and the first could not be taken away), and
it has written a wrong `Path` for a file reached through a linked
folder on another disk. So `sendToTrash`, which is what both the
delete and the organiser call:

1. answers false, and tries nothing, when the folder the file is in
   cannot be written or cannot be found;
2. hands the file over by the real path of its folder (every link in
   the folder's path resolved) and the file's name, so the trash record
   names where the file really was;
3. answers true only when, afterwards, nothing is at the path any
   more. A file that is still there was not moved, whatever was said:
   the answer is false. Nothing is removed to make it true.

In `moveFilesToTrash` a file whose folder cannot be written is
`Failed` with the reason `its folder cannot be written`; any other
false is `Failed` with `the trash did not take it`. The outcome's
`path` stays the path as it was given (clean and absolute), links in
it and all, so that the caller knows which of its files is meant.

## FD-STAYED: "moved", and the file stayed

Rule 3 of FD-SEND is for the folders rule 1 cannot judge (a network
folder above all). When it strikes, the answer is `StillThere`, and in
`moveFilesToTrash` the outcome is `Failed` with the reason
`the file is still in its place` (not `the trash did not take it`,
which is then untrue).

The trash may then hold a second name for the very same file, with a
record; a later Restore of it would collide with the file. So, and
only then:

- if the place the library named lies inside this trash's `files`
  folder (after `..` and links are resolved), is not a link, and is
  the same file as the one at the path (same device and same file
  number), that name is taken away, and with it its record,
  `info/<that name>.trashinfo`;
- anything else is left: a copy is another file and not this
  program's to remove, and nothing outside the trash is ever touched.

Taking a second name away loses nothing: the file keeps its first.
This is the one place the program removes a name, and it is never the
name the user knows the file by.

A path that is empty, ends in `/`, or names a folder is never handed
over: `NotTaken`, and nothing is tried.

`sendToTrashWith(path, pathInTrash, mover)` and
`moveFilesToTrashWith(paths, mover)` take what stands in for the
desktop library (an empty one is the library); `sendToTrash` and
`moveFilesToTrash` are these with the library. `pathInTrash` is
written for `Went` only.

## FD-CLI

```
linuxbeectl trash FILE...
```

One line for each outcome, in order, on standard output:

```
trashed: PATH -> PATH-IN-TRASH
not there: PATH
refused: PATH: REASON
failed: PATH: REASON
```

Exit 0 when every file went to the trash, 3 when at least one did not,
2 for a usage mistake (no file, or an option: there are none), with
`usage: linuxbeectl trash FILE...` on standard error.

## Differences from MusicBee

- MusicBee's Shift+Delete deletes for good, and its confirmations can
  be switched off. Here Shift+Del moves to the trash, and always asks.

## Not in this version

- Deleting from a playlist view with the choice "remove from the
  playlist or delete the file".
- Emptying or looking into the trash.
- Undo: the trash is the way back.

## Open parity questions

| Rule | Question |
|---|---|
| FD-TRASH | Whether MusicBee refuses links, and what it does with a file that is playing. |
