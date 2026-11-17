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

## WS-WRITTEN

(Found by the audit of the tag write path, in 0.7.0 as released: with
no room for the grown file the tag library reported a save, the copy
was cut off where the room ended, and it was put in the song's place.
The command said "saved". Shown for MP3, FLAC, Ogg, M4A and WAV.)

**A copy that was not written in full is never put in the song's
place.** The tag library is not believed about this: it does not
report a write that failed or was cut short. Every write made into
the copy in step 3 is accounted for by the writer itself, and if any
one of them did not put down all its bytes, wherever in the save it
came and whatever was written after it, the save is `Failed`: the
copy is removed, the original is byte for byte what it was, and
nothing is left beside it.

So for any amount of room there are two outcomes and no third: `Ok`,
and the file is the whole new version; or `Failed`, and the file is
the song as it was.

This covers a full disk, a quota, a limit on file size, and an error
of the medium, when the system reports them at the write. Where it
reports them only when the data is forced out (a file system that
allocates late, a network share), step 4 fails, which is WS-SYNC.

The copy of step 2 is the same: a copy that could not be made whole
is removed, and the save is `Failed`.

Not covered: a tag library that writes every byte it means to and
means the wrong ones. Nothing here reads the audio of the copy
again.

## WS-REST

(Found by the review of WS-WRITTEN: three wrong ways of moving the
rest of a file passed every tag test, because the test files were
smaller than one step of the move and a save was compared only with
another save of the same build.)

A save that makes the tag larger or smaller moves what follows it.
What follows the tag is, after the save, **byte for byte what it
was**: for an MP3 everything after the ID3v2 tag, for a FLAC
everything after the last metadata block, for an M4A whose tags come
before the sound the contents of `mdat`. (After the sound of an MP3
there may be an ID3v1 tag, which the writer adds.) This holds for sound of
several mebibytes, for a move of less than a step of the writer and
of more, for sizes that are a whole number of steps and that are not,
growing and shrinking, save after save on one file.

The locked test makes its own sound, bytes of which no stretch is
like another, and compares with those bytes as it made them, never
with the result of another save.

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

**A file with more than one name (hard links) is not saved.**
(`docs/decisions/hard-linked-files.md`. Until 0.7.0 the save replaced
the name it was given and the other names kept the old version,
without a word.) When the file the path leads to has a link count
above one, `writeTags` answers `HardLinked`, having written nothing
and made no copy, and `problems` holds one line:

`the file has N names (hard links), and linuxBee does not change the
tags of such a file; to tag it here, make it a file of its own first:
cp -p FILE FILE.new && mv FILE.new FILE`

(one line; N is the link count). This comes after W-UNCHANGED and
W-INVALID: a save that would write nothing is `Unchanged`, and a
change refused for itself is `InvalidChange`. A symbolic link is not
another name.

## WS-SAME

(Found by the audit of the tag write path: an edit another program
made while a save was under way was replaced by the save, in 1 run
of 10. The window and `linuxbeectl` can both be running.)

**The file that is replaced is the file that was read.**

- *Another program.* What the system says of the file (which file it
  is, its size, when its contents and when its record were last
  changed) is taken before the file is read. Just before the swap,
  after the hook, it is taken again. If anything differs, or the file
  is gone, the copy is removed, nothing is put in the file's place,
  and the answer is `ChangedOnDisk` with the one line
  `the file was changed by another program while it was being saved;
  nothing was written, try again`. The file is as that program left
  it.
- *Another save by linuxBee.* For the whole of a save the file is
  held with `flock(2)`, exclusive. A save that finds it held does not
  wait: it answers `ChangedOnDisk` at once, having made no copy, with
  the line `another save of this file is under way; nothing was
  written, try again`. The hold ends with the save, however it ends.
  Where the file system has no such locks, the save goes on without.

Both answers are not `ok()`.

Left: the instant between the second look and the swap; and a
program that changes the file, puts back its size and both its times,
within one tick of the system's clock.

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

**`WriteOptions::afterFirstLook`**, when set, is called once in every
call of `writeTags`, whatever comes of the call: after the first look
at the file (WS-SAME) and before the file is read. (Found by the
review of WS-SAME: a first look taken after the read lost another
program's edit in 39 of 60 saves of a large file, and every other
test passed, because they act at the swap.) It exists for the test,
which is "another program" there:

- a file changed at that moment, in place, in its time only, or by
  being replaced, is a file that changed during the save: refused
  with `ChangedOnDisk`, their version kept;
- a file removed at that moment is `NotFound`: the read comes after.

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

`HardLinked` and `ChangedOnDisk`: nothing on standard output; on
standard error one line, `not saved: <path>: <the line>`; exit 3.
Every other command that saves tags gives the line where it gives its
reason for a failure.

## Not in this version

- Clearing up leftovers of files that are never saved again (a scan
  could offer it).
- Keeping every hard link in step (decided against: WS-LINK).
