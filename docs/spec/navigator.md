# The navigator

The left sidebar: the library's sections and the user's playlists as a
tree. Choosing a row decides what the main panel shows.

This document is the contract for `src/ui/navigator/navigatorpanel.h`. The
locked test `tests/locked/tst_ui_navigator.cpp` checks it.

The panel is told what exists (track counts, playlist names) and reports
what the user chose. It reads no library and no folder; the assembly plan
connects it to both.

## Sources

| Tag | Source |
|---|---|
| W-NAV | MusicBee wiki, "Navigator" and "Left Sidebar": https://musicbee.fandom.com/wiki/Navigator |
| W-L | MusicBee wiki, library nodes (Music, Inbox, Audiobooks, Podcasts, Videos): see library.md |
| V-… | A linuxBee decision. |

Read on 2026-11-13. What MusicBee documents: a navigator in the left
sidebar listing the library's nodes, playlists in folders, and other
places (services, devices, the computer's folders), with the number of
files beside a node. Its exact rows and order are configurable there and
are not copied here.

## V-FRAME

The panel holds one `QTreeWidget` named `navigatorTree` that fills it: one
column, no header, one row selected at a time, nothing editable. Every
group is shown open.

## V-LIBRARY

The fixed rows, from the top:

```
Playing Tracks
Library
  Music
  Inbox
  Audiobooks
  Podcasts
  Videos
Playlists
```

`setLibraryCounts(counts)` puts the number of tracks beside a section:
`Music (12,345)`, with a comma between thousands. A section with no
tracks, or not named in `counts`, shows its bare name. Names in `counts`
that are not sections are ignored. Counts never change what is chosen.

## V-PLAYLISTS

`setPlaylists(names)` replaces the rows under `Playlists`. A name is a
path with `/` between folders (`Rock/Best of`), as the playlist store
lists them.

- Each part is trimmed; empty parts are dropped; a name with nothing left
  is dropped; a name that repeats an earlier one exactly is dropped.
- Rows appear in the order given. A folder row is created where its
  first playlist appears and holds everything given for it later.
- A playlist and a folder may have the same name; they are two rows.

Replacing the list keeps the choice when the chosen node still exists,
and then announces nothing. When the chosen playlist is gone, `Music`
becomes the choice and `nodeSelected` says so.

## V-NODES

A row that can be chosen has an id:

| Row | Id |
|---|---|
| Playing Tracks | `playing` |
| a library section | `library:` and its name, `library:Music` |
| a playlist | `playlist:` and its cleaned path, `playlist:Rock/Best of` |

`Library`, `Playlists` and folder rows are headings: they cannot be
selected, and clicking them chooses nothing.

- The panel opens with `library:Music` chosen.
- `currentNode()` is the chosen id. `nodes()` lists every id from top to
  bottom.
- `selectNode(id)` chooses a row and returns true; for an id that does not
  exist (ids are matched exactly) it returns false and changes nothing.
- `nodeSelected(id)` is emitted when the choice changes, by mouse,
  keyboard or `selectNode`; not when the same row is chosen again.
- `nodeActivated(id)` is emitted for a double-click or Return on a row
  that can be chosen.

## V-AUTO

`setAutoPlaylists(names)` replaces the auto-playlist rows. They stand
directly under `Playlists`, before every playlist and folder, in the
order given, and read as their names. A name is trimmed; an empty one,
and one that repeats an earlier one exactly, is dropped. Auto-playlists
have no folders: a `/` is part of the name.

The id of such a row is `auto:` and its name (`auto:Recently Added`); it
can be chosen like any other node, and is listed by `nodes()` and
`rows()` in its place. `setPlaylists` leaves these rows alone, and
`setAutoPlaylists` leaves the playlists alone.

Replacing the list keeps the choice when the chosen node still exists,
and then announces nothing. When the chosen auto-playlist is gone,
`Music` becomes the choice and `nodeSelected` says so.

## V-MENU

Asking for a context menu on a row (the right mouse button, or the menu
key with the row current) emits `menuRequested(rowId, globalPos)`. The
panel shows no menu itself; whoever listens decides what is in it.

A row's id for this purpose:

| Row | Row id |
|---|---|
| a row that can be chosen | its node id (V-NODES, V-AUTO) |
| a playlist folder | `folder:` and the folder's path, `folder:Rock/Live` |
| the `Playlists` heading | `playlists` |
| `Library`, and anywhere that is not a row | none: nothing is emitted |

A right click does not change what is chosen. `rowAt(point)` gives the
row id for a point in the tree; `rows()` lists every row id from top to
bottom.

## Not in this version

- Icons, and a different look for auto-playlists.
- Drag and drop of tracks onto a playlist, and of playlists into folders.
  (Creating, renaming, moving and deleting playlists from the tree:
  playlist-editing.md.)
- Collapsing a group and remembering it.
- Browsing by artist, album or genre in the sidebar; services, devices
  and the computer's folders.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| V-LIBRARY | The default rows of the navigator and their order, and how a count is shown. |
| V-NODES | What a single click and a double-click on a playlist each do. |
