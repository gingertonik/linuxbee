# Differences from MusicBee that are kept

**Status:** Decided by the owner, 2026-11-20.

## The question

A review of the specs against the public pages found places where linuxBee
does not do what MusicBee documents. These are kept, and said to be
differences.

## The decision

- `<Path>` has no separator at its end.
- Disc-Track# is `01-11` by default (two digits each), though one wiki page
  gives `0-00`.
- `Ctrl+Z` is Undo and `F11` is Full Screen.
- A search always ignores accents; MusicBee does only when asked.
- The status bar's summary has no KB step and no switch of the time.
- The Delete key in a playlist asks nothing (see "Deleting a file sends it
  to the trash").

## What it allows

Each is written in its spec as a difference, so that it is not reported as a
fault.

## Where it is relied on

- `docs/spec/track-fields.md`, F-FILE and F-DISCTRACK
- `docs/spec/main-window.md`, H-CATALOGUE
- `docs/spec/search.md`, S-FOLD
- `docs/spec/track-list.md`, T-SUMMARY
- `docs/spec/library-shell.md`, SH-REMOVE
