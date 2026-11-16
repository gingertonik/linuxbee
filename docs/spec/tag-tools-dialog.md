# The tagging-tool dialog

One dialog serves every tagging tool: the tool's options on top, a
preview of what would change below, and Apply. This document covers the
dialog and the first two tools in it, Search and Replace and Capitalise.

It is the contract for the headers in `src/ui/tagtools/`. The locked
test `tests/locked/tst_ui_tagtools.cpp` checks it. What the tools do to
tags is in tagging-tools.md (G-x); nothing of that is decided here.

## Sources

| Tag | Source |
|---|---|
| W-TM, W-TT | MusicBee wiki, "Tools Menu" and "Tagging Tools" (see tagging-tools.md): the tools exist, Search and Replace has a preview |
| TT-… | A linuxBee decision. The layout of MusicBee's own dialogs is not documented in what could be read. |

## TT-PANEL: what a tool supplies

A tool is a `ToolPanel`: a widget with the tool's controls, which also
answers

- `title()`: the dialog's window title;
- `problem()`: empty when the options can be used, otherwise one sentence
  saying why not;
- `changes(targets)`: for each track handed in, in order, the changes the
  options would make;
- `load(settings)` and `save(settings)`: TT-REMEMBER.

It emits `optionsChanged` whenever one of its options changed.

## TT-STRUCTURE

The dialog's object name is `tagToolDialog`, its title the panel's. It
owns the panel and shows it above these widgets:

| Name | Widget | |
|---|---|---|
| `previewTable` | table, not editable | columns `File`, `Field`, `Now`, `Becomes` |
| `summaryLabel` | label | TT-PREVIEW, TT-APPLY |
| `applyButton` | button | |
| `cancelButton` | button | closes the dialog as rejected; nothing is saved |

## TT-PREVIEW

The preview is worked out when the dialog is built and again at every
`optionsChanged`. `pendingChanges()` is what it shows: one entry per
track, in order.

While the panel has a problem: the table is empty, `summaryLabel` shows
the problem, Apply is disabled and `pendingChanges()` is all empty.

Otherwise the table has one row for each thing that would change, tracks
in the order given, and within a track: the fields in code-point order
of their names, then the `other` keys in code-point order, then the
pictures.

| Change | File | Field | Now | Becomes |
|---|---|---|---|---|
| a field | the file's name, without its folder | the field's name | its values now, joined with `; ` | its new values, joined the same way |
| an `other` key | same | `other ` and the key | same | same |
| the pictures | same | `Pictures` | how many there are | how many there will be |

`summaryLabel` says `M of T tracks would change` (M the tracks with at
least one change, T the tracks open), and Apply is enabled. When M is 0
it says `Nothing to change` and Apply is disabled.

## TT-APPLY

Apply (the button, or `apply()`) does nothing and returns false while
there is a problem or nothing to change. Otherwise:

1. Each track that has changes is handed to the saver, in order. A
   failure does not stop the others.
2. `saved(ids)` is emitted once with the tracks whose files were written
   (status `Ok`), if there are any. A file that turned out unchanged
   counts as a success and is not listed.
3. If every save succeeded, the panel's options are stored (TT-REMEMBER)
   and the dialog closes as accepted.
4. Otherwise the dialog stays open. The tracks that were saved now have
   their new tags, so the preview shows only what is still to do, and
   `summaryLabel` says `Could not save F of T: NAMES` (F the number of
   failures, T the tracks open, NAMES the file names with `, ` between).
   Applying again writes only what is still to do.

The saver is `writeTags` with default options unless `setSaver` replaced
it. The dialog reads no file and does not touch the library; the caller
acts on `saved`.

## TT-WARN: asked before it cannot be taken back

There is one step of undo (tag-undo.md), and it is lost at the next tag
change. A panel whose changes cannot be made good by hand
gives a warning: `warning(tracks)` is a yes-or-no question for a run
that would change `tracks` files, or empty when there is nothing to
warn about (the default).

Apply, after it has found no problem and something to change, asks the
panel for its warning, with the number of tracks that would change.
When that is not empty it is put to `confirm` (`setConfirm`; by default
a dialog whose default answer is "no"), once for each Apply. On "no"
Apply does nothing and returns false: no file is handed to the saver,
`saved` is not emitted, the options are not stored, and the dialog
stays open as it was. On "yes" it goes on as TT-APPLY says. A preview
asks nothing.

## TT-REMEMBER

When the dialog was given settings, it calls the panel's `load` before
the first preview, and the panel's `save` when an Apply succeeded
completely. Cancel stores nothing. Values are read as a later run finds
them, which is as text.

## TT-REPLACE: the Search and Replace panel

Title `Search and Replace`.

| Name | Widget | Option (G-REPLACE) |
|---|---|---|
| `findEdit` | line edit | `find` |
| `replaceEdit` | line edit | `replace` |
| `fieldCombo` | combo box | `fields`: the first entry, `All fields`, means an empty list; then one entry for each catalogue field except Rating, in catalogue order, meaning that field alone |
| `regexBox` | check box | `regex` |
| `matchCaseBox` | check box | `matchCase` |
| `wholeWordBox` | check box | `wholeWord` |

`options()` is what the controls say. `problem()` is
`validateSearchReplace(options())`; `changes` is `searchReplace` for each
track. A new panel has empty texts, `All fields`, and nothing checked.

Remembered, in the group `tagtools/replace`: `field` (the field's name,
or empty for all), `regex`, `matchCase`, `wholeWord`. The two texts are
not remembered. A stored field that is not offered means `All fields`.

## TT-CASE: the Capitalise panel

Title `Capitalise`.

| Name | Widget | Option (G-CASE) |
|---|---|---|
| `styleCombo` | combo box | `style`. Entries, in order, with their item data: `Each Word` (`each-word`), `Title Case` (`title`), `Sentence case` (`sentence`), `UPPER CASE` (`upper`), `lower case` (`lower`) |
| `fieldList` | list of rows that can be ticked | `fields`: the ticked rows, in list order. One row for each catalogue field except Rating, in catalogue order |

A new panel has `Each Word` and ticks at Title, Artist, Album and Album
Artist. `smallWords` is always left empty (the default list).

With no row ticked, `problem()` is `Choose at least one field` and
`changes` are all empty. Otherwise there is no problem and `changes` is
`capitalise` for each track.

Remembered, in the group `tagtools/case`: `style` (the item data) and
`fields` (the ticked names). An unknown style means `Each Word`. When
none of the stored names is a row, the four default rows are ticked.

## TT-REMOVE: the Remove Tags panel

Title `Remove Tags`. Class `RemoveTagsPanel`, `tagtools/removetagspanel.h`.
(MusicBee wiki, "Tools Menu": "Remove Tags: Clear a tag or tags from all
selected tracks.")

| Name | Widget | Option (G-REMOVE) |
|---|---|---|
| `fieldList` | list of rows that can be ticked | `fields`: the ticked rows, in list order. One row for each catalogue field except Rating, in catalogue order |
| `allTagsBox` | check box, `All tags` | `allTags`. While it is ticked the list is disabled |
| `picturesBox` | check box, `Embedded pictures` | `removePictures` |

A new panel has nothing ticked. With no row ticked and neither box
ticked, `problem()` is `Choose what to remove` and `changes` are all
empty. Otherwise there is no problem and `changes` is `removeTags` for
each track.

**The warning (TT-WARN).** With N the files (`1 file`, `3 files`,
`17,000 files`, a comma between thousands):

| Ticked | `warning` |
|---|---|
| `All tags`, not the pictures | `Remove every tag from N? The rating and tags that are not in the list go too. Embedded pictures stay. Undo brings them back, until the next tag change or until the program closes.` |
| `All tags` and `Embedded pictures` | `Remove every tag and the embedded pictures from N? The rating and tags that are not in the list go too. Undo brings them back, until the next tag change or until the program closes.` |
| `Embedded pictures`, not `All tags` (with or without rows) | `Remove the embedded pictures from N? Undo brings them back, until the next tag change or until the program closes.` |
| rows of the list only | none: a field can be typed in again |

Nothing is remembered: `load` reads nothing and `save` writes nothing,
so that what was removed from one selection is never ticked already for
the next. No key under `tagtools/remove` is written.

## TT-RENUMBER: the Renumber Tracks panel

Title `Renumber Tracks`. Class `RenumberPanel`, `tagtools/renumberpanel.h`.
(MusicBee wiki, "Tools Menu": "Renumber Tracks: Fix track numbering for
an album.")

| Name | Widget | Option (G-RENUMBER) |
|---|---|---|
| `startSpin` | spin box, 0 to 99999 | `start` |
| `digitsSpin` | spin box, 0 to 9 | `digits` |
| `totalBox` | check box, `Also write the track count` | `writeTotal` |

A new panel has 1, 0 and no tick. There is never a problem. `changes`
is `renumberTracks` over the tags of the targets, in the order the
targets were given: the caller decides the order (tools-shell.md,
TS-SELECTION: the order the list shows).

Remembered, in the group `tagtools/renumber`: `digits` and `total`. A
stored `digits` that is not a whole number from 0 to 9 means 0; a
stored `total` other than `true` means no tick. The start is not
remembered and not read: a new panel always starts at 1.

## TT-INFER: the Infer Tags From Filename panel

Title `Infer Tags From Filename`. Class `InferPanel`,
`tagtools/inferpanel.h`.

| Name | Widget | Option (G-INFER) |
|---|---|---|
| `patternEdit` | line edit | the pattern |
| `blankOnlyBox` | check box, `Only fill fields that are empty` | `blankFieldsOnly` |

A new panel has the pattern `<Track#> - <Title>` and the box ticked.
`problem()` is `validateInferPattern` of the pattern. `changes` is
`inferTags` for each track, with the track's path; a file whose name
does not fit the pattern gets no changes.

Speed: a preview makes the pattern ready once (tagging-tools.md,
`InferPattern`), however many tracks are open; the preview is worked
out at every keystroke. A pattern with a problem makes nothing ready.

Remembered, in the group `tagtools/infer`: `pattern` and `blankOnly`. A
stored pattern that is empty or only spaces means the new panel's
pattern; any other is shown as it is, with its problem if it has one.
A stored `blankOnly` other than `false` means ticked.

## Not in this version

- Remove Tags: removing a tag that is outside the catalogue, or the
  rating. Renumber Tracks: numbering each disc on its own. Infer From
  Filename: a list of patterns used before, and saying which file names
  do not fit. Copy and swap (G-COPY) have no panel and no command.
- Editing the small-word list; choosing several fields in Search and
  Replace; remembering the texts searched for.
- Unticking single rows of the preview to leave them out.
- Undo.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| TT-PREVIEW | Whether MusicBee's preview lets single changes be left out. |
| TT-CASE | Which styles and fields MusicBee's Capitalise dialog offers. |
