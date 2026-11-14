# The Convert Format dialog

Which format, where the results go, what happens to files that are
already there, and a table that follows every file through the run.

This document is the contract for `src/ui/convert/convertdialog.h`. The
locked test `tests/locked/tst_ui_convert.cpp` checks it. Converting is
the converter's business (convert.md) and running many jobs is the
batch's (convert-batch.md); this dialog adds no rule about audio.

## Sources

| Tag | Source |
|---|---|
| W-CF | MusicBee wiki, "Convert Format": https://musicbee.fandom.com/wiki/Convert_Format |
| CD-… | A linuxBee decision. |

Read on 2026-11-12. What MusicBee documents: a dialog reached from
Tools, with the format to encode to, a destination (a folder, or the
same location), what to do if the file already exists, and whether tags
and artwork are carried over. Its layout and wording are not documented.

## CD-FRAME

`ConvertDialog` (`convertDialog`) is given the files to convert and a
settings object. It owns one `ConvertBatch`, reachable as `batch()`.

| Object name | What |
|---|---|
| `presetCombo` | `QComboBox`: the converter's presets in its order, shown by name, each item's data its id |
| `folderEdit` | `QLineEdit`: the folder for the results. Empty (with a placeholder saying so): next to the originals. |
| `browseButton` | opens a folder chooser and puts the choice into `folderEdit` |
| `ifExistsCombo` | `QComboBox`, four items whose data are `skip`, `overwrite`, `rename`, `updateTags`, in that order |
| `parallelSpin` | `QSpinBox`, 1 to 16: how many files are converted at a time |
| `copyTagsCheck`, `copyPicturesCheck` | `QCheckBox`es |
| `jobTable` | `QTreeWidget`, three columns headed `From`, `To`, `Status`, one row per file, no tree decoration |
| `summaryLabel` | `QLabel` |
| `progressBar` | `QProgressBar`, visible only during a run |
| `buttons` | `QDialogButtonBox` at the bottom with `runButton` and `closeButton` |

The options are above the table and the buttons below it.

## CD-OPTIONS

With nothing stored: the first preset, no folder, skip, copy tags and
artwork, and the batch's default number at a time.

`options()` is what the controls say: the preset's settings, the folder
trimmed, the choice for existing files, and the two boxes. With "copy
tags" off, "copy artwork" is disabled and counts as off, but keeps its
tick.

The settings group `convert` holds `preset` (the id), `folder`,
`ifExists` (the item data), `copyTags`, `copyPictures` and `parallel`.
They are written when a run starts, and at no other time. A stored
preset or choice that does not exist gives the first item; a stored
number outside 1 to 16 is pulled into that range.

## CD-TABLE

Whenever an option changes (and at the start), the table shows
`planBatch(files, options())`: source and destination, with an empty
Status. `plan()` is that list. `summaryLabel` shows `N files` (`1 file`
for one). `runButton` is enabled when there is at least one file.

## CD-RUN

`runButton`, or `run()`:

1. Does nothing and returns false when there are no files or a run is
   under way.
2. Stores the options (CD-OPTIONS), makes the output folder if one is
   named, sets the batch's number at a time from `parallelSpin`, and
   starts the batch with the plan shown. It returns true at once; the
   work goes on in the background.
3. While it runs: every option and `runButton` are disabled;
   `progressBar` is visible with the number of files as its maximum and
   the number finished as its value; each row's Status is `Waiting`, then
   a percentage such as `40%`, then its result.
4. When the batch has finished: the options and `runButton` are enabled
   again, `progressBar` is hidden, `summaryLabel` shows
   `C converted, S skipped, F failed`, with `, N cancelled` added when
   any were, and `converted(paths)` is emitted with the files that now
   hold a result (converted, or tags updated), in table order.
5. If nothing failed and nothing was cancelled, the dialog closes as
   accepted. Otherwise it stays open, and Convert can be pressed again.

| Result | Status text |
|---|---|
| Converted | `Converted` |
| Skipped | `Skipped: already there` |
| TagsUpdated | `Tags updated` |
| Cancelled | `Cancelled` |
| SourceNotFound | `File not found` |
| SourceNotAudio | `Not a music file` |
| InvalidJob | `Cannot convert: MESSAGE` |
| EncoderMissing | `ffmpeg was not found` |
| Failed | `Failed: MESSAGE` |

In the summary, C counts Converted and TagsUpdated, S counts Skipped, and
F counts everything else except Cancelled.

`closeButton`, Escape and the window's close button close the dialog as
rejected. During a run they instead ask the batch to stop, and the
dialog stays open.

## Not in this version

- Custom encoder settings beyond the presets (a bitrate box, a quality
  slider).
- Adding the results to the library; the caller does that from
  `converted`.
- Naming results by a template.
- A sound or a notification when a long run ends.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| CD-FRAME | The dialog's options and their wording, and the choices for a file that already exists. |
| CD-RUN | Whether the dialog closes by itself after a clean run. |
