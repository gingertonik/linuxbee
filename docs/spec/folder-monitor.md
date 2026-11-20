# Monitored folders

linuxBee can watch folders and take in what arrives in them: the Inbox,
and the "monitored folders" of the library.

This document is the contract for `src/core/library/foldermonitor.h`. The
locked test `tests/locked/tst_foldermonitor.cpp` checks it.

The monitor decides only **when** to scan and **which** folders. What a
scan does to the library is the library's rule (library.md, L-SCAN): the
monitor is handed a scanner function and calls it.

## Sources

| Tag | Source |
|---|---|
| W-LP | MusicBee wiki, "Library Preferences" (monitored folders, "on startup check for updated/missing files", "continuously monitor", inbox): https://musicbee.fandom.com/wiki/Library_Preferences |
| FM-… | A linuxBee decision. |

Read on 2026-11-13. What MusicBee documents: folders can be monitored,
new files found in them are added to the library or sent to the Inbox,
and the check can run at start-up or continuously. How it waits for a
copy to finish is not documented.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Library Preferences" (3.0.5976;
2017-10-12).

## FM-DIRS

`directoriesToWatch(roots, limit, &limited)` lists the folders to watch:
each root that exists and is a folder, and every folder below it.

- Roots are cleaned to absolute paths; an empty or blank root is ignored;
  a folder is listed once however it was reached.
- A folder whose name starts with a dot is skipped with everything below
  it, unless it was given as a root itself.
- A link to a folder is not followed.
- A root is taken before the folders below it, and roots in the order
  given. No more than `limit` folders are listed; when there were more,
  `limited` is set to true (otherwise to false).
- The answer is sorted.

## FM-ROOTS

`rootsFor(changedFolders, roots)` answers which roots must be scanned:
each root (cleaned, once) that is one of the changed folders or holds
one, in the order of `roots`. A folder is inside a root only at a `/`:
`/m/musical` is not inside `/m/music`. A root inside another root is its
own root as well.

## FM-WATCH

`setFolders(roots)` replaces the roots (cleaned, each once, blank ones
dropped, in the order given; a root that does not exist is kept, in case
it appears later, but is not watched) and watches the folders of FM-DIRS
with the limit set. Anything that was waiting is dropped. Setting folders
never scans.

`watched()` is what is watched now, sorted; `limited()` says whether the
limit cut it short. Changing the limit applies it at once.

A folder created inside a watched folder is watched from then on, so that
an album copied in as a new folder is seen file by file. After every
scan the list is made afresh, which also lets go of folders that were
deleted.

## FM-SETTLE

A change in a watched folder (a file or folder appearing, vanishing or
being renamed) does not scan at once. It starts a wait of `settleMs`.
Further changes during the wait start it again.

When the wait ends, the monitor looks at the folders that changed: the
names, sizes and modification times of what is in them. If that is not
what it saw at the last change or the last look, something is still
being written: it waits `settleMs` again. Only when a whole wait has
passed with nothing different is the scan asked for.

Then the scanner is called once with the roots of the changed folders
(FM-ROOTS), and `scanned(report, folders)` is emitted with its answer and
those roots. `isPending()` is true from the first change until then.

The scanner is never called while it is running.

## FM-CHECK

`checkNow()` scans at once: the scanner is called with every root that
exists, in order, and `scanned` is emitted. It takes the place of a scan
that was waiting. With no root that exists, or no scanner, nothing
happens.

This is the "check on start-up" of W-LP, and what a "Rescan" command
calls.

## FM-LIBRARY

`libraryScanner(library, category, now)` is the scanner that goes with a
library: `Library::scan(folders, options)` with new files going to
`category` and `now()` as the time.

## Not in this version

- Noticing a file that is changed in place without its size or time
  changing, or a change inside a folder beyond the limit: both are found
  by the next `checkNow()`.
- Network folders that send no change notices: they need a timed
  `checkNow()`, which the application may add.
- Moving files from the Inbox into the library's folders when they are
  filed: that is the organiser.
- A window for choosing the monitored folders (a preferences page).

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| FM-SETTLE | How long MusicBee waits after a file appears before reading it. |
| FM-DIRS | Whether hidden folders inside a monitored folder are scanned. |
