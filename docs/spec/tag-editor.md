# The tag editor

One dialog that edits the tags of one track or of many at once.

This document is the contract for `src/core/tagedit/tagedit.h` and
`src/ui/tageditor/tageditordialog.h`. The locked tests
`tests/locked/tst_tagedit.cpp` and `tests/locked/tst_ui_tageditor.cpp`
check it.

## Sources

| Tag | Source |
|---|---|
| W-TE | MusicBee wiki, "Tag Editor": https://musicbee.fandom.com/wiki/Tag_Editor |
| E-… | A linuxBee decision. |

Read on 2026-11-13. What MusicBee documents: the editor opens for one
file or several (Shift+Enter); with several files a field whose values
differ is shown specially and is left alone unless the user changes it;
several artists or genres are entered separated by `;`; the editor has
further pages for artwork, lyrics and other tags. The exact wording and
layout are not documented.

## 1. The thinking (core)

### E-FIELDS

The editor shows these fields, in this order: Title, Artist, Album
Artist, Album, Year, Track#, Track Count, Disc#, Disc Count, Genre,
Composer, Grouping, Publisher, BPM, Rating, Love, Comment. Each is a field of
the tag catalogue (tag-reading.md, R-CATALOGUE).

### E-STATE

`fieldState(tracks, field)` says what a field shows:

- **Blank**: no track has a value.
- **Same**: every track has the same list of values, in the same order.
  The text is the values joined with `"; "`.
- **Mixed**: anything else, including "some have it and some do not" and
  the same values in a different order. No text.

One track is never mixed. No tracks, or a name outside the catalogue, is
blank. Field names are matched without regard to case.

### E-SPLIT

`valuesFromText(field, text)` turns typed text into values.

- A **list field** (Artist, Genre, Composer, Conductor, Lyricist, Mood,
  Mix Artist, Original Artist) is split at every `;`. Each part is
  trimmed; empty parts are dropped; a part that repeats an earlier one
  exactly is dropped. A comma is not a separator.
- Every other field is one value: the text, trimmed. Spaces inside are
  kept.
- Nothing left means an empty list: the field is to be removed.

### E-CHECK

`editProblem(field, text)` is empty when the text can be saved, and
otherwise one sentence:

| When | Sentence |
|---|---|
| the name is not a catalogue field | `NAME is not a tag field.` (the name as given) |
| Track#, Track Count, Disc# or Disc Count is not blank and not a whole number from 1 to 9999 (digits only; leading zeros allowed) | `FIELD must be a whole number from 1 to 9999.` |
| Rating is not blank and not one of 0.5, 1, 1.5 … 5 (`5.0` and `4.0` are accepted) | `Rating must be from 0.5 to 5 in steps of 0.5.` |
| Love is not blank and not `L` or `B`, in either case | `Love must be L (loved) or B (banned).` |

Blank text is always fine: it removes the field. Every other field takes
any text.

### E-BUILD

`buildEdits(tracks, edits)` returns one `TagChanges` per track, in order.
For each edit (a field name and its typed text):

- an edit E-CHECK refuses is skipped for every track;
- the values are those of E-SPLIT, except that the four number fields are
  written without leading zeros (`010` is `10`), Rating without a
  trailing zero (`5.0` is `5`), and Love in upper case (`l` is `L`);
- the field is named in a track's changes only if those values differ
  from the values the track has.

A field with no edit is never named. Only `fields` is used; `other` and
`pictures` stay untouched.

## 2. The dialog

### E-DIALOG

`TagEditorDialog` (`tagEditorDialog`) is given the tracks to edit, each
with an id, a path and its tags as read.

| Object name | What |
|---|---|
| `summaryLabel` | the file's name for one track; `N files` for any other number |
| `field.NAME` | a `QLineEdit` per E-FIELDS field, top to bottom in that order, each with a label that starts with the field's name |
| `problemLabel` | empty until something is wrong |
| `buttons` | a `QDialogButtonBox` at the bottom holding `saveButton` (the default button) and `cancelButton` |

**What a field shows.** Same: the text. Blank: nothing. Mixed: nothing,
with the placeholder `<multiple values>`. The Love field, when it is
not Mixed, has a placeholder that says what may be typed (it names
`L` and `B`).

**What counts as an edit.** A field is edited when its text differs from
what it showed at the start. A Mixed field is also edited once the user
has typed in it, even when nothing is left: that is how a field is
cleared on every track. Typing a value back to what it was is not an
edit. `edits()` gives the edited fields and their text;
`pendingChanges()` gives E-BUILD of them.

**Save** (the button, or `save()`):

1. Each edit is checked in field order (E-CHECK). At the first problem
   its sentence goes into `problemLabel`, that field gets the focus,
   nothing is saved and the dialog stays open.
2. Each track that has something to change is handed to the saver, in
   order. A failure does not stop the others.
3. `saved(ids)` is emitted once with the tracks whose files were written
   (status `Ok`), if there are any. A file that turned out unchanged
   counts as a success and is not listed.
4. If every save succeeded, `problemLabel` is cleared and the dialog
   closes as accepted. Otherwise `problemLabel` says
   `Could not save F of N: NAMES` (F the number of failures, N the number
   of tracks open, NAMES the file names with `, ` between) and the dialog
   stays open. Saving again writes only what is still to do.
   A file refused as `HardLinked` or `ChangedOnDisk` (tag-safe-save.md)
   is named with its reason, the result's first `problems` line, in
   round brackets after a space: `01.flac (the file has 2 names ...)`.

With nothing edited, Save closes the dialog without calling the saver.

**Cancel** closes the dialog as rejected and saves nothing.

With no tracks the fields and the Save button are disabled.

The saver is `writeTags` with default options unless `setSaver` replaced
it. The dialog does not read files, does not touch the library and does
not move anything; the caller acts on `saved`.

## Not in this version

- Artwork, lyrics, the sort fields and tags outside the list above.
- Stars for the rating; a drop-down of known genres or artists; a
  control of its own for Love (it is a text field like the others for
  now, and the Love command, with the rating commands, is the way to
  love a track without opening this dialog).
- Stepping to the previous and next track without closing.
- Auto-completion, "copy from the first file", undo.
- Editing in place in the track list.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| E-STATE | What a field with differing values shows, and how it is cleared for all files. |
| E-SPLIT | Whether Album Artist takes several values, and whether repeats are dropped. |
| E-FIELDS | The fields on the first page of the editor and their order. |
