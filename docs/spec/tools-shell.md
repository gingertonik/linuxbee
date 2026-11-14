# The tools in the window

The third assembly: Edit Tags, Organise Files, Convert Format and
Preferences open from the window on the selected tracks, and the library
is brought up to date with what they did.

This document is the contract for `src/ui/shell/toolsshell.h` and for what
`src/app/main.cpp` does with it. The locked tests
`tests/locked/tst_ui_toolsshell.cpp` and `tests/locked/cli_tools.sh` check
it.

The shell owns no rule. What each dialog does is decided in
tag-editor.md, organise-dialog.md, convert-dialog.md and preferences.md.

## Sources

| Tag | Source |
|---|---|
| W-FILE | MusicBee wiki, "File Menu", "Tools Menu", "Edit Menu": see main-window.md |
| TS-… | A linuxBee decision. |

## TS-COMMANDS

The shell implements four commands while it exists:

| Command | Opens |
|---|---|
| `file.editTags` | the tag editor (TS-TAGS) |
| `file.organise` | the organiser (TS-ORGANISE) |
| `tools.convert` | the converter (TS-CONVERT) |
| `edit.preferences` | the preferences (TS-PREFERENCES) |

Each creates its dialog with the main window as parent, connects to it,
hands it to the presenter, and destroys it when the presenter returns.
The presenter is `QDialog::exec()` unless another was given.

## TS-SELECTION

The first three work on the library shell's selected tracks, in the order
shown. With nothing selected no dialog is made and the message is
`Select one or more tracks first`.

## TS-TAGS

The editor is given one target per selected track: its library id, its
path and the tags the library holds for it. It saves with its default
saver.

When the editor reports files saved, the library reads each of those
tracks again (`Library::refresh`), and the message is
`Saved tags of N tracks` (`1 track`). A dialog that is closed without
saving changes nothing and gives no message.

## TS-ORGANISE

The organiser is given the selected paths and the settings, and uses its
default planner and executor.

When it reports a run, every outcome that is `Done`, whose target differs
from its source, and whose source file is no longer there, is a move: the
library track at the source gets the target as its path
(`Library::setPath`), so the track keeps its id and statistics. A `Done`
outcome whose source still exists was a copy and changes nothing in the
library. The message is `Organised: D done, S skipped, F failed`, the
three numbers of the report.

When the library holds another track at the target of such a move, that
track is removed first (`Library::remove`): its file is no longer
there, whether the move replaced it (O-REPLACED) or it had gone
before. The moved track then takes the target as above, keeping its id
and statistics. The statistics of the track removed are lost with it.

## TS-CONVERT

The converter is given the selected paths and the settings. When it
reports results, the message is `Converted N files` (`1 file`). The
results are not added to the library.

## TS-PREFERENCES

Needs no selection. The dialog gets one page, the Hotkeys page over the
window's action registry and the settings, so that applied hotkeys are
stored at once.

Other plans add pages with `addPreferencesPage(factory)`. Each time the
preferences open, every factory is called once, in the order added, and
the page it returns is added after Hotkeys; the dialog owns it. An empty
factory, or one that returns no page, adds nothing.
`preferencesPages()` is the list of titles the dialog would show if it
were opened now; it opens nothing and calls no presenter.

## TS-MESSAGE

Every message goes to the window's status bar and is kept as
`lastMessage()`.

## TS-MAIN

`src/app/main.cpp` additionally:

- creates a `ToolsShell` with the default presenter, after the library
  shell;
- understands `--commands` (added to the usage line of main-window.md,
  W-MAIN, Options): after everything has been created, and
  without showing the window, it prints the id of every command that is
  implemented, one per line in catalogue order, and ends with status 0.

## Not in this version

- Adding converted files to the library, or offering to.
- Preferences pages other than Hotkeys (other plans add theirs).
- The tagging tools (capitalise, search and replace, renumber, infer from
  file name), auto-tagging, volume analysis and the library import
  dialog: their core parts exist as plans; their windows do not.
- Opening the dialogs without blocking the window.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| TS-CONVERT | Whether converted files are added to the library by default. |
| TS-SELECTION | Whether Edit Tags with nothing selected does nothing, or edits the playing track. |
