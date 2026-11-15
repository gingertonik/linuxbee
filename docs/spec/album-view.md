# The "Album and Tracks" view

The library's tracks under a heading per album, in album order, with a
jump bar. Activating a track plays from there through everything shown;
activating an album's heading plays that album only.

This document is the contract for `src/core/library/albumgroups.h`,
`src/ui/albumview/albumtracksview.h`, `src/ui/shell/albumviewing.h`, and
for what `src/app/main.cpp` does with them. The locked tests
`tests/locked/tst_ui_albumview.cpp` and `tests/locked/cli_albumview.sh`
check it.

## Sources

| Tag | Source |
|---|---|
| W-ATV | MusicBee wiki, "Album and Tracks View": a hybrid of the tracks view and the album covers view; clicking on the album art selects the entire album (it acts like a grouping header); a blank line is put between different discs of the same album: https://musicbee.fandom.com/wiki/Album_and_Tracks_View (matches version 3.0; read 2026-11-13) |
| W-MPV | MusicBee wiki, "Main Panel Views": the default views are Tracks, Album and Tracks, Album Covers, Artists and Jukebox, chosen from the panel's menu: https://musicbee.fandom.com/wiki/Main_Panel_Views |
| W-LNE | MusicBee wiki, "Library Navigation Elements", Jump Bar: clicking a letter takes you to that letter in the main panel, according to the first sort tag: https://musicbee.fandom.com/wiki/Library_Navigation_Elements |
| SHOT | The user's own MusicBee window and preferences, 2026-11-13: the main panel is headed `Album and Tracks`; a bar `# A B C ... Z` runs above it; the panel's Sort By is `Album Artist / Year / Album`; under Now Playing, Play Now is `clear list and play all tracks`, with `except when clicking album covers - play album only` ticked. |
| AV-… | A linuxBee decision. |

## AV-ORDER: the order of the view

`albumSortFields()` is `Album Artist`, `Year`, `Album`, `Disc#`,
`Track#`, `Title` (SHOT: Album Artist / Year / Album, then the order on
the record).

`sortedForAlbums(tracks, options)` returns the same tracks ordered by
those keys, the weightiest first. The value compared for a key is what
`LibraryFieldSource` gives for the field, with two exceptions: for
`Album Artist` it is the artist of artist-browser.md, AB-ARTIST (so a
track with no album artist stands with its artist), and for `Year` it
is what `browseValues(track, "Year")` gives (so `1998-04-20` and `1998`
are the same year). Each key compares by
track-list.md, T-COMPARE; a blank value comes last. Tracks that tie on
every key are in order of path.

## AV-GROUPS: albums

`albumGroups(tracksInOrder)` cuts the list, in the order given, into
runs: a track belongs to the run of the track before it when both have
the same artist (AB-ARTIST, compared as AB-SAME) and the same Album
(first value, trimmed, case ignored). Tracks with no album are one run
per artist. Nothing is reordered: the same album met again later is a
second group.

Of each group:

- `artist` and `album`: as the first track has them;
- `year`: for the first track that has a Year, what `browseValues(track,
  "Year")` gives (`1998-04-20` is `1998`); otherwise empty;
- `genre`: the first value of Genre, trimmed, of the first track that has
  one; otherwise empty;
- `totalMs`: the tracks' durations added up;
- `trackIds` and `paths`: the tracks, in order;
- `discStarts`: every position above 0 at which the track's Disc# (first
  value, trimmed) differs from that of the track before it (W-ATV).

## AV-TEXT

`albumHeading(group)` is the album, or `[Unknown Album]` when it is
empty.

`albumDetail(group)` is these parts, joined by ` • ` (a bullet with a
space either side), a part that is empty left out:

1. the artist, or `[Unknown Artist]`;
2. the year;
3. the genre;
4. `1 track` or `N tracks`;
5. the length in whole minutes, rounded to the nearest (half a minute
   goes up), as `N min`.

For example `Boards of Canada • 1998 • Electronic • 17 tracks • 63 min`.

## AV-JUMP

`jumpIndex(groups, letter, options)`: each group has a first character:
that of its artist, trimmed, after one leading word of
`options.ignoreWords` and the spaces after it are dropped (case ignored;
so `The Beatles` begins with B), folded as for search (`foldForSearch`:
`É` counts as E) and made upper case. For a letter from `A` to `Z`
(either case) the answer is the first group whose first character is
that letter. For `#` it is the first group whose first character is not
one of `A` to `Z`, a group with no artist included. Any other character,
and a letter no group has, gives -1.

## AV-VIEW: the widget

Widgets, by object name:

| Name | Widget | |
|---|---|---|
| `jumpBar` | a widget holding 27 buttons | AV-BAR |
| `albumTree` | a tree with three columns and no header shown | the rows below |

`setTracks(tracksInOrder)` fills the tree from `albumGroups`:

- one top-level row per group. Its first column spans the tree and reads
  `albumHeading`, a line break, `albumDetail`. It is always expanded and
  cannot be collapsed; no branch marks are drawn.
- under it, one row per track: the Track# in the first column, the Title
  in the second (the file's name when there is no title), the Time in
  the third, each as `LibraryFieldSource` gives it. The row's tool tip is
  the path.
- before the track at each of `discStarts`, a row whose first column
  spans the tree and reads `Disc ` and that track's Disc#. It cannot be
  selected and is not a track.

`groups()` is what the tree was filled from. `setTracks` keeps the
selection (AV-SELECT) for the tracks that are still there.
After a fill the view shows its top: the first heading is the first
row shown, wherever the view was scrolled to before. (A view that is
not filled, because the same tracks fill nothing (AV-FILL), keeps its
place.)

Speed. The headings and disc lines are made to span the columns in one
pass after the last heading is in the tree, never album by album while
the tree is still being built: done one by one, that costs more with
every album there already is (measured: 1.8 s of a 2.1 s fill at 17,000
tracks, against 0.25 s for the whole fill when done after). No column is sized to its contents, because that measures every
row at every fill: the title column stretches and the other two have a
width worked out once from the font. The headings are expanded in one
step, not one by one. `fills()` is the number of times `setTracks` has
built the rows. (Measured in review, not locked: about 4,900 tracks in
1,300 albums fill in about a quarter of a second.)

## AV-SELECT

Several rows can be selected. `selectedTrackIds()` is, in the order
shown and each once: every selected track row, and every track of a
selected heading (W-ATV: the heading stands for the whole album).
`selectTracks(ids)` selects exactly the track rows with those ids (no
headings); unknown ids are ignored. `selectionChanged` is emitted when
the answer of `selectedTrackIds()` changes, by hand or by
`selectTracks`; not when `setTracks` keeps it as it was.

## AV-ACTIVATE

A double click on a track row, or Return or Enter with a track row the
current one, emits `trackActivated(id)`. The same on a heading emits
`albumActivated(group)`, the group's position. A disc row does nothing.
The view plays nothing itself.

A right click on a track row or a heading emits `menuRequested` with the
place on the screen; if the row was not selected, it becomes the
selection first.

## AV-BAR

`jumpBar` holds 27 buttons, named `jump_#` and `jump_A` to `jump_Z`, in
that order, each showing its character. A click calls `jumpTo` with it.

`jumpTo(letter)` makes the heading of `jumpIndex(groups(), letter)` the
current row and scrolls it to the top; the selection is left as it is.
When there is no such group, nothing happens.

`currentGroup()` is the position of the album the current row is in or
is the heading of, and -1 when there is no current row.

## AV-SHELL

`AlbumViewing(shell, settings)`:

- makes an `AlbumTracksView` as a child of the library shell's track
  list page (`trackListPage`) and puts it in that page's layout directly
  after `trackView`;
- puts a combo box named `mainViewCombo` at the top of the page, with the
  entries `Album and Tracks` and `Tracks` (W-MPV);
- reads `view/mainPanel` from the settings: the mode is `tracks` when the
  stored text is `tracks` (any case), and `albums` for anything else or
  nothing.

In mode `albums` the view is shown and `trackView` is hidden; in mode
`tracks` it is the other way round. The search box and the summary line
are there in both. `setMode`, or choosing in the combo box, changes the
mode, stores `view/mainPanel` (`albums` or `tracks`) at once and emits
`modeChanged` when the mode became a different one.

What the view shows, in mode `albums`: the tracks the track list's model
holds (after the navigator's node, a browser's narrowing and the search
box have had their say), put in order by `sortedForAlbums`. The order
the plain list is sorted in is not touched.

**AV-FILL: one fill for many changes.** A change of the model or of the
node's tracks does not fill the view; it makes it stale. A stale view is
filled once: at the next turn of the event loop, or at once when
`AlbumViewing::view()` is asked, whichever comes first. So one change of
the library, which reaches this object as several signals, costs one
fill, and so does a run of changes made in one turn. A change of mode
fills (or empties) at once. In mode `tracks` the view is emptied once
and nothing makes it stale.

The same tracks fill nothing. When a stale view is to be filled and the
list holds the same track ids in the same order as when the view was
last filled, and `LibraryShell::nodeTracksChanged` has not been emitted
since, the view is left as it is and is no longer stale. So a letter
added to a search that finds the same tracks costs no fill; a track
that changed in the library, or the node chosen again, does.

While in mode `albums`:

- the view's selection is the shell's: whenever it changes, the same ids
  are selected in the track list (`TrackListPage::selectTracks`), so
  `LibraryShell::selectedTrackIds()` and every command that acts on the
  selection follow;
- and the other way round: when the track list's selection changes
  (`TrackListPage::selectionChanged`; Select All does this) to ids other
  than the view's, the view is brought up to date and selects those ids
  (`selectTracks`). When it already has them, nothing is touched, so a
  selected heading stays selected. On a change to mode `albums` the view
  selects what the track list has selected;
- `trackActivated(id)` makes the shell emit
  `playRequested(paths, index)` with the paths of every track the view
  shows, in the view's order, and the position of that track (SHOT:
  "clear list and play all tracks");
- `albumActivated(group)` makes it emit `playRequested` with that
  group's paths and 0 (SHOT: "play album only");
- `menuRequested(pos)` shows the track list's own context menu
  (`trackContextMenu`) at that place.

In mode `tracks` the view is given no tracks and nothing above happens.

## AV-MAIN

`src/app/main.cpp` additionally:

- creates an `AlbumViewing` over the library shell and the settings,
  after the auto-playlist nodes;
- prints, for `--parts`, one more line directly after those of the earlier
  plans (the last of which is `auto-playlists=N`): `main-view=albums` or
  `main-view=tracks`. Later plans add lines of their own below it.

## Not in this version

- Album covers, and a cover as the heading.
- The other views of W-MPV: Album Covers, Artists, Jukebox.
- Choosing the grouping, the fields shown and the sort (MusicBee's
  "Customize Panel" and "Sort By").
- Collapsing an album, and showing only its first few tracks.
- Typing several letters on the jump bar; years on it when sorted by
  date.
- Dragging tracks onto an album to tag them.
- A playlist shown in its own order: a playlist node is grouped and
  ordered like any other.
- The other Play Now settings (queue next, queue last, play the
  selection only).

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| AV-SHELL | Which view a new install shows first. linuxBee starts with Album and Tracks. |
| AV-TEXT | What an album's heading shows by default, and in which order. |
| AV-GROUPS | Whether two albums of the same name by one artist in different years are one group. |
| AV-JUMP | Where `#` goes, and what a letter with no artist does (nothing, or the nearest). |
| AV-ACTIVATE | Whether a single click on a track selects it only, and what Return does. |
