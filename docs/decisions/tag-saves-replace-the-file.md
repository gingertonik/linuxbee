# A tag save writes a new file and puts it in place

**Status:** Decided by the owner, 2026-11-13.

## The question

A save that is interrupted (a crash, a kill, the power going) can leave a
music file damaged when its tags are written into the file where it lies.
MusicBee's own way of saving is not documented. Writing a copy costs a
second write of the file on every save.

## The decision

- A tag save writes a complete new version beside the file and puts it in
  the file's place in one step.
- A file is never written in place.

## What it allows

Every save leaves either the old file or the new one, whole. A file with
several names is refused (see "Hard-linked files are not tagged").

## Where it is relied on

- `docs/spec/tag-safe-save.md`, all of it (the rules WS-)
