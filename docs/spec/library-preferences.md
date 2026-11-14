# The Library page of the preferences

Where the monitored folders are chosen. What monitoring does is in
background-shell.md (BG-MONITOR, BG-STARTUP) and folder-monitor.md; what
any preferences page must do is in preferences.md (PF-x).

This document is the contract for `src/ui/preferences/librarypage.h`,
`src/ui/shell/librarypreferences.h` and what `src/app/main.cpp` does with
them. The locked tests `tests/locked/tst_ui_librarypage.cpp` and
`tests/locked/cli_pages.sh` check it.

## Sources

| Tag | Source |
|---|---|
| W-LP | MusicBee wiki, "Library Preferences": monitored folders, where new files go, check on startup (see folder-monitor.md) |
| LP-… | A linuxBee decision. |

## LP-STRUCTURE

The page's title is `Library`. Its widgets, by object name:

| Name | Widget | Shows |
|---|---|---|
| `monitoredList` | list | one row per monitored folder, the row's text being the folder |
| `addFolderButton` | button | always enabled |
| `removeFolderButton` | button | enabled only while a row is the current one |
| `sectionCombo` | combo box, not editable | the library's sections (`Library::categories()`, in that order) |
| `checkOnStartupBox` | check box | BG-STARTUP |

## LP-LOAD

Constructing the page, and `load()`, show what the settings hold in the
group `library`:

| Key | Shown |
|---|---|
| `monitored` | the folders, each tidied (`QDir::cleanPath`), in the stored order. Left out: an empty entry, a path that is not absolute, a folder already shown |
| `monitoredCategory` | the section; `Inbox` when missing or not a section |
| `checkOnStartup` | checked when the stored text is `true` (in any case of letters) or `1`; not checked otherwise |

Values are read as a later run of the program finds them, which is as
text: a single folder is one plain text, true is the text `true`.

`load()` drops every edit that was not applied.

## LP-ADD

The add button asks the `chooseFolder` hook, with the page as parent,
and adds what it answers. `addFolder(folder)` adds without asking.

The folder is tidied. It is refused, and nothing changes, when it is
empty or not absolute. When it is listed already nothing is added and
that row becomes the current one. Otherwise it goes to the end of the
list and becomes the current row. Whether the folder exists is not
checked: a drive may be unplugged. A folder inside a listed folder is
its own entry.

With no `chooseFolder` hook the page uses the desktop's folder chooser.

## LP-REMOVE

The remove button removes the current row. `removeFolder(folder)`
removes that folder (compared tidied) and says whether it was listed.

## LP-MODIFIED

The page is modified when what it shows differs from what is stored
(as LP-LOAD showed it): the list of folders in order, the section, the
check box. An edit that is undone leaves the page not modified.
Selecting a row is not an edit. `modifiedChanged` is emitted exactly
when the answer of `isModified()` changes.

## LP-APPLY

`apply()` never refuses. When the page is not modified it does nothing.
Otherwise it stores `monitored` (a list), `monitoredCategory` and
`checkOnStartup` (a truth value), after which the page is not modified,
and then calls the `stored` hook once. Other keys of the group are left
alone. Editing alone stores nothing.

## LP-INSTALL

`installLibraryPreferences(tools, background, settings, chooseFolder)`
adds a page factory to the tools shell (tools-shell.md, TS-PREFERENCES).
Each page it makes is over those settings, has that `chooseFolder`, and
has a `stored` hook that calls the background shell's `reload()`
(BG-RELOAD). So applying the page changes what is watched at once, with
the dialog still open, and a folder that was added is scanned at once.

## LP-MAIN

`src/app/main.cpp` additionally:

- calls `installLibraryPreferences` with the default chooser, after the
  background shell exists;
- understands `--pages` (added to the usage line): after everything has
  been created, and without showing the window, it prints the title of
  every preferences page, one per line in order, and ends with status 0.

## Not in this version

- The settle time on the page (it stays a key in `linuxbee.ini`).
- The library's own music folders, and everything else on MusicBee's
  Library page.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| LP-ADD | Settled 2026-11-14 (Dillon, from memory): it does, once the page is applied (BG-RELOAD). |
