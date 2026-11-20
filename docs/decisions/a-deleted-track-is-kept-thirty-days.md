# A deleted track's row is kept for thirty days

**Status:** Decided by the owner, 2026-11-18, 2026-11-19 and 2026-11-20.

## The question

Delete Files removed a track's row with its file, and with it the plays, the
skips and the date added. A file taken back out of the trash came in as a
new track.

## The decision

- The row is kept, marked deleted, for thirty days. A scan that finds the
  file at its path brings the track back with everything it had.
- A deleted track is not followed to a like file elsewhere, and is not put
  back into the playlists it was taken out of.
- A deleted row whose file sits in a folder that is never scanned stays.
- The library's schema goes to version 3 for this, and a copy of the old
  file is made first. Version 0.8.0 cannot open a library after the step;
  the release notes say so.

## What it allows

Restoring a file from the trash restores the track. The cost is one column
in the library, and an older version that cannot read it.

## Where it is relied on

- `docs/spec/library.md`, L-DELETED and L-SCHEMA
- `docs/spec/library-shell.md`, SH-DELETE
