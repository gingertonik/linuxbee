# Playlists made and changed from the navigator

A right click on the navigator's playlist rows offers to make, rename,
move and delete playlists, and File > Playlists > New Playlist works.

This document is the contract for `src/ui/shell/playlistediting.h` and
for what `src/app/main.cpp` does with it. The locked tests
`tests/locked/tst_ui_playlistediting.cpp` and
`tests/locked/cli_playlistediting.sh` check it. The navigator's part
(which row a menu is about) is navigator.md, V-MENU; what a valid name
is and what the store does are playlists.md (Y-NAME, Y-STORE).

## Sources

| Tag | Source |
|---|---|
| W-NAV | MusicBee wiki, "Navigator": playlists are managed from the left sidebar's context menu (see navigator.md) |
| PE-… | A linuxBee decision. The wording and order of MusicBee's menu are not documented in what could be read. |

## PE-MENU

`commandsFor(rowId)` is what the menu of a navigator row offers, in
order:

| Row | Commands |
|---|---|
| `playlists` (the heading) | `new` |
| `folder:` and a path | `new` |
| `playlist:` and an id that the store has | `rename`, `move`, `delete` |
| anything else | none |

`label(command)`: `new` is `New Playlist…`, `rename` is `Rename…`,
`move` is `Move to Folder…`, `delete` is `Delete`; an unknown command
has no label.

## PE-COMMANDS

`run(command, rowId)` carries a command out. It returns false, and
changes nothing, when the row does not offer that command.

| Command | Does |
|---|---|
| `new` | asks `askName("New Playlist", "New Playlist")`; with an answer, `PlaylistStore::create` of that name, in the row's folder (none for the heading). Then the new playlist is the navigator's choice |
| `rename` | asks `askName("Rename Playlist", NAME)`, NAME being the playlist's present name; with an answer, `PlaylistStore::rename` |
| `move` | asks `chooseFolder(folders)` with the store's folders; with an answer, `PlaylistStore::moveToFolder` (the empty text is the top). A folder that does not exist yet is made first |
| `delete` | asks `confirm("Delete the playlist \"ID\"? Its tracks stay in the library.")`; on yes, `PlaylistStore::remove` |

- No answer, or a "no", ends the command: false, nothing changed, and
  the message is left as it was.
- When the playlist renamed or moved was the navigator's choice, the
  playlist under its new id is the choice afterwards.
- A refusal by the store ends the command with false and its reason in
  the message.

## PE-ADD

`addSelected()` adds the tracks selected in the list
(`LibraryShell::selectedPaths()`, in the order shown) to a playlist.

- With nothing selected it asks nothing and returns false.
- It asks `choosePlaylist(ids)` once, with the id of every playlist the
  store has, in the store's order. No answer ends it: false, nothing
  changed, the message left as it was.
- The empty text means a new playlist: it asks
  `askName("New Playlist", "New Playlist")` and creates that playlist at
  the top, in no folder. No name ends it as above. A name the store
  refuses ends it with false and the `create` message of PE-MESSAGE.
- An id the store does not have ends it with false and the message
  `Could not add to the playlist "ID": there is no such playlist`.
- Then the tracks are added to the end of the playlist, in one call of
  `PlaylistStore::append` with duplicates allowed: a track that is in
  the playlist already is added once more (playlists.md; MusicBee's
  List menu has "Remove Duplicates" for this). The message is
  `Added 1 track to "ID"` or `Added N tracks to "ID"`. When the store
  could not write, false and
  `Could not add to the playlist "ID": the file could not be written`.
- The navigator's choice and the selection stay as they were, also for
  a new playlist.

Speed: one write of the playlist's file for the whole selection, however
many tracks (`PlaylistStore::tracksChanged` once).

## PE-MESSAGE

Every outcome goes to the window's status bar and is kept as
`lastMessage()`:

| Outcome | Message |
|---|---|
| created | `Created the playlist "ID"` |
| renamed | `Renamed the playlist to "ID"` |
| moved | `Moved the playlist to "ID"` |
| deleted | `Deleted the playlist "ID"` |
| refused | `Could not create the playlist: REASON`, and likewise `rename`, `move`, `delete` |

ID is the playlist's id after the change (before it, for a deletion).
REASON is what the store said; for a deletion that failed, `the file
could not be removed`.

## PE-WIRING

Constructing the object:

- implements the command `file.playlists.new` as `run("new", "playlists")`;
- implements the command `track.addToPlaylist` as `addSelected()` (PE-ADD);
- listens to the navigator's `menuRequested`: for a row that offers
  commands it calls `showMenu(commands, labels, globalPos)` and runs
  what comes back (an empty text: nothing).

Without hooks: `askName` is a text question in a dialog, `chooseFolder`
a choice among the folders that can also be typed into, `confirm` a
yes-or-no question, `showMenu` a pop-up menu, `choosePlaylist` a choice among
`<New Playlist>` and the playlists.

Destroying the object frees the commands.

## PE-MAIN

`src/app/main.cpp` additionally creates a `PlaylistEditing` with the
default hooks, after the library shell. (`--commands` then lists
`file.playlists.new`.)

## Not in this version

- Empty folders: a folder shows in the navigator only while it holds a
  playlist, so there is no "New Folder"; moving a playlist into a new
  folder makes it.
- Adding the selected tracks to a playlist, drag and drop, reordering a
  playlist, exporting and importing.
- Auto-playlists (auto-playlist-nodes.md).

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| PE-MENU | The entries of the playlist context menu and their order. |
| PE-COMMANDS | Whether deleting a playlist asks first. |
| PE-ADD | Whether MusicBee adds a track that the playlist has already (taken as yes), and whether its menu lists every playlist or a submenu per folder. The owner's menu reads "Include in Playlist" (seen); the wiki names it "Add to Playlist". |
