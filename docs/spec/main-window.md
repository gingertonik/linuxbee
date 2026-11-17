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

### H-NOTYET

A command that nothing implements is greyed in its menu (H-IMPLEMENT).
`tests/locked/catalogue-not-yet.tsv` lists every such command, with
what it waits for. `tests/locked/cli_catalogue.sh` holds the list to
the program: every command of the catalogue is implemented by the
running program (`linuxbee --commands`) or is on the list, never both
and never neither.

So the list is the program's own account of what its menus promise
and do not yet do, and it cannot rot: a plan that gives a command its
behaviour takes the line out in the same commit, and a command added
to the catalogue without a behaviour needs a line with a reason.

The list is empty at 1.0, or what is left on it has been taken out of
the catalogue: a menu does not show a command that will never work.

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

### W-PLACES

The application keeps its files in three folders, each named `linuxbee`
once:

| What | Where | Default |
|---|---|---|
| settings (`linuxbee.ini`) | `$XDG_CONFIG_HOME/linuxbee/` | `~/.config/linuxbee/` |
| the library (`library.db`), `playlists/`, `auto-playlists/`, `skins/` | `$XDG_DATA_HOME/linuxbee/` | `~/.local/share/linuxbee/` |
| what may be thrown away (the undo's pictures) | `$XDG_CACHE_HOME/linuxbee/` | `~/.cache/linuxbee/` |

It writes nowhere else of its own accord. `--version` writes nothing.

### W-ADOPT

Builds before 0.8 had the name twice (`.../linuxbee/linuxbee/`). At a
start, for the settings folder and for the data folder alike, what is
in that inner folder is taken into the folder above it:

- each entry is moved by renaming it, whole, and only where the folder
  above has nothing of that name; a folder is never merged into another;
- an entry whose name is taken is left exactly as it is, and named on
  standard error with the place it was left in;
- the inner folder is removed once it is empty;
- an inner `linuxbee` that is a link is not followed and not touched.

So a library made by an older build is there at the first start of
this one, and when both places hold a library, the one in the new
place is used and the other is not lost.

This happens before the library is opened, and under W-ONE's lock.

**A database and its companions.** (Found by the review of this rule's
first build, which moved each entry by itself: with a library in both
places, the older library's write-ahead log was moved beside the
library in use, SQLite applied it, and the library in use was
destroyed.) A name that ends in `-wal`, `-shm` or `-journal` is the
companion of the name without that ending. Then:

- a companion is never moved by itself, and never to where a file of
  its database's name is: a log with no database beside it is left;
- an entry is moved only when the folder above has neither its name
  nor any companion's name of it;
- an entry that has a companion beside it is first settled: if it
  begins as an SQLite database does (its first sixteen bytes), it is
  opened as one and closed, by the database's own means, so that what
  its log holds is in the one file. Only when no companion is left is
  it moved. If it does not begin so, it is not opened at all (SQLite
  clears away the log of a file it cannot read); it and its
  companions are left as they are, as they are when a companion
  stays after settling;
- whatever is left is left whole, byte for byte, and each is named on
  standard error.

So what was committed last in an older build is there after the move,
and a library is never changed by another library's log.

**Open elsewhere.** If another program has an entry of the inner
folder open (an older build still running does not know W-ONE's
lock), nothing is moved, and the start ends as W-ONE's does: status 5,
a line containing `already running`. This is found by what the system
shows of this user's programs' open files.

**Never an empty library while an older one waits.** When, after all
this, the inner folder of the data folder (followed through a link,
this once) holds a `library.db` and the data folder has none, the
program does not start: status 6, a line on standard error containing
`was not taken over` that names both places and why, and the same in
a dialog where a window would have been shown. It makes no library.
A user must not be shown an empty library and left to think the music
is gone.

Known: with a library in both places the one in the new place is used
and the older one is only named on standard error, at every start.
Going back to a build from before 0.8 on a profile that has been
moved shows that build an empty library; its files are left alone
when this build starts again.

### W-ONE

One program uses a profile at a time. Every start that opens the
library (all but `--version`) first takes a lock in the data folder.
When another program holds it, the start ends with status 5 and a
line on standard error containing `already running`; it opens
nothing, changes nothing, prints nothing on standard output and shows
no dialog.

(On a network file system a lock left by a program that died on
another machine is never taken to be stale: the start is refused
until the lock file is removed by hand. The owner's library is on a
local disk.)

The lock goes when the program ends, however it ends: a program that
was killed does not hold the profile afterwards. A profile in another
place (another `XDG_DATA_HOME`) is another profile.

Two starts at the same moment on a profile with nothing in it: one of
them runs, or both in turn; neither fails to open the library.

Why: two windows over one library each hold their own picture of it,
and each saves its settings over the other's on the way out.
`linuxbeectl` takes no such lock; it and a running window share the
library file by the library's own rules (library.md, L-OPEN, L-WAIT).

Not in this version: bringing the running window to the front, and
handing it the files the second start was given.

### W-MAIN

`src/app/main.cpp` builds the application: the settings file
`linuxbee.ini` in the settings folder (W-PLACES), an `ActionRegistry` loaded from it,
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

### W-SIGNAL

The signals SIGTERM, SIGINT and SIGHUP (a logout, a service being
stopped, Ctrl+C in a terminal, the terminal closing) end the
application as File > Exit does: the event loop is left, everything
W-MAIN and the shells save on exit is saved, the objects are
destroyed in their order, and the exit status is 0. The process does
not die of the signal.

The signal handler itself does nothing but tell the event loop, in a
way that is safe inside a handler (a byte written to a pipe or
socket the loop watches); the quit happens in the loop. A second
signal while the first is being answered changes nothing.

Not covered: SIGKILL, which no program sees. What a killed run
leaves behind is tidied at the next start (tools-shell.md, TS-UNDO).
`--version` and `--screenshot` runs need none of this.

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
