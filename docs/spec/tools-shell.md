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
| W-FILE | MusicBee wiki, "File Menu", "Tools Menu", "Edit Menu": https://musicbee.fandom.com/wiki/File_Menu , https://musicbee.fandom.com/wiki/Tools_Menu , https://musicbee.fandom.com/wiki/Edit_Menu |
| TS-… | A linuxBee decision. |
| W-ANS | MusicBee wiki, the pages that answer the questions under "Answered from the MusicBee wiki": "Convert Format": https://musicbee.fandom.com/wiki/Convert_Format |

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "File Menu" (3.0.5899; 2020-05-01);
"Tools Menu" (3.0.5902; 2024-08-27); "Edit Menu" (3.0.5899; 2016-03-11).

## TS-COMMANDS

The shell implements nine commands while it exists:

| Command | Opens |
|---|---|
| `file.editTags` | the tag editor (TS-TAGS) |
| `file.organise` | the organiser (TS-ORGANISE) |
| `tools.convert` | the converter (TS-CONVERT) |
| `edit.preferences` | the preferences (TS-PREFERENCES) |
| `tools.tagging.searchReplace` | the tagging-tool dialog with the search and replace panel (TS-TOOLS) |
| `tools.tagging.capitalise` | the tagging-tool dialog with the capitalise panel (TS-TOOLS) |
| `tools.tagging.removeTags` | the same dialog with the Remove Tags panel (tag-tools-dialog.md, TT-REMOVE) |
| `tools.tagging.renumber` | the same dialog with the Renumber Tracks panel (TT-RENUMBER) |
| `tools.tagging.inferFromFilename` | the same dialog with the Infer Tags From Filename panel (TT-INFER) |

It also implements `edit.undo` (TS-UNDO), which opens nothing.

Each creates its dialog with the main window as parent, connects to it,
hands it to the presenter, and destroys it when the presenter returns.
The presenter is `QDialog::exec()` unless another was given.

## TS-SELECTION

All but the preferences work on the library shell's selected tracks, in the order
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

**Not kept.** When a save of the tag editor or of a tagging tool comes
back with lines in `WriteResult::notKept` (tag-writing.md, W-ID3VER),
the message goes on: `; not kept in ID3v2.3: NAMES (K files)`
(`1 file`). NAMES are the names of the lines, the part before `: `,
each once, sorted, joined with `, `; K is the number of files that
had any. So: `Saved tags of 2 tracks; not kept in ID3v2.3: Year (1
file)`. What is told belongs to that message alone: the next save
starts with nothing. Undo names nothing.

## TS-TOOLS

A tagging tool is `TagToolDialog` (tag-tools-dialog.md) with that
tool's panel, one target per selected track as TS-TAGS makes them, and
the shell's settings, so that the options are remembered (TT-REMEMBER).
It saves with its default saver.

When the dialog reports files saved, the library reads each of those
tracks again (`Library::refresh`), once each and no other track, and
the message is that of TS-TAGS. A dialog that is closed without
applying changes nothing and gives no message.

**Pictures.** The library keeps how many pictures a track has and
none of them (library.md, L-STORE), so the tags it hands out have an
empty picture list. A tool that removes pictures must know which
tracks have any: the targets the shell gives the tagging tools carry
one picture entry, without data, for each picture the library counts
(`LibraryTrack::pictureCount`), as a read without picture data would
give them. The entries are for counting only; nothing writes them
back.

## TS-UNDO

The shell owns one `TagUndo` (tag-undo.md), `tagUndo()`, with a
scratch folder of its own that is removed when the shell goes.

Every file the tag editor and the tagging tools save goes through it:
the shell gives each dialog a saver that is `TagUndo::write`, with
`begin(the dialog's window title)` before the dialog is presented and
`end()` after. So the step is named `Edit Tags`, `Search and Replace`,
`Remove Tags` and so on, and a dialog closed without a change leaves
the step before it.

`edit.undo` (Edit > Undo, Ctrl+Z) asks nothing:

0. While a change is open (between `begin` and `end` of a dialog the
   shell presented): the message `Undo is not available while a change
   is being made`, and nothing else. The step before it is not touched.
1. With no step: the message `Nothing to undo`, and nothing else.
2. Otherwise `TagUndo::undo()`; then each restored file that the
   library knows is read again (`Library::refresh`), once.
3. The message: `Undid NAME: N files put back` (`1 file`); after it,
   when any was no longer the file that was edited (UN-SAME),
   `; K changed or moved since, and left as they are` (for one,
   `; 1 changed or moved since, and left as it is`); and after that,
   when any failed, `; K could not be put back (Undo tries again)`.
   Those files are still in the step (tag-undo.md, UN-KEEP), so
   `edit.undo` again is this point again, for them alone, with `0
   files put back` when none went.
   When none failed and some were changed or moved since, those are
   all that is left in the step, and another Undo cannot put them
   back as they are. The message then ends, after the "changed or
   moved since" part and a space, with
   `(a new edit drops this)`, and promises no second try. (What is kept for them stays until the next tag
   change or the end of the program; if the very file comes back to
   its path, Undo puts it back.)

The shell tells its `TagUndo` of every move the library reports
(`Library::pathsMoved` to `TagUndo::moved`, UN-MOVED), so a change is
undone in the file where Organise or a relink has put it since.

Because the removed pictures and tags come back, the questions of
TT-WARN end with `Undo brings them back, until the next tag change or until the program closes.`
and no longer say that the change cannot be undone. The questions
stay: undo is one step, and the next change loses it.

### The run folder

The undo keeps removed pictures in files (tag-undo.md). Where:

- The root is `linuxbee/undo` under the user's cache folder
  (`QStandardPaths::GenericCacheLocation`), never the temporary
  folder and never a music folder.
- Each shell makes one run folder there when it starts, `run-` and
  six characters, open to the user alone (mode 0700). It holds
  `lock`, a `QLockFile` the shell holds for as long as it lives, and
  `pictures`, the folder the `TagUndo` is given.
- When the shell goes, the lock is let go and the run folder is
  removed with what is in it.

A run that was killed, or ended by a signal, leaves its folder and
the covers in it. So at start, before its own folder is made, the
shell tidies the root. For every entry whose name begins `run-`:

| The entry is | Then |
|---|---|
| a link, or anything that is not a folder | removed itself; a link is never followed |
| a folder whose `lock` can be taken at once | its run is dead: the folder is removed with what is in it |
| a folder whose `lock` is held | left exactly as it is |

Whether a lock is held is judged by its holder being alive, never by
the age of the file (`setStaleLockTime(0)`): a program left open for
a week still holds its folder. Entries with other names are not
touched. Nothing outside the root is removed.

**The root is a real folder.** `undo` itself, the last name of the
root, must be a folder and not a link. When it is a link it is not
followed: nothing is tidied through it, no run folder is made through
it, and the link is left as it is. The shell then has no run folder.
A folder above it may be a link (a cache kept on another disk).

**One start at a time.** Tidying the root and making the run folder
(with its lock taken) happen while the shell holds `start.lock`, a
`QLockFile` in the root, judged by its holder being alive like the
others. It is let go as soon as the start is done. So one program's
tidying never meets a run folder another has made and not yet
locked. A shell that cannot have `start.lock` within two seconds
tidies nothing and has no run folder. `start.lock` is not a run
folder and is never removed as one.

**No run folder means no picture kept.** When the shell has no run
folder (the cache folder cannot be written, the root is a link, the
start lock could not be had), it still starts, and the `TagUndo` is
made with no folder at all (tag-undo.md, UN-NOKEEP): a change that
would need a picture kept is refused, for as long as this shell
lives, whatever becomes of the disk meanwhile. No other place is
used instead. Every other change is saved and undone as usual.

Known and left: a run folder whose `lock` cannot be read or taken
for a reason other than a live holder (its permissions were changed)
is taken for live and stays until the user removes it. It holds
nothing but kept covers.

A program ended by a signal does not tidy up after itself; the next
start does. (Ending cleanly on SIGTERM is the application's matter
and not part of this rule.)

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

The moved files of one run are recorded by one `Library::recordMoves` call
(library.md, L-MOVED): every outcome that is `Done`, has `moved` set
(organise.md) and a target other than its source, as `{source,
target}`, in the report's order. The shell decides nothing else: not
by looking at the disk, and not which track goes. So the playlists
and the queue are brought up to date once for the run.

When the library emits `failed` during that call (library.md,
L-FAILED), the files are where Organise put them and the library is
as it was. The line is then the usual one followed by `; the library
could not be changed and still lists the files where they were`.

One run has one line. When, during that call, the library shell said
that playlists were not updated (library-shell.md, SH-MOVED), the
tools shell's line, shown after it, would take its place in the status
bar before anyone could read it. So the line of the run is then the
usual one, `; `, and that sentence whole, as in `Organised: 1 done, 0
skipped, 0 failed; 1 playlist was not updated for the files that moved
(it could not be read or written): NAME`. That is what the status bar
shows when the command has returned, and what `lastMessage()` answers.
Only what was said during this run's call counts: a run that left no
playlist behind, or recorded no move, has the usual line and no more,
whatever an earlier run said.

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
- The other tagging tools (remove tags, renumber, infer from
  file name), auto-tagging, volume analysis and the library import
  dialog: their core parts exist as plans; their windows do not.
- Opening the dialogs without blocking the window.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| TS-SELECTION | Whether Edit Tags with nothing selected does nothing, or edits the playing track. |

### Answered from the MusicBee wiki (checked 2026-11-20)

| Rule | Question | What the wiki says |
|---|---|---|
| TS-CONVERT | Whether converted files are added to the library by default. | No: "add converted files to the inbox" is unticked ("Convert Format", version 2.1). TS-CONVERT already does not. |
