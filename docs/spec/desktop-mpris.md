# Desktop integration: MPRIS

linuxBee announces itself on the session bus the way Linux desktops
expect of a media player. That is what makes the keyboard's media keys,
the panel's player widget, the lock screen's controls and phone remotes
(KDE Connect and the like) work, with no code of linuxBee's own for any
of them.

This document is the contract for `src/ui/desktop/mprisservice.h`. The
locked test `tests/locked/tst_ui_mpris.cpp` checks it over a real, private
message bus.

The service decides nothing about playback. It passes requests to the
player and the play queue (player.md, play-queue.md) and reports their
state.

## Sources

| Tag | Source |
|---|---|
| MPRIS | MPRIS D-Bus Interface Specification, version 2.2: https://specifications.freedesktop.org/mpris-spec/latest/ |
| W-HK | MusicBee wiki, "Hotkeys" (global hotkeys, multimedia keys): https://musicbee.fandom.com/wiki/Hotkeys |
| MP-… | A linuxBee decision. |

Read on 2026-11-13. MusicBee handles multimedia keys and global hotkeys
itself; on Linux the desktop does that and hands the request to whichever
player is on the bus. This is how the same wish is met here.

Where this document and the MPRIS specification disagree about what
goes over the bus, the MPRIS specification is right and the disagreement
is reported.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Hotkeys" (3.0.5903; 2020-12-31).

## MP-START

The service's name on the bus is `org.mpris.MediaPlayer2.linuxbee` and
its object is `/org/mpris/MediaPlayer2`.

`start(connection, &error)` registers the object and then the name.

- Not connected: false, with a reason.
- The name is already owned (another linuxBee is running): false, the
  reason contains the name, and nothing stays registered.
- Otherwise true, and `error` is emptied when it was given.

`stop()` gives up the name and the object; twice is harmless; destroying
the service does the same. A service that is not running still follows
the player quietly, so that it can be started later.

## MP-ROOT

Interface `org.mpris.MediaPlayer2`:

| Member | Value |
|---|---|
| `Identity` | `linuxBee` |
| `DesktopEntry` | `io.github.gingertonik.linuxbee` (the desktop file's name without `.desktop`) |
| `CanQuit`, `CanRaise` | true |
| `HasTrackList` | false |
| `SupportedUriSchemes` | `file` |
| `SupportedMimeTypes` | at least `audio/flac`, `audio/mpeg`, `audio/mp4`, `audio/ogg`, `audio/opus`, `audio/x-wav` |
| `Raise()` | calls the `raise` hook |
| `Quit()` | calls the `quit` hook |

## MP-METHODS

Interface `org.mpris.MediaPlayer2.Player`:

| Method | Does |
|---|---|
| `Play`, `Pause`, `PlayPause`, `Stop`, `Next`, `Previous` | the player's function of the same name |
| `Seek(x)`, `SetPosition(o, x)` | MP-SEEK |
| `OpenUri(s)` | accepted and ignored |

## MP-PROPS

| Property | Value | Written |
|---|---|---|
| `PlaybackStatus` | `Playing`, `Paused` or `Stopped` | no |
| `LoopStatus` | `None`, `Track` (repeat one) or `Playlist` (repeat all) | sets the queue's repeat; any other text changes nothing |
| `Shuffle` | the queue's shuffle | switches it, with a fresh random seed |
| `Volume` | the player's volume, 0 to 1 | sets it; below 0 is 0, above 1 is 1 |
| `Rate`, `MinimumRate`, `MaximumRate` | 1.0 | `Rate` may be written; it stays 1.0 |
| `Metadata` | MP-METADATA | no |
| `Position` | MP-SEEK | no |
| `CanControl` | true | no |
| the other `Can…` | MP-CAN | no |

## MP-METADATA

A map of text to value:

| Key | Type | Value |
|---|---|---|
| `mpris:trackid` | object path | with a track: `/io/github/gingertonik/linuxbee/track/N`, where N is different every time a track is started (the same file played again gets a new one); with none: `/org/mpris/MediaPlayer2/TrackList/NoTrack` |
| `xesam:url` | text | the file as a URL, fully percent-encoded (`file:///m/A%20B/x.flac`) |
| `xesam:title` | text | the title from the `describe` hook; when that is empty, the file's name without folder and extension |
| `xesam:artist` | list of text | the artists from the hook; left out when there are none |
| `xesam:album` | text | from the hook; left out when empty |
| `mpris:length` | 64-bit integer | the length in microseconds; left out until it is known |

With no track, the map holds the track id and nothing else.

## MP-CAN

| Property | True when |
|---|---|
| `CanPlay`, `CanPause` | the queue is not empty |
| `CanGoNext` | the queue has somewhere to go for a user's Next (`PlayQueue::hasNext(false)`) |
| `CanGoPrevious` | the queue has a current entry |
| `CanSeek` | the player is not stopped and the track's length is known |

## MP-SEEK

Everything on the bus is in microseconds; the player works in
milliseconds.

- `Position` is the player's position; 0 when stopped.
- `Seek(offset)` moves by `offset` from the current position. Before the
  start becomes the start. Past the end is `Next`. Nothing happens when
  `CanSeek` is false.
- `SetPosition(trackId, position)` seeks to `position`. It is ignored
  when `CanSeek` is false, when `trackId` is not the current track's id,
  and when `position` is below 0 or beyond the length (the length itself
  is allowed).
- The signal `Seeked(position)` tells listeners that the position jumped.
  It is emitted when the player reports a position, other than 0, that is
  earlier than the last one or more than 1500 ms later, while not
  stopped. A report of 0 is a track starting, not a seek; but when `Seek`
  or `SetPosition` themselves take the track to its very start,
  `Seeked(0)` is emitted.

## MP-CHANGED

Whenever the player or the queue reports a change, the service works out
the Player interface's properties again and sends
`org.freedesktop.DBus.Properties.PropertiesChanged` with exactly those
whose value differs from what was last sent, in one signal. Nothing
different: no signal. `Position` is never part of it (MPRIS). Starting
the service announces nothing.

## Not in this version

- The `TrackList` and `Playlists` interfaces.
- Cover art (`mpris:artUrl`): it needs artwork written to a cache folder.
- Opening files handed over by the desktop (`OpenUri`, and files named on
  the command line of a second linuxBee).
- A tray icon. Global hotkeys of linuxBee's own, outside the media keys.
- `Seeked` for a seek to exactly 0 made in the window.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| MP-ROOT | Nothing to compare: MusicBee has no MPRIS. Which of its global hotkeys do you use that media keys do not cover? |
