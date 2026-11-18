# Undo for tag changes

A tag change that turns out wrong can be taken back: the last one,
until the next one is made or the program closes. (The owner's ruling,
2026-11-16: one step; removed pictures are kept in a scratch folder so
that they come back too.)

This document is the contract for `src/core/tagundo/tagundo.h`. The
locked test `tests/locked/tst_tagundo.cpp` checks it. The class shows
nothing and asks nothing; the window's part is in tools-shell.md.

## Sources

| Tag | Source |
|---|---|
| UN-… | A linuxBee decision, on the owner's ruling above. |

## UN-RECORD: what is kept

One change of many files is `begin(name)`, one `write(path, changes,
writer)` for each file, and `end()`. `name` says what the change was
("Edit Tags", "Remove Tags").

`write` between a `begin` and its `end`:

1. reads the file as it is now. Picture data is read only when
   `changes.pictures` is set;
2. hands `path` and `changes` to the writer (an empty writer is
   `writeTags` with default options) and returns its result unchanged;
3. when that result is `Ok`, keeps for this file what each entry the
   change names held before: for every name in `changes.fields` and
   every key in `changes.other` its earlier values (an empty list when
   it was not there), and, when `changes.pictures` is set, the earlier
   pictures. Entries the change does not name are not kept: undo will
   not touch them.

Nothing is kept for a result that is not `Ok` (`Unchanged` too: nothing
changed). A file that cannot be read before the write is not written:
the result is `Failed` with the problem `the earlier tags could not be
read`, and the writer is not called. The same holds when a picture
cannot be kept (step 5): `Failed`, `the earlier pictures could not be
kept`, and the writer is not called. A change that cannot be taken back
is not made behind the user's back.

4. A file written twice in one step keeps, for each entry, what it
   held before the first write that named it.
5. Pictures are kept as files in the scratch folder, one file for each
   different picture (the same cover in twelve tracks is one file), and
   not in memory. The folder is made when first needed.

A `write` outside a `begin` and `end` writes and keeps nothing.

## UN-STEP: one step

There is at most one step. The first file kept in a new step forgets
the step before it, and removes that step's picture files. A step that
ends with no file kept (the user cancelled, or nothing changed) leaves
the earlier step as it was.

`canUndo()` is true when a step with at least one file is there and no
`begin` is open. `name()` is its name and `paths()` its files, in the
order first written.

`undo(writer)` hands each file, in that order, its kept entries as a
change, unless the file is no longer the one that was edited (UN-SAME:
it goes in `changed` and is not written). `Ok` and `Unchanged` put the
file in `restored`; anything else in `failed`. The step is then forgotten, whatever the results, and its
picture files are removed. With no step, `undo` does nothing and both
lists are empty. An undo is not itself a step: there is no redo.

Undo writes the earlier values of the entries the change named. It does
not look at what tags the file holds now (it does look at whether it is
the same file: UN-SAME), and it does not bring back bytes:
a tag is written the way `writeTags` writes it (tag-writing.md). The
rating frames of an ID3v2 tag are the one thing brought back whole:
UN-RATING.

`changed()` is emitted whenever `canUndo()`, `name()` or `paths()` may
answer differently: at the `end()` of a step that kept a file, and after
an `undo()` that had a step. Not for a step that kept nothing.

When the object goes, the step is forgotten and its picture files are
removed. Only files this object made are ever removed from the scratch
folder; the folder itself is left.

## UN-RATING: the rating frames come back whole

(Found by the audit of undo, 2026-11-18. In an ID3v2 tag the rating is
one byte of a `POPM` frame. The frame also holds an address, which
says whose rating it is, and a play counter, and a file can have one
such frame for each player that rated it. Undo wrote a fresh frame
from the stars. So a play counter kept in a frame with no rating was
deleted; a frame taken out came back with no address and a counter of
0; a second player's frame did not come back; and a byte of 200 came
back as 196.)

When a change names Rating and the file has an ID3v2 tag (MP3, WAV):

- what is kept for the file (UN-RECORD 3) is also **every `POPM` frame
  as it was**: its address, its rating byte and its counter, in the
  order of the tag. A file with no such frame keeps "none". UN-RECORD
  4 holds: a file written twice keeps the frames it had before the
  first write that named Rating;
- undo puts the file's `POPM` frames back as exactly those: as many,
  in that order, each with its address, byte and counter. Whatever
  `POPM` frames the file has by then go. A file that had none has
  none;
- that is so too when the file reads the same stars now as it did
  before the step, and its frames are not what they were: the result
  is `Ok`, not `Unchanged`;
- the frames travel in the change `undo` hands to the writer, so a
  caller's writer that passes the change on to `writeTags` brings
  them back (tag-writing.md, W-RATING, "the frames whole").

A change that does not name Rating keeps no frames, and its undo does
not touch them. In every other format the rating is a key like any
other, and UN-RECORD is all there is.

This is about undo only. What a rating change does to the frames is
W-RATING, as it was: one frame is left, with the address and counter
of the first, and removing the rating removes the frame.

## UN-SAME: only into the file that was edited

(Until 0.8.0 undo wrote by path alone. A file that was moved, and
another file given its old name, got the first file's earlier tags,
and was reported as put back.)

For each file of a step, what the file **is** is kept beside its path:
its device and inode, its size and its modification time, as they were
straight after the write that the step kept it for (after the last
such write, when it was written more than once in the step).

At `undo`, before anything is handed to the writer:

- nothing is at the path: the file goes to the writer as before, which
  fails, and it is in `failed`;
- something is at the path and it is that file, with that size and
  time: it is written;
- **something is at the path and it is not: it is not written, not
  read for writing, and is in `changed`.** That is another file under
  the old name, and it is also the same file saved again since by
  anything that is not this step. Undo does not know what it would be
  writing over.

`restored`, `changed` and `failed` each keep the step's order, and a
file is in exactly one.

**This takes something away.** Until 0.8.0, when another program had
saved the file since (another field, or the same one again), undo
still wrote the earlier values of the named entries over it. A save
makes a new file of the track, so a track saved since cannot be told
from another file under its name, and both are left alone now. Of the
two mistakes, leaving a change in place is the one that can be put
right by hand.

## UN-MOVED: following a move

`moved(oldToNew)` says that files have other paths now. Every file of
the step (the one being made too) whose path is a key gets the new
path. `changed()` is emitted when the answer of `paths()` changed.

A move within one disk leaves the file what it was. A move to another
disk is a copy and a removal, and the copy is another file to the
system: where the file at the new path has the size that was kept, it
is taken as the file (what it is, is taken again from there); where it
has not, nothing is taken, and UN-SAME will find it changed. The one
who calls `moved` is believed that far and no further.

Nothing follows a file that is moved without `moved` being called: by
hand, or by another program.

## UN-NOKEEP: made with no folder

(Stated here because the tools shell now leans on it; the code already
did this.)

A `TagUndo` made with an empty folder name keeps no picture, in no
place: an empty name is not the current folder. A change that would
need a picture kept is refused as in UN-RECORD (the file is not
written). Changes that need none are recorded and undone as usual.

## Not in this version

- More than one step; redo.
- Undo for a file move (the organiser), a conversion, or a delete (the
  trash is its undo).
- Keeping the step when a file could not be put back: a file in
  `failed` or `changed` is not offered again.
- Looking at the tags themselves to tell whether an unnamed change
  since would be lost: any save since is a change.
- Keeping a step over a restart.
- The command line: `linuxbeectl` changes are not recorded.
