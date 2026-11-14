# How a save reaches the disk

A save that is interrupted (a crash, a kill, the power) must never leave
a music file damaged. So `writeTags` does not change a file in place: it
writes a complete new version beside the file and then puts it in the
file's place in one step.

This document adds to tag-writing.md and is the contract for that part
of `src/core/tags/tagwriter.h`. The locked test
`tests/locked/tst_tagsafesave.cpp` checks it. Everything in
tag-writing.md still holds, and its tests still pass.

## Sources

| Tag | Source |
|---|---|
| WS-… | A linuxBee decision (D13). MusicBee's own way of saving is not documented. |
| POSIX | `rename(2)`: replacing a file by renaming another over it is atomic within one file system. |

## WS-ORDER

After the checks of tag-writing.md (section 1, steps 1 to 4) have found
that there is something to write:

1. The path is resolved (WS-LINK). Leftovers of an earlier, killed save
   of this file are removed (WS-KILLED).
2. The file is copied to a new file in the same folder (WS-NAME).
3. The changes are saved into the copy.
4. The copy is given what the original had (WS-KEEP), and its contents
   are forced to the disk (`fsync`).
5. The hook is called (WS-HOOK).
6. The copy is renamed over the original. The folder is then forced to
   the disk as well.

Until step 6 the original is byte for byte what it was. After step 6 it
is the complete new version. There is no moment at which the path holds
half a file.

If any step fails, the copy is removed, the original is left as it was,
and the result is `Failed`.

## WS-SYNC

Forcing to the disk is part of the save, not a courtesy.

- If the copy cannot be opened for it, or `fsync` of the copy fails
  (step 4), the save fails there: no hook, no rename, the copy is
  removed, the original is untouched, the result is `Failed`.
- If the folder cannot be opened for it, or `fsync` of the folder fails
  (after the rename of step 6), the result is `Failed` as well. Nothing
  can be undone at that point: the path holds the complete new version,
  and only its survival of a power cut is in doubt. Nothing is removed.
- What WS-KEEP copies is another matter. A permission bit, an owner or an
  extended attribute that cannot be set never fails a save: these touch
  the file's description, never its audio or its tags.

No locked test checks this rule, because a test cannot make `fsync`
fail. It is checked by reading the code.

## WS-NAME

The copy is in the folder of the file itself and is called
`.lbsave-` + six characters chosen at random from `a` to `z` and `0` to
`9` + `-` + the file's own name. For `song.flac`:
`.lbsave-k3x9qa-song.flac`.

The name ends with the file's name so that it keeps the extension, by
which the tag library tells the format. It begins with a dot so that
file managers and scans do not show it. A file name is used as it is:
brackets, stars, question marks, a leading dot or dash mean nothing
special.

## WS-KEEP

The new version has the original's permission bits and extended
attributes. Its owner and group are set to the original's where the
system allows that; where it does not, the file becomes the user's own
and the save still succeeds. Its modification time follows W-MTIME: with
`keepModificationTime` it is the original's, otherwise the time of the
save.

## WS-LINK

When the path is a symbolic link, the file it points to is the one
saved, in its own folder; the link stays a link.

A file with several hard links is a known limit: the save replaces the
name it was given, and the other names keep the old version.

## WS-REFUSED

The result is `NotWritable`, and nothing is created, when the user may
not write to the file (as before: replacing it would be possible, but a
file marked read-only is to be left alone), or may not create a file in
its folder.

## WS-HOOK

`WriteOptions::beforeReplace`, when set, is called once per save, at
step 5, with the path of the copy. At that moment both versions exist
and are complete. If it returns false the save is abandoned as a failed
step is. It is not called when nothing is written (`Unchanged`,
`InvalidChange`, `NotFound`, `NotAudio`, `NotWritable`).

It exists so that a caller can check or stop a save at the last moment,
and so that the test can end the process at the worst one.

## WS-KILLED

A process that dies between steps 2 and 6 leaves the original whole and
its copy behind. The next save of that same file removes, at step 1,
every file in the folder whose name is exactly `.lbsave-`, six
characters, `-` and the file's name. Files of any other name are not
touched, including the leftovers of other files.

Two programs saving the same file at the same moment are not guarded
against.

## Cost

Every save copies the whole file once. On file systems that can share
data between files (Btrfs, XFS) the copy is nearly free where the
library used for copying supports it; elsewhere a save of a large file
takes as long as copying it.

## Command line

`linuxbeectl tags write` prints nothing new. A save that fails says so
as before, and the file is as it was.

## Not in this version

- Clearing up leftovers of files that are never saved again (a scan
  could offer it).
- A guard against two saves of one file at once.
- Keeping every hard link in step.
