# Preferences

The preferences window, what one page of it must offer, and its first
page: Hotkeys.

This document is the contract for `src/ui/preferences/preferencesdialog.h`
and `hotkeyspage.h`. The locked test `tests/locked/tst_ui_preferences.cpp`
checks it.

Other pages (library folders, tags, player, file converter) are added by
the plans that own those settings; each is a `PreferencesPage`.

## Sources

| Tag | Source |
|---|---|
| W-PREF | MusicBee wiki, "Preferences": https://musicbee.fandom.com/wiki/Preferences |
| W-HK | MusicBee wiki, "Hotkeys Preferences": https://musicbee.fandom.com/wiki/Hotkeys_Preferences |
| PF-…, HK-… | A linuxBee decision. |

What MusicBee documents: a preferences window with a list of sections on
the left, Apply, Save and Cancel; and a Hotkeys section that lists every
command, lets the user type a key for one, and can restore the defaults.

## 1. The window

### PF-FRAME

A dialog titled `Preferences` with these named widgets:

| Name | Kind | Is |
|---|---|---|
| `pageList` | `QListWidget` | the titles of the pages, at the left |
| `pageStack` | `QStackedWidget` | the page chosen, taking the room |
| `errorLabel` | `QLabel` | why the last Apply or Save stopped; otherwise empty |
| `saveButton`, `applyButton`, `cancelButton` | `QPushButton` | PF-APPLY, PF-CANCEL |

### PF-PAGES

A page offers a title, `load()` (show what is stored; forget edits),
`apply()` (store the edits; answer nothing, or the reason it could not),
`isModified()` and the signal `modifiedChanged`.

`addPage(page)` appends the page, takes ownership of it and calls its
`load()`. The first page added is the one shown. Choosing a title in the
list shows that page. `selectPage(title)` does the same by exact title and
answers false for a title that no page has. `pages()` lists the titles in
the order added and `currentPage()` is the title shown (empty with no
pages).

### PF-APPLY

`applyButton` is enabled when at least one page is modified.

`apply()`, which is what the Apply button does:

1. Clears the error label.
2. Calls `apply()` on each modified page, in the order added. Pages not
   modified are left alone.
3. When a page answers with a reason: the error label shows the page's
   title, a colon, a space and the reason; that page is shown; the pages
   after it are not tried; the answer is false.
4. Emits `applied()` once if at least one page stored its edits, also when
   a later page refused.

With nothing modified it does nothing and answers true.

Save does the same and, when the answer is true, closes the dialog as
accepted. Save with nothing modified just closes.

### PF-CANCEL

Cancel, Escape and closing the window do one thing: every page's `load()`
is called, in the order added, so that all edits are forgotten; the error
label is cleared; the dialog closes as rejected.

## 2. The Hotkeys page

Its title is `Hotkeys`. It works on an action registry (main-window.md)
that it is given.

### HK-TABLE

`hotkeyTable` is a `QTableWidget` with the columns `Command` and `Hotkey`
and one row per command of the registry, in catalogue order. Whole rows
are selected, one at a time, and no cell can be edited in place.

- The command cell holds the command's menu path and text joined by
  ` > `, with every `/` in the path turned into ` > ` (`Tools > Tagging
  Tools > Capitalise`). An `&` is removed from the text, and so is a
  closing `…`. A command in no menu shows its text alone. The cell's
  `Qt::UserRole` data is the command's id.
- The hotkey cell holds the key as the platform writes it
  (`QKeySequence::NativeText`), empty for none.

### HK-FILTER

`filterEdit` narrows the table: a row stays when the trimmed text is
found, whatever the case, in its command cell or its hotkey cell. Empty
text shows every row. Rows are hidden, not removed. The filter looks at
what the cells show, so it sees edits that are not applied yet.

### HK-EDIT

The page keeps its edits to itself: one pending key per command, starting
from the registry's keys. Nothing reaches the registry before `apply()`.

`assign(id, sequence)`:

- an unknown id: `UnknownAction`;
- more than one key combination: `Invalid`, and the conflict label says
  `Only one key combination can be used.`;
- a key that another command has **among the pending keys**: HK-CONFLICT;
- otherwise `Ok`: the pending key is the sequence (an empty sequence means
  no hotkey), the table shows it, and the conflict label is emptied.

`pending(id)` is the pending key (empty for an unknown id). The page is
modified when any pending key differs from the registry's, and
`modifiedChanged` is emitted when that becomes true or false, not at every
edit. `load()` takes the registry's keys again and empties the conflict
label.

### HK-CONFLICT

A refused key changes nothing. The answer is `Conflict` with the id of
the command that has the key, and `conflictLabel` reads: the key as shown
in the table, ` is used by: `, and that command's command cell.

To move a key the user first takes it from the command that has it; the
registry's own state does not matter until the edits are applied.

### HK-RESET

`reset(id)` gives a command its default key. When another command has
that key among the pending keys, the command is left with no key and the
conflict label says who has it (as in HK-CONFLICT). An unknown id does
nothing. `resetAll()` makes every pending key the default and empties the
conflict label.

### HK-APPLY

`apply()` hands the pending keys to the registry so that the end state is
exactly the pending keys, whatever the order of the edits was: commands
that swapped keys must not block each other. (Take the key away from
every command that changed, then give each its new one.) Commands that
did not change are not touched. With a settings object, the registry is
then saved to it (H-STORE). Afterwards the page is not modified. The
answer is empty, or a sentence naming a command whose key the registry
refused.

### HK-BUTTONS

| Name | Kind | Does |
|---|---|---|
| `keyEdit` | `QKeySequenceEdit`, one combination at most | where the user types a key |
| `assignButton` | `QPushButton` | `assign` the key in `keyEdit` to the chosen row |
| `clearButton` | `QPushButton` | empties `keyEdit` and assigns no key to the chosen row |
| `resetButton` | `QPushButton` | `reset` for the chosen row |
| `resetAllButton` | `QPushButton` | `resetAll` |
| `conflictLabel` | `QLabel` | HK-CONFLICT |

The first three are enabled only while a row is chosen. Choosing a row
puts its pending key in `keyEdit` and empties the conflict label. After a
reset, `keyEdit` shows the chosen row's pending key.

## Not in this version

- Global (system-wide) hotkeys and media keys: desktop integration.
- Offering to take a key from the command that has it, in one step.
- Several keys for one command; mouse buttons and wheel as hotkeys.
- A page that follows changes made to the registry while it is open.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| HK-CONFLICT | What happens there when a key that is in use is typed for another command. |
| PF-APPLY | Whether Apply and Save differ in anything but closing. |
