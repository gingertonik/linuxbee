# Deleting a file sends it to the trash, after a question

**Status:** Decided by the owner, 2026-11-16.

## The question

What Delete Files, Shift+Del and the Delete key do, and whether anything in
linuxBee removes a file for good.

## The decision

- A file the user deletes goes to the desktop's trash, always after a
  question. There is no permanent delete, in the window or on the command
  line.
- In a playlist the Delete key takes the track out of the playlist and
  asks nothing. In Playing Tracks it takes it out of the queue. Anywhere
  else it removes the track from the library after a question.
- MusicBee asks before taking a file out of a playlist; linuxBee does not.
  A difference, and meant.

## What it allows

The trash is the undo of a delete. Nothing is lost by a key pressed in the
wrong view.

## Where it is relied on

- `docs/spec/delete-files.md`
- `docs/spec/library-shell.md`, SH-REMOVE and SH-DELETE
