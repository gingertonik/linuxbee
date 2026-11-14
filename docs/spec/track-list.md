# The track list

The middle of the window: a list of tracks in columns, sorted, narrowed
by a search box, with a line under it saying how much is shown.

This document is the contract for `src/core/sort/fieldsort.h`,
`src/ui/tracklist/tracklistmodel.h` and
`src/ui/tracklist/tracklistpage.h`. The locked tests
`tests/locked/tst_fieldsort.cpp` and `tests/locked/tst_ui_tracklist.cpp`
check it.

The list is given tracks and shows them. Where they come from (the
library, a playlist, the inbox) and what a double-click does are other
plans' business.

## Sources

| Tag | Source |
|---|---|
| W-MP | MusicBee wiki, "Main Panel" (track views, the header, the search box): https://musicbee.fandom.com/wiki/Main_Panel |
| W-SORT | MusicBee wiki, "Sorting/Grouping" preferences ("ignore words"): https://musicbee.fandom.com/wiki/Sorting/Grouping_Preferences |
| T-… | A linuxBee decision. |

Read on 2026-11-13. What MusicBee documents: a track view with columns
the user chooses, sorting by clicking a header, a search box that narrows
the list as the user types, a summary of count, size and length in the
status bar, and a preference listing words (by default "The") that are
ignored at the start of a name when sorting. How it compares two values
exactly is not documented.

## 1. Order (core)

### T-KIND

A field is compared as a number, a length, a size, or text.

| Kind | Fields |
|---|---|
| Number | Track#, Disc#, Track Count, Disc Count, BPM, Play Count, Skip Count, Bitrate, Sample Rate, Channels, Rating |
| Duration | Time |
| Size | Size |
| Text | every other name, known or not |

Names are matched without regard to case. Year and the date fields are
text: `yyyy` and ISO 8601 dates sort correctly as text.

### T-COMPARE

`compareFieldValues(field, a, b, options)` is negative when `a` comes
first, positive when `b` does, and 0 when neither does. It is a proper
ordering: comparing the other way round gives the opposite sign.

**Number.** The value is the number the text starts with (`3/12` is 3,
`320k` is 320, `120.5` is 120.5; a leading minus counts). Smaller first.
A value that does not start with a number comes after every number, and
two such values tie.

**Duration.** `m:ss` or `h:mm:ss`, compared as seconds. Anything else is
treated as a number, and what is not a number comes last.

**Size.** A number and a unit, `KB`, `MB`, `GB` or `TB` in steps of 1024
(no unit: bytes), compared as bytes. Anything else comes last.

**Text.**

1. Both values are folded as for search (`foldForSearch`: lower case,
   accents removed) and trimmed.
2. If a value then starts with one of `options.ignoreWords` (folded the
   same way) followed by a space and something more, that word and the
   space are dropped. One word only: "The The" sorts as "the". By default
   the list is "The" (W-SORT).
3. A blank value comes after every value that is not blank.
4. Otherwise the values are compared left to right. A run of digits in
   both at the same place is compared as a number of any length, leading
   zeros ignored (`Track 2` before `Track 10`; `Part 007` ties with
   `Part 7`). Other characters are compared by their code. A value that is
   the start of the other comes first.

No locale is consulted: the order is the same on every machine.

### T-SUMMARY

`formatTrackSummary(count, bytes, ms)` gives `COUNT tracks, SIZE, TIME`:

- the count with a comma between thousands, and `track` for exactly one;
- the size in MB to one decimal below 1 GB (1,073,741,824 bytes), in GB
  to one decimal from there;
- the time as `h:mm:ss`, and as `Nd hh:mm:ss` from one day up.

Negative input counts as 0: `0 tracks, 0.0 MB, 0:00:00`.

## 2. The model

### T-MODEL

`TrackListModel` is a flat table: one row per track shown, one column per
field name in `columns()`.

**Columns.** By default Track#, Title, Artist, Album, Year, Genre, Rating,
Time. `setColumns` takes any field names; it refuses an empty list, a
blank name, and a name given twice (in any case), changing nothing. A cell
shows what `LibraryFieldSource` gives for the field, and is empty when the
track has no value or the field does not exist. The header shows the field
name.

**Roles.** `Qt::DisplayRole` the text; `Qt::ToolTipRole` the file's path;
`Qt::TextAlignmentRole` right for number, duration and size columns and
left for text, vertically centred, for cells and headers alike;
`TrackIdRole` the track's id; `MissingRole` whether its file was missing
at the last scan. Nothing is editable in place.

**Order.** `sortFields()` is a list of field names, the weightiest first;
`-` in front of a name means descending. By default Album Artist, Album,
Disc#, Track#, Title. Each key compares by T-COMPARE, except that a blank
value comes last in both directions. Tracks that tie on every key are in
order of path. With no keys the list is in order of path.

`sort(column, order)`, which is what a click on a header does, makes that
column's field the first key and puts the default keys after it, leaving
out the field itself. A column that does not exist is ignored.

**The order given (T-GIVEN).** `setGivenOrder(true)` shows the tracks in
the order they were given instead of sorting them: the order of the last
`setTracks`, a track that `upsertTracks` adds going to the end and one it
replaces keeping its place. The filter narrows as before and the order
stays. `sortFields()` is kept and may be set; it decides nothing until
the given order is switched off, and then the list is sorted by it
again. Switching it on again shows the order given, which is still
known. `sort(column, order)` switches the given order off and sorts,
also when the keys come out the same as they were. `givenOrder()` is
false for a new model. Changing it resets the model once; setting what
it already is does nothing. It is not stored: `save` and `load` neither
write nor change it.

**Filter.** `setFilter(query)` shows only the tracks the search matches
(the search plan's `SearchQuery`, against `LibraryFieldSource`). The empty
query shows everything. `totalCount()` is the number before the filter.

**Changes.** `setTracks` replaces everything. `upsertTracks` adds tracks
and replaces those whose id is already there. `removeTracks` takes ids
away; unknown ids are ignored. After each, the order and the filter still
hold.

**Summary.** `summary()` is T-SUMMARY over the rows shown.
`summaryChanged()` is emitted when the tracks or the filter changed; not
for a call that changed nothing.

**Stored.** `save(settings, group)` writes `columns` and `sort` as string
lists under the group; `load` reads them back. Columns that are missing
or unusable give the default columns; a missing `sort` gives the default
order. Each list on screen uses its own group.

## 3. The page

### T-PAGE

`TrackListPage` (`trackListPage`) is, from top to bottom:

| Object name | What |
|---|---|
| `searchBox` | a `QLineEdit` with a placeholder text and a clear button |
| `trackView` | a `QTreeView` on the model: no tree decoration, whole rows selected, several rows at once, uniform row heights, nothing editable, columns the user can drag, a sort indicator in the header |
| `summaryLabel` | a `QLabel` showing the model's summary, kept up to date |

- **Search.** Typing in the box filters the list at once, with no Return
  needed. `setSearchText` does the same from code.
- **Sorting.** A click on a header calls the model's `sort`.
- **Selection.** `selectedTrackIds()` gives the selected tracks in the
  order shown. `selectTracks(ids)` selects exactly those of the ids that
  are shown. The selection belongs to the tracks, not the rows: after a
  sort, a filter or an update, the same tracks are selected, less those no
  longer shown. `selectionChanged()` is emitted when the set of selected
  tracks changed, and not when only the order did.
- **Activation.** A double-click, or Return on the current row, emits
  `trackActivated(id)`.
- **Context menu.** A right-click on a row opens `trackContextMenu`, which
  holds the registry's `Context` branch.
- **Commands.** The page implements `edit.selectAll` (all rows shown) and
  `edit.find` (the search box gets the focus with its text selected).

## Not in this version

- Album-and-tracks, artwork and other views; grouping headers.
- Choosing columns from a menu on the header; column widths remembered.
- Editing a cell in place (F2), drag and drop.
- Stars for ratings; a different look for missing files.
- Lists too large to hold in memory at once.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| T-COMPARE | Does "Track 2" sort before "Track 10"? Are accents ignored? Where do blank values go? |
| T-COMPARE | Do "ignore words" apply to titles and albums, or only to artists? |
| T-MODEL | What does a click on a header use as second and later sort keys? |
| T-SUMMARY | The exact wording of the status bar summary. |
