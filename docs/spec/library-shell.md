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

A playlist and `playing` are shown in their own order: the playlist's,
and the order last given to `setPlayingPaths`. The shell switches the
model's given order (track-list.md, T-GIVEN) when a node is chosen, the
node it starts on included, and on `view.refresh`: on for these two
kinds of node, off for every other. It switches it at no other time. The
list's sorting is neither changed nor stored differently by this. A
click on a header sorts the rows on show, as anywhere, and that sort
holds while the node stays: a narrowing, the search box, a change of the
library, of the playlist or of the queue, and `reloadNode()` leave it,
and a track that arrives falls into the sort. Choosing another node and
this one again, or `view.refresh`, shows its own order again. So what a double-click on a
track plays (SH-PLAY) follows the playlist, as in MusicBee.

## SH-COUNTS

The navigator shows, for every section of `Library::categories()`, how
many tracks it holds. The counts are brought up to date whenever the
library reports tracks added, updated or removed.

They come from `Library::categoryCounts()` (library.md, L-COUNT): the
shell reads no track to count. So a change of the library costs the
reading of the tracks it names, not of every section (with the kept
tracks of SH-EXTEND: at most a few readings for each id named).

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
| `file.scanFolders` | asks `chooseFolders`; with an answer, a scan of those folders through the shell's scan runner (scan-runner.md, SR-SHELL: the command returns at once and the message follows the scan), new files going to the section on show (or `Music` when the node is not a library section), `now` from the hook |
| `file.addFiles` | asks `chooseFiles`; with an answer, `Library::addFile` for each, same section rule |
| `file.remove` | `Library::remove` of the selected tracks. No file is deleted |
| `file.delete` | the selected tracks' files go to the trash, after a question (SH-DELETE) |
| `track.sendToInbox`, `track.sendToMusic`, `track.sendToAudiobooks` | `Library::setCategory` of the selected tracks |
| `view.refresh` | counts, playlists and the node's tracks are read again; the search text stays |
| `edit.find` | puts the keyboard focus in the list's search box |

A chooser that answers nothing (the user cancelled, or no hook was given)
ends the command without a message. The four commands on the selection
do nothing, without a message, when nothing is selected.

## SH-REMOVE: the Delete key

`file.remove` (the Delete key) does what the view on show calls for.
(The owner's rulings, 2026-11-16: in a playlist the Delete key takes
the track out of the playlist and asks nothing; anywhere else it
removes the track from the library after a question; Shift+Del stays
"to the trash, after a question".)

| View on show | The command | Its name in the menu |
|---|---|---|
| a playlist (`playlist:ID`) | takes the selected tracks out of that playlist; asks nothing | `Remove from Playlist` |
| Playing Tracks (`playing`) | asks the playback shell to take the selected tracks out of the queue; asks nothing | `Remove from Queue` |
| any other | asks, then `Library::remove` of the selected tracks | `Remove from Library` |

With nothing selected nothing happens: nothing is asked and nothing
is said.

**In a playlist.** Every entry of the playlist whose file is one of
the selected tracks goes, in one change of the playlist
(`PlaylistStore::removeAt`, once); an entry for a file the library
does not know stays. The library is not asked to remove anything and
no file is touched. The message is `Removed N tracks from the
playlist` (`Removed 1 track from the playlist`), N being the tracks,
not the entries.

"Removed" is said only when the store removed something. A playlist
the store will not edit (`PlaylistStore::editProblem`, playlists.md,
Y-EDIT) is not asked to: the message is `Could not remove from the
playlist: PROBLEM`. When `removeAt` answers 0 for any other reason,
it is `Could not remove from the playlist: the file could not be
written`. Either way the list on show stays as it is.

**In Playing Tracks.** (The owner's ruling, 2026-11-16: "delete in
playing tracks should take it out of the queue".) The shell emits
`queueRemovalRequested(paths)` once, with the paths of the selected
tracks in the order shown, and does nothing else: no question, no
`Library::remove`, no file touched, and no message of its own. What
the queue does with it is PS-UNQUEUE in playback-shell.md.

**Anywhere else.** It asks `confirm(question)` first:
`Remove N tracks from the library? The files stay. Their play counts
and dates added are lost.` (for one: `Remove 1 track from the
library? The file stays. Its play count and date added are lost.`).
Without a hook the question is a yes-or-no dialog whose default is
"no". On "no" the command ends: no track is removed, nothing is
said, and the selection is as it was. On "yes" the tracks are
removed in one `Library::remove` call and the message is `Removed N
from the library`. No file is touched either way.

What is lost is what only the library knows of a track: how often it
was played and skipped, when it was last played, when it was added,
and its section. The tags, the rating among them, are in the file.

The action's text is set whenever the view changes.

## SH-DELETE: Delete Files

`file.delete` works on the selected tracks, in the order shown.

1. It asks `confirm(question)` (a hook of `ShellHooks`) with
   `Move N files to the trash?` (`Move 1 file to the trash?` for one).
   The question comes before anything else: until it is answered no
   file and no track is touched. On "no" the command ends: no file, no
   track, no message, and the selection is as it was. Without a hook
   the question is a yes-or-no dialog whose default is "no".
2. On "yes" the tracks' paths go to `moveFilesToTrash`
   (delete-files.md, FD-TRASH).
3. The tracks whose files are `Trashed` are taken out of the library
   by one `Library::remove` call for all of them, so the list changes
   once. When none is, the library is not asked at all. Every other
   track stays in the library: one whose file was not there, was
   refused, or was not taken by the trash.
4. The message: `Moved T files to the trash` (`Moved 1 file to the
   trash`; `No file was moved to the trash` for none). After it, when
   there are any: `; M were not there` (`1 was not there`), then
   `; K could not be moved` for the refused and the failed together.

   When the library did not take the removal (library.md, L-FAILED),
   the files are in the trash all the same, and the message says
   so after the count, before the other parts: `; the library could
   not be changed and still lists them` (`still lists it` for one
   file). Step 5 happens as usual. Running the command again on those
   tracks finds their files not there.

5. When any file went, `filesTrashed(paths)` is emitted once, after
   the library was told: the paths of those tracks as the library
   held them, in the order shown.

There is no permanent delete: nothing in the window removes a file in
any other way than to the trash. "Remove from Library" (`file.remove`)
is still the command that takes a track away and leaves its file.

A file that is playing is moved like any other: the player keeps the
file it has open and plays the track to its end. It stays in the queue until
then; the other files that went leave the queue (playback-shell.md,
PS-TRASHED).

## SH-MOVED

When the library emits `pathsMoved` (library.md, L-MOVED), the shell
gives the moves to the playlist store's `relink` (playlists.md,
Y-RELINK), so a moved file stays in its playlists, in its place.

## SH-PATHS: the paths of the list

`shownPaths()` is the path of every row of the list, in the order
shown; `selectedPaths()` the paths of the selected rows, in the order
shown. A row whose track the shell does not hold gives an empty
text.

Speed: neither reads a track. The shell holds the tracks of the view
on show (SH-EXTEND), and the paths come from there: `tracksRead()`
does not rise for either call, nor when a track is activated
(`playRequested` carries `shownPaths()`). The paths are those the
library has now: the held tracks follow the library's changes.

(Before this rule each call read every row's track one by one:
measured in a Release build at 17,000 rows, about 370 ms before a
double-clicked track could start.)

## SH-FAILED: the library did not take the change

When the library emits `failed` (library.md, L-FAILED) during one of
the shell's commands, the command's line is not the usual one:

| Command | Line |
|---|---|
| Remove from Library | `The library could not be changed: none of the N tracks was removed` (`the track was not removed` for one) |
| Send To | `The library could not be changed: nothing was moved to <section>` |
| Delete Files | see SH-DELETE, step 4 |

Nothing else is done: the selection and the view stay.

## SH-REFUSED: every other refusal is said too

(Found while reading for this: nothing in the window listened to the
library's `failed` except the commands of SH-FAILED and Organise, for
the length of their own call. A play that could not be counted, files
that could not be added, a saved tag the library could not read again
and a background scan that could not be stored were refused without a
word in the window.)

The shell has one `RefusalNotice` (`shell/refusalnotice.h`), its
child, on the library and the window's status bar, for as long as the
shell lives. It listens to `failed` always.

- **When.** Not during the library's call: when control next returns
  to the event loop. The command that made the call has by then said
  its own line.
- **Once.** All the refusals gathered until then are one line. The
  count starts again afterwards.
- **Never a dialog**, and no window: a play that could not be counted
  must not stop the user's work.
- **The line.** With `<what>` the library's words for the last
  refusal gathered, and `<tail>` being `: <what>` for one refusal and
  `, N times: <what>` for N:
  - if a line has been put in the status bar since the first refusal
    gathered, and is still there, and it contains `library could not
    be changed` in any case of letters: nothing is said. That command
    worded it itself, and better;
  - if such a line is there and does not contain it: that line,
    followed by `; the library could not be changed<tail>`. The line
    is true as far as it goes (the tags were saved; none was added);
  - otherwise (no line since, or it was cleared again):
    `The library could not be changed<tail>`. A line from before the
    refusal is about something else, and is replaced.
- The shell's `lastMessage()` is its own last line and is not changed
  by this. `lastLine()` and `count()` are the notice's.
- The status bar or the library may be destroyed before the notice,
  or the notice with refusals gathered: nothing is said, nothing
  breaks.

Known: a command whose line is letter for letter the line already
in the status bar is not seen to have shown one (the status bar says
nothing of it), so the refusal then replaces the line. `<what>` is the database's own wording ("database is
locked"), not a sentence for a user. A play that could not be counted
is said and is still lost; it is not kept for later.

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

**A scan the library refused.** When the library emitted `failed`
while the shell's scan was running, the scan's line is followed by
`; the library could not be changed, and what was not stored will be
found by the next scan` (after ` (stopped)`, when that is there). A
part of a scan is stored whole or not at all (L-PARTS), so the next
scan does find it. A refusal from before the scan began is not the
scan's.

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
holds (SH-LIVE), or the node was read again. It is emitted once for each
of those, and for nothing else: not for a narrowing, not for the search
box.

**A source says when its answer changes.** A node source whose answer
for the chosen node has changed for a reason the shell cannot see (its
own store changed, or it stops or starts answering for that node) calls
`reloadNode()`. After a change of the library the shell asks it anyway.

**Kept, not read again.** The shell keeps the node's tracks as it last
read them, and `nodeTracks()` answers from that without touching the
library; for a library section they come in no promised order. The
node is read (the node sources asked, then SH-NODE) once when another
node is chosen, on `reloadNode()`, on `addNodeSource`, on `view.refresh`,
and as SH-LIVE says for a playlist, for `playing` and for a node a
source answers for. On a library section a change of the library does
not read the section again: the tracks the library names are added to
the kept tracks, replaced in them or taken out, as they are now. (The
sources may be asked once per signal whether the node is theirs.)

**Narrowing.** With `setNarrowing(keep)`, the list shows only those of
the node's tracks for which `keep` answers true; `nodeTracks()` still
answers all of them, and the navigator's counts are not touched.
Setting or ending a narrowing reads nothing: it goes through the kept
tracks. It
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

- Configuration is `linuxbee.ini` in the settings folder and the library
  is `library.db` in the data folder (main-window.md, W-PLACES); the
  playlists are in the folder `playlists` beside it. Missing folders
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

- A progress bar for a scan, and a way to stop one from the window
  (scan-runner.md).
- Reordering a playlist or Playing Tracks by hand. Showing an
  auto-playlist in the order it was selected in.
- The tag editor, the organiser, the converter and the player: each is
  connected by a later assembly plan. `Playing Tracks` stays empty until
  the player assembly calls `setPlayingPaths`.
- Choosing several folders at once; remembering the folders scanned.
- Deleting files for good. Delete from a playlist view.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| SH-COMMANDS | Where "Add Files" puts files when a playlist is on show. |
| SH-NODE | Whether the search box keeps its text when another node is chosen. |
