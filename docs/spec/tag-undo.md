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
change. `Ok` and `Unchanged` put the file in `restored`; anything else
in `failed`. The step is then forgotten, whatever the results, and its
picture files are removed. With no step, `undo` does nothing and both
lists are empty. An undo is not itself a step: there is no redo.

Undo writes the earlier values of the entries the change named. It does
not look at what the file holds now, and it does not bring back bytes:
a tag is written the way `writeTags` writes it (tag-writing.md).

`changed()` is emitted whenever `canUndo()`, `name()` or `paths()` may
answer differently: at the `end()` of a step that kept a file, and after
an `undo()` that had a step. Not for a step that kept nothing.

When the object goes, the step is forgotten and its picture files are
removed. Only files this object made are ever removed from the scratch
folder; the folder itself is left.

## Not in this version

- More than one step; redo.
- Undo for a file move (the organiser), a conversion, or a delete (the
  trash is its undo).
- Keeping a step over a restart.
- The command line: `linuxbeectl` changes are not recorded.
