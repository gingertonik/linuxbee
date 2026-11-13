# The main window, its commands and its hotkeys

This is the frame every other part of the window is put into: one
catalogue of commands with their menus and hotkeys, and a window with
three panels, a player bar and a status bar that remembers how the user
left it.

This document is the contract for `src/ui/actions/actionregistry.h`,
`src/ui/actions/actioncatalogue.cpp` (locked data) and
`src/ui/mainwindow/mainwindow.h`. The locked tests
`tests/locked/tst_ui_mainwindow.cpp` and `tests/locked/cli_window.sh`
check it on the offscreen platform.

What a locked test can hold a window to is structure and behaviour: which
parts exist, what they are called, what a command does, what is
remembered. How it looks (spacing, fonts, icons, colours) is judged by a
person, from the screenshot this plan's last task takes.

## Sources

| Tag | Source |
|---|---|
| W-ANAT | MusicBee wiki, "The Anatomy of MusicBee" (panels and their default places): https://musicbee.fandom.com/wiki/Panels_Configuration |
| W-MENU | MusicBee wiki, the menu pages: https://musicbee.fandom.com/wiki/File_Menu , `Edit_Menu`, `View_Menu`, `Controls_Menu`, `Tools_Menu`, `Help_Menu` |
| W-HOT | MusicBee wiki, "Hotkeys": https://musicbee.fandom.com/wiki/Hotkeys_Preferences |
| H-…, W-… | A linuxBee decision. |

Read on 2026-11-13. What MusicBee documents: a left sidebar (the
navigator), a main panel, a right sidebar (playing tracks, track
information), and a bottom panel with the player controls and the status
bar (W-ANAT); six menus, File, Edit, View, Controls, Tools and Help, with
the entries listed on their pages (W-MENU); a default hotkey for some
sixty commands, each of which the user can reassign (W-HOT). The wiki
gives names and places, not sizes.

## 1. Commands

### H-CATALOGUE

`actionCatalogue()` lists every command: an id, the text shown, the menu
it sits in, its default hotkey, whether it is a tick-box, and whether a
separator comes before it. The list is data, written by the reviewer and
locked. It follows W-MENU for names and order and W-HOT for keys, minus
what linuxBee does not have (CD ripping, devices, Last.fm, the visualiser,
tabs) and plus `Exit` on `Ctrl+Q`.

For any catalogue: ids are unique and look like `group.name`; a menu is
empty (a hotkey-only command) or starts with File, Edit, View, Controls,
Tools, Help or Context; a default hotkey is one key combination, written
as Qt writes it (`Ctrl+Shift+Return`, `Del`, `Ins`), and no two commands
share one.

An `ActionRegistry` makes one `QAction` per entry, owned by the registry:
object name the id, the text, checkable or not, the default hotkey, and
`Qt::WindowShortcut` as its context.

### H-IMPLEMENT

A command does nothing until some part of the program *implements* it.
Until then its action is disabled, so the user sees it greyed out in the
menu instead of clicking something dead.

`implement(id, context, slot)` connects the action's `triggered` to the
function, with `context` as the receiving object, and enables the action.
It returns false, changing nothing, for an unknown id, a null context, an
empty function, or a command that is implemented already. When the context
object is destroyed the command is disabled and free again.

A tick-box command's function reads the new state from the action.

### H-SET

`setShortcut(id, sequence)`:

- an unknown id: `UnknownAction`;
- more than one key combination in the sequence: `Invalid`;
- a combination another command has now: `Conflict`, with that command's
  id, and nothing changes (the caller asks the user, clears the other
  command, and tries again);
- otherwise `Ok`. An empty sequence leaves the command without a hotkey.

`shortcutChanged(id)` is emitted when a command's hotkey really changed.

`resetShortcut(id)` gives a command its default back if that is free, and
leaves it with no hotkey if another command holds it.
`resetAllShortcuts()` puts every default back.

### H-STORE

`save(settings)` rewrites the settings group `hotkeys`: one entry per
command whose hotkey differs from its default, key the id, value the
hotkey as portable text (empty for "none"). Nothing outside the group is
touched.

`load(settings)` starts from the defaults. Every command named in the
group first loses its hotkey; then, in catalogue order, each gets the one
stored for it. An entry is skipped when its id is unknown; a command ends
up with no hotkey when its stored text is empty, cannot be read, is more
than one combination, or is held by another command at that moment (a
default the file did not touch, or an earlier command's stored key).

### H-MENU

`populateMenuBar(bar)` adds the menus File, Edit, View, Controls, Tools,
Help in that order, with sub-menus for paths such as
`Tools/Tagging Tools`, created where their first command comes in the
catalogue. Commands are added in catalogue order, with a separator before
one that asks for it unless it would be the first thing in its menu. Each
menu's object name is `menu.` and its path (`menu.File/Library`). The
entries are the registry's own actions. Context and hotkey-only commands
are not in the bar.

`populateMenu(menu, path)` fills a menu of the caller's with the commands
of one branch (`Context`, `Tools/Tagging Tools`) in the same way,
sub-menus included.

## 2. The window

### W-FRAME

Object names are part of the contract:

```
mainWindow (QMainWindow, title "linuxBee")
  menuBar
  centralArea
    mainSplitter (QSplitter, horizontal, children cannot be collapsed)
      navigatorPanel
      mainPanel (QStackedWidget)
      rightPanel
    playerBar            below the splitter, full width, fixed height
  statusBar
```

The window is at least 640 by 400. The navigator and right panels are at
least 120 wide and the main panel at least 200. The main panel takes the
room the others do not use. The window adds every registry action to
itself, so hotkeys work whichever part has the focus.

### W-PANELS

`setPanelWidget(panel, widget)` puts a widget into the navigator panel,
the right panel or the player bar, where it fills the panel. What was
there before is deleted at once: it no longer exists when the call
returns. (Its owner may be about to destroy what the widget points to.)
Null empties the panel. Putting in the widget that is there already
changes nothing. A caller must not replace a panel's widget from inside
a function of that widget.

### W-PAGES

The main panel shows one page at a time. `addPage(id, widget)` adds one;
the first page added is shown. An empty id, a null widget or an id already
used is refused. `showPage(id)` switches; `currentPageChanged(id)` is
emitted when the page shown really changes (and for the first page
added).

### W-COMMANDS

The window implements these commands itself:

| Command | Does |
|---|---|
| `view.navigator`, `view.rightPanel`, `view.statusBar` | show or hide that part; the tick shows the state |
| `view.fullScreen` | full screen and back |
| `file.quit` | closes the window |
| `help.about` | opens a dialog (`aboutDialog`) that does not block, with the line `linuxBee VERSION` |

The player bar cannot be hidden.

### W-LAYOUT

The settings group `window` holds the window's size, the three panel
widths, and whether the navigator, the right panel and the status bar are
shown. `saveLayout()` writes it; closing the window calls it.
`restoreLayout()` applies it; the constructor calls it.

- First run: 1200 by 760, the navigator about 220 wide and the right
  panel about 300, everything shown.
- In full screen, the size saved is the one the window had before.
- A hidden panel keeps its width for when it is shown again.
- Each stored value is used only if it makes sense on its own: a size
  below the minimum or above 16384, or a width list that is not three
  numbers from 0 to 16383, is ignored in favour of the default.

### W-MAIN

`src/app/main.cpp` builds the application: the settings file
`linuxbee.ini` in the user's configuration folder for the application
(`QStandardPaths::AppConfigLocation`), an `ActionRegistry` loaded from it,
a `MainWindow`, and on exit the hotkeys saved. `--version` still prints
the version line and exits without opening a window or writing a file.

`--screenshot FILE` opens the window, waits for it to be drawn, saves a
PNG picture of it to FILE and exits: 0 when the picture was saved, 3 when
it could not be. The layout and hotkeys are saved as on a normal exit. It
exists so that a person can judge the look of a build made on a machine
with no display, and so that the gate can see that the window draws.

**Options.** The whole command line is read before anything is done.
These are mistakes: an option (a word that begins with `--`) the program
does not know; `--screenshot` as the last word or followed by another
option; `--screenshot` given twice. On a mistake the program prints a
message and the line `usage: linuxbee [--version] [--screenshot FILE]
[FILE...]` to standard error and ends with status 2: no window, nothing on
standard output, no file written, and `--version` on the same line is not
acted on. Words that are not options are files handed over by the desktop;
they are accepted and, for now, ignored. Later plans add options of their
own to the usage line.

## Not in this version

- What goes into the panels: the navigator, the track list, playing
  tracks, the player controls, the status text.
- The preferences dialog, including the page that edits hotkeys.
- Arrange Panels, tabs, the mini player and the compact player.
- Global hotkeys and media keys (desktop integration).
- Icons and a theme.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| H-CATALOGUE | The full list of commands on the Hotkeys page of Preferences, with their defaults. |
| W-LAYOUT | Default panel widths, and whether a hidden panel's width is remembered. |
| H-SET | What MusicBee does when a key is already taken: refuse, or move it? |
