# The library in the window

The first assembly: the navigator and the track list are put into the
main window and connected to the library and the playlists, and the File
commands that fill the library start to work.

This document is the contract for `src/ui/shell/libraryshell.h` and for
what `src/app/main.cpp` does with it. The locked tests
`tests/locked/tst_ui_libraryshell.cpp` and `tests/locked/cli_shell.sh`
check it.

The shell owns no rule. What a track is, how a list sorts, what a scan
does and what a playlist holds are decided in library.md, track-list.md,
navigator.md and playlists.md. The shell only passes things between them.

## Sources

| Tag | Source |
|---|---|
| W-MP | MusicBee wiki, "Main Panel" and "Navigator": see track-list.md and navigator.md |
| W-FILE | MusicBee wiki, "File Menu" (Add Files, Scan Folders, Send To): see main-window.md |
| SH-… | A linuxBee decision. |

## SH-BUILD

Constructing the shell:

- puts a `NavigatorPanel` into the window's navigator panel;
- creates a `TrackListModel`, loads its columns and sorting from the
  settings group `tracklist`, and adds the page `tracks` to the window,
  which is shown. The page holds a `TrackListPage` over the model
  (SH-EXTEND says what else it may hold);
- gives the navigator the playlists and the counts (SH-COUNTS);
- chooses the node stored under `shell/node`, or `library:Music` when
  there is none or it no longer exists, and shows its tracks.

Destroying the shell frees its commands (they were implemented with the
shell as their context).

## SH-NODE

The list shows the tracks of the navigator's chosen node:

| Node | Tracks |
|---|---|
| `library:` and a section | `Library::tracks(section)` |
| `playlist:` and an id | the playlist's paths, each looked up with `Library::trackByPath`; a path the library does not know is passed over; a track is listed once |
| `playing` | the paths last given to `setPlayingPaths`, looked up in the same way; none until it is called. Calling it while the node is on show reads the tracks again |

Choosing another node empties the search box first.

The order in which a playlist's tracks appear is the list's own sorting,
not the playlist's order. (See "Not in this version".)

## SH-COUNTS

The navigator shows, for every section of `Library::categories()`, how
many tracks it holds. The counts are brought up to date whenever the
library reports tracks added, updated or removed.

## SH-LIVE

Whoever changes the library or the playlists, what is on show follows:

- `tracksAdded`, `tracksUpdated`, on a library node: each track named
  that belongs to the section on show is added to the list or replaced in
  it; each that does not (it moved to another section) is taken out. The
  rest of the list, and the selection, are left alone.
- The same two signals on a playlist node or on `playing`: the node's
  tracks are read again.
- `tracksRemoved`: those tracks leave the list.
- `PlaylistStore::listChanged`: the navigator gets the new list of ids.
  If the playlist on show is gone, the navigator falls back to
  `library:Music` (navigator.md) and the list follows.
- `PlaylistStore::tracksChanged(id)`: when that playlist is on show, its
  tracks are read again.

## SH-COMMANDS

| Command | Does |
|---|---|
| `file.scanFolders` | asks `chooseFolders`; with an answer, `Library::scan` of those folders, new files going to the section on show (or `Music` when the node is not a library section), `now` from the hook |
| `file.addFiles` | asks `chooseFiles`; with an answer, `Library::addFile` for each, same section rule |
| `file.remove` | `Library::remove` of the selected tracks. No file is deleted |
| `track.sendToInbox`, `track.sendToMusic`, `track.sendToAudiobooks` | `Library::setCategory` of the selected tracks |
| `view.refresh` | counts, playlists and the node's tracks are read again; the search text stays |
| `edit.find` | puts the keyboard focus in the list's search box |

A chooser that answers nothing (the user cancelled, or no hook was given)
ends the command without a message. The three commands on the selection
do nothing, without a message, when nothing is selected.

## SH-MESSAGE

After a command has done something, one line is shown in the window's
status bar and kept as `lastMessage()`:

| After | Line |
|---|---|
| a scan | `Scan: 3 added, 0 updated, 1 missing, 2 not audio` (the sizes of the report's `added`, `updated`, `nowMissing`, `notAudio`) |
| adding files | `Added 1 of 3 files` |
| removing | `Removed 2 from the library` |
| sending to a section | `Moved 2 to Inbox` |

The numbers are what the library answered, not what was asked for.

## SH-PLAY

When the list reports a track activated and that track is in the list,
the shell emits `playRequested(paths, index)`: the paths of every row as
shown now, and the row of the track. A track that is not in the list
emits nothing. Nothing is played by this plan.

`selectedTrackIds()`, `selectedPaths()` and `shownPaths()` are for the
parts added later (tag editor, organiser, converter, player).

## SH-EXTEND

What later plans build on. None of it changes what the shell does while
it is not used.

**The node's tracks.** `nodeTracks()` is what the chosen node holds
(SH-NODE), before any narrowing and before the search box. The signal
`nodeTracksChanged` is emitted whenever that answer may have changed:
another node was chosen, the library or a playlist changed what the node
holds (SH-LIVE), or the node was read again.

**Narrowing.** With `setNarrowing(keep)`, the list shows only those of
the node's tracks for which `keep` answers true; `nodeTracks()` still
answers all of them, and the navigator's counts are not touched. It
holds while nodes change and while the library changes: a track that
arrives or is updated is listed only if `keep` accepts it as it is now.
An empty function ends the narrowing. The search box narrows further, as
before.

**The browser.** The page `tracks` is a horizontal splitter with the
object name `browserSplitter`. `setBrowser(widget)` puts the widget to
the left of the track list and takes ownership of it; another widget, or
null, deletes the one before at once. `browser()` is the widget set. The
browser lives in the page, and goes when the window does.
Without a browser the splitter holds the track list alone.

**Node sources.** `addNodeSource(source)` adds a function that is asked,
before the table of SH-NODE and in the order added, for the tracks of
the chosen node. The first that answers true has given the node's tracks;
if none does, SH-NODE applies. A node that a source answers for is read
again on every change of the library. Adding a source reads the chosen
node again. `reloadNode()` reads it again on request (a source's answer
changed); the search text stays.

## SH-STORE

`saveState()` saves the model to the settings group `tracklist` and the
chosen node as `shell/node`.

## SH-MAIN

`src/app/main.cpp`, after `--version` has been dealt with:

- Configuration is `linuxbee.ini` in `QStandardPaths::AppConfigLocation`.
  The library is `library.db` in `QStandardPaths::AppDataLocation`, and
  the playlists are in the folder `playlists` beside it. Missing folders
  are created.
- A library that cannot be opened: the reason goes to standard error in a
  line containing `library cannot be opened`, a message box says the same
  unless `--screenshot` was given, and the program ends with status 4. No
  window is shown and the file is not touched.
- The folder chooser is `QFileDialog::getExistingDirectory`, the file
  chooser `QFileDialog::getOpenFileNames`, both starting in the user's
  music folder; `now` is the current time in UTC.
- On the way out: `saveState()`, the hotkeys, then `sync()`.
- `--screenshot FILE` goes on working as in main-window.md.

## Not in this version

- Scanning in the background with a progress bar: a scan blocks the
  window until it is done.
- Showing a playlist in its own order, and reordering it.
- The tag editor, the organiser, the converter and the player: each is
  connected by a later assembly plan. `Playing Tracks` stays empty until
  the player assembly calls `setPlayingPaths`.
- Choosing several folders at once; remembering the folders scanned.
- Deleting files from the disk.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| SH-COMMANDS | Where "Add Files" puts files when a playlist is on show. |
| SH-NODE | Whether the search box keeps its text when another node is chosen. |
