# The Organise Files dialog

Where the selected files should go, what they will be called, a preview
of every move before anything happens, and the button that does it.

This document is the contract for `src/ui/organise/organisedialog.h`.
The locked test `tests/locked/tst_ui_organise.cpp` checks it. The moving
itself is the organiser's (organise.md); this dialog adds no rule about
files.

## Sources

| Tag | Source |
|---|---|
| W-O | MusicBee wiki, "File Organization" (the Organize Files dialog, its naming template, its preview, its options): https://musicbee.fandom.com/wiki/File_Organization |
| OD-… | A linuxBee decision. |

Read on 2026-11-13. What MusicBee documents: a dialog reached from the
File menu or Ctrl+R, with a target folder, a naming template, a preview
of the resulting paths, an option to copy instead of move, and what to do
with files that have missing naming tags. Its exact layout and wording
are not documented.

## OD-FRAME

`OrganiseDialog` (`organiseDialog`) is given the files to organise and a
settings object.

| Object name | What |
|---|---|
| `targetEdit` | `QLineEdit`: the folder to organise into. Empty: each file stays in its folder and is only renamed. |
| `browseButton` | opens a folder chooser and puts the choice into `targetEdit` |
| `templateEdit` | `QLineEdit`: the naming template |
| `problemLabel` | `QLabel`: empty unless the template cannot be used |
| `moveRadio`, `copyRadio` | `QRadioButton`s |
| `replaceCheck` | replace files that are already there |
| `otherFilesCheck` | take a folder's other files along (move only) |
| `removeEmptyCheck` | remove folders left empty (move only) |
| `skipMissingCheck` | leave files with missing naming tags where they are |
| `previewTable` | `QTreeWidget`, three columns headed `From`, `To`, `Status`, one row per file, no tree decoration |
| `summaryLabel` | `QLabel` |
| `progressBar` | `QProgressBar`, visible only during a run |
| `buttons` | `QDialogButtonBox` at the bottom with `runButton` and `closeButton` |

The options are above the preview and the buttons below it.

## OD-OPTIONS

With nothing stored the controls show the organiser's defaults: no
folder, the template `<Album Artist>\<Album>\<Disc-Track#> <Title>`,
move, do not replace, leave other files, leave emptied folders, leave
files with missing tags.

`options()` is what the controls say: the folder trimmed and cleaned
(`QDir::cleanPath`; empty stays empty), the template parsed, and the
rest one to one. When Copy is chosen the two "move only" boxes are
disabled and count as off, but keep their ticks for when Move is chosen
again.

The settings group `organise` holds `target`, `template`, `copy`,
`replace`, `otherFiles`, `removeEmpty` and `skipMissing`. They are
written when a run starts, and at no other time: what the user only
tried out and closed is not remembered.

## OD-TEMPLATE

When the template text does not parse, `problemLabel` shows
`Template: MESSAGE (at character N)`, with the template engine's message
and its position counted from 1. The planner is not called, the preview
and the summary are empty, and `runButton` is disabled.

## OD-PREVIEW

Whenever a control changes (and at the start, and when the planner is
replaced), the planner is called with the files and `options()`, and the
table shows its answer: the source, the target, and the status in words.

| Planned status | Words |
|---|---|
| Ready | `Ready`; `Renamed` when the move got a numbered name; `Replaces a file` when it replaces one |
| AlreadyInPlace | `Already in place` |
| MissingTags | `Missing: ` and the field names with `, ` between |
| EmptyName | `No file name` |
| InvalidTemplate | `Invalid template` |
| NotFound | `File not found` |
| NotAudio | `Not a music file` |

`summaryLabel` shows `R to move, P already in place, C cannot be done`
(`to copy` when copying), where R counts Ready, P AlreadyInPlace and C
the rest. `runButton` is enabled when R is above 0.

`preview()` is the plan the table shows.

## OD-RUN

`runButton`, or `run()`:

1. Does nothing and returns false when `runButton` is disabled or a run
   is under way.
2. Stores the options (OD-OPTIONS).
3. Hands the plan shown, exactly as shown, and `options()` to the
   executor. While it works, `progressBar` is visible and follows the
   executor's progress calls (maximum the count, value the index), and
   the window keeps answering.
4. Afterwards the Status column shows what happened to each planned
   file: `Done`, `Failed: MESSAGE` or `Skipped: MESSAGE`; a row the run
   never came to, because it was stopped first, shows `Not reached`.
   `summaryLabel`
   shows `D done, S skipped, F failed`, with ` (stopped)` added when the
   run was stopped.
5. `organised(report)` is emitted.
6. If nothing failed and the run was not stopped, the dialog closes as
   accepted. Otherwise it stays open with `runButton` disabled, since
   the table now shows results and not a plan; changing any option makes
   a fresh preview.

`closeButton` closes the dialog as rejected. During a run it instead asks
the run to stop: the executor's next progress call is answered with
false.

A run cannot be left behind a closed window. During a run the Escape
key and the window's own close button do what `closeButton` does then:
they ask the run to stop, and the dialog stays open until the run has
ended and shows what happened. With no run under way they close the
dialog as rejected.

"The plan shown, exactly as shown" (step 3) means the planner is not
asked again when the run starts: what the user read in the table is what
is handed to the executor.

The planner and executor are `planOrganise` and `executeOrganise` unless
replaced with `setPlanner` and `setExecutor`.

## Not in this version

- Buttons that insert a field into the template; saved template presets.
- The preview worked out in the background for very large selections.
- Organising automatically as files are added, and the per-folder rules
  of MusicBee's Library preferences.
- Undo.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| OD-FRAME | The dialog's options and their wording. |
| OD-OPTIONS | Whether the last-used folder and template are remembered when the dialog is cancelled. |
| OD-RUN | Whether the dialog closes by itself after a clean run. |
