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
