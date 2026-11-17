# What linuxBee does without being asked

The fourth assembly: the application answers the desktop's media
controls, and takes in files that arrive in the folders it was told to
watch.

This document is the contract for `src/ui/shell/backgroundshell.h` and
for what `src/app/main.cpp` does with it. The locked tests
`tests/locked/tst_ui_backgroundshell.cpp` and
`tests/locked/cli_background.sh` check it.

The shell owns no rule. What goes over the bus is decided in
desktop-mpris.md, when a folder is scanned in folder-monitor.md, and what
a scan does in library.md.

## Sources

| Tag | Source |
|---|---|
| W-LP | MusicBee wiki, "Library Preferences" (monitored folders, inbox, check on startup): see folder-monitor.md |
| BG-… | A linuxBee decision. |

## BG-BUILD

Constructing the shell creates an `MprisService` over the playback
shell's player and queue and starts it on the bus it was given
(BG-MPRIS), and creates a `FolderMonitor` for the folders stored in the
settings (BG-MONITOR) and a `ScanRunner` of its own on the library
(`scanner()`; scan-runner.md). Nothing is scanned and nothing is played.

Destroying the shell takes linuxBee off the bus and stops the watching, and then its runner
(SR-STOP: what was stored stays, and nothing is stored after).
The player and the library are untouched by that.

## BG-MPRIS

The service's hooks:

| Hook | Does |
|---|---|
| raise | the window is shown (also when it was hidden or minimised), raised and made active |
| quit | the shell's own `quit` hook is called |
| describe | for a file the library knows: its Title, every Artist value, its Album, from the library's tags. For a file it does not know: nothing, so the service falls back to the file's name |

When the service cannot start (no session bus, or another linuxBee has
the name), that is not a failure: the message is
`Media keys are not available: ` followed by the service's reason, and
everything else works.

## BG-MONITOR

Settings, all in the group `library`:

| Key | Meaning | When missing or senseless |
|---|---|---|
| `monitored` | the folders to watch (a list; one folder is stored as plain text) | none |
| `monitoredCategory` | the library section new files go to | `Inbox` |
| `settleMs` | the monitor's settle time, 0 to 60000 | the monitor's own default |
| `checkOnStartup` | BG-STARTUP | false |

The monitor's scans do not hold the window. The monitor's scanner asks
the shell's runner for a scan of the folders it names
(`ScanRunner::scan`), with new files going to that section and the time
from the `now` hook, and returns at once with an empty report: no file
is looked at and nothing is stored inside the call. So
`FolderMonitor::scanned` now says that a scan was asked for, and its
report is empty; what the scan did is the report of the runner's
`finished`. The section is read from the settings when the scan is
asked for, and it is that scan's section until it ends, whatever the
settings say by then and however many scans wait behind it.

The runner is the shell's own, not the window's (SR-SHELL), so the
window's "Scan Folders" message and this one do not answer each
other's scans.

When the runner's scan has ended (`finished`), and it added files, the message is that scan's section, a colon, and
`1 new file` or `N new files`: `Inbox: 2 new files`. A scan that added
nothing says nothing.

The window follows by itself: the library reports the new tracks and the
library shell shows them (library-shell.md, SH-LIVE).

## BG-FOLDERS

`monitoredFolders()` is the monitor's list. `setMonitoredFolders(list)`
gives the monitor the list and stores, at once, what the monitor made of
it (cleaned, each folder once). It scans nothing.

## BG-RELOAD

`reload()` is for when something else changed the settings: the monitor
is given the stored `monitored` folders again.

When that leaves the monitor with a folder it did not have before the
call, every monitored folder is checked once (`FolderMonitor::checkNow`),
not inside `reload()` but as soon as the event loop runs. So a folder
that is added in the preferences has its files taken in at once, as
MusicBee does (the owner, from memory, 2026-11-14), with the message of
BG-MONITOR. A reload that adds no folder (the same list, a shorter one,
another order) checks nothing and says nothing. `checkOnStartup` is not
acted on here. (The section needs no reloading: BG-MONITOR.)

## BG-STARTUP

With `checkOnStartup` true, every monitored folder is checked once
(`FolderMonitor::checkNow`), not inside the constructor but as soon as
the event loop runs, so that the window appears first. This finds what
arrived while linuxBee was closed.

## BG-MESSAGE

Every message goes to the window's status bar and is kept as
`lastMessage()`.

## BG-MAIN

`src/app/main.cpp` additionally:

- creates a `BackgroundShell` on `QDBusConnection::sessionBus()`, with
  the same `now` as the other shells and a `quit` that closes the window;
- on the way out it is the first thing destroyed, before anything is
  saved;
- understands `--services` (added to the usage line): after everything
  has been created, and without showing the window, it prints two lines,
  `mpris=on` or `mpris=off`, and `monitored=` with the number of
  monitored folders, and ends with status 0.

A machine with no session bus still starts the program. A second
linuxBee with a profile of its own still starts, without the bus name
when the first has it; a second with the same profile is refused
(main-window.md, W-ONE).

## Not in this version

- The preferences page for choosing the monitored folders and the section
  (library-preferences.md).
- A timed re-check for folders that send no change notices.
- A tray icon and "keep running when the window is closed".
- Opening files handed over by the desktop.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| BG-MONITOR | Whether new files in a monitored folder go to the Inbox or straight into the library by default. |
| BG-STARTUP | Whether "check on startup" is on by default. |
