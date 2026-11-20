# The player bar

The controls along the bottom of the window: previous, play/pause, stop,
next, a seek bar between two times, the name of the track, mute and
volume. It also gives the commands of the Controls menu their behaviour.

This document is the contract for `src/ui/playerbar/playerbar.h`. The
locked tests `tests/locked/tst_ui_playerbar.cpp`,
`tst_ui_playerbar_review.cpp` and `tst_ui_playerbar_modes.cpp` check it.

The bar decides nothing about playback. It calls the player (player.md)
and the play queue (play-queue.md) and shows what they report.

## Sources

| Tag | Source |
|---|---|
| W-PC | MusicBee wiki, "Player Controls" and "Controls Menu": https://musicbee.fandom.com/wiki/Player_Controls , https://musicbee.fandom.com/wiki/Controls_Menu |
| PB-… | A linuxBee decision. |

What MusicBee documents: a player controls panel with transport buttons,
a progress bar that seeks, volume, and toggles for shuffle, repeat and
stop-after-current. Its layout is skinnable there and is not copied.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Player Controls" (no version given;
2016-03-23); "Controls Menu" (3.0.5900; 2018-02-02).

## PB-FRAME

Named widgets:

| Name | Kind | Is |
|---|---|---|
| `previousButton` | `QToolButton` | `controls.previous` |
| `playPauseButton` | `QToolButton` | `controls.playPause` |
| `stopButton` | `QToolButton` | `controls.stop` |
| `nextButton` | `QToolButton` | `controls.next` |
| `muteButton` | `QToolButton` | `controls.mute` |
| `positionLabel` | `QLabel` | time played |
| `seekSlider` | `QSlider`, horizontal | position in the track, in ms |
| `durationLabel` | `QLabel` | length of the track |
| `titleLabel` | `QLabel` | PB-TITLE |
| `volumeSlider` | `QSlider`, horizontal, 0 to 100 | PB-VOLUME |

Each button's default action is that command's `QAction` from the action
registry (main-window.md), so a button and its menu entry are one thing:
same behaviour, same enabled state, same tick.

From left to right: previous, play/pause, stop, next. The seek bar sits
between the two time labels and takes the spare width. Mute is left of
the volume slider, and both are at the right end.

## PB-COMMANDS

The bar implements these commands while it exists; when it is destroyed
they are unimplemented again.

| Command | Does |
|---|---|
| `controls.playPause` | `Player::playPause()` |
| `controls.stop` | `Player::stop()` |
| `controls.next`, `controls.previous` | `Player::next()`, `Player::previous()` |
| `controls.stopAfterCurrent` (tick) | `Player::setStopAfterCurrent` |
| `controls.mute` (tick) | `Player::setMuted` |
| `controls.volumeUp`, `controls.volumeDown` | PB-VOLUME |
| `controls.repeat` (tick) | on: repeat all; off: no repeat |
| `controls.shuffle` (tick) | `PlayQueue::setShuffle` with a fresh random seed |

A tick always shows the truth, whoever changed it: the player clearing
stop-after-current once it is used, a mute set elsewhere, a repeat mode
set elsewhere. The repeat tick is on for any repeat mode, including
repeat-one. A bar created when something is already set starts with the
right ticks.

## PB-MODES

Two more `QToolButton`s, in the row of mute and volume, to the right of
the volume slider: `repeatButton`, then `shuffleButton`.

`shuffleButton`'s default action is `controls.shuffle`, so it is that
command, tick included (PB-FRAME).

`repeatButton` has no default action, because it has three states and
the command has two. A click moves the queue's repeat mode on by one:
off, all, one, off (the owner's ruling, 2026-11-14; the wiki does not
say how MusicBee's button cycles). The button always shows the queue's mode, whoever set it
(the button, the Controls menu, a restored setting), and a bar created
when a mode is already set starts with it:

| Mode | Property `mode` | Checked | Text | Tool tip |
|---|---|---|---|---|
| off | `off` | no | `Repeat` | `Repeat: off` |
| all | `all` | yes | `Repeat` | `Repeat: all` |
| one | `one` | yes | `Repeat 1` | `Repeat: one` |

The `controls.repeat` command keeps its meaning (PB-COMMANDS): its tick
is on for any mode, taking it off gives no repeat, putting it on gives
repeat all. Repeat-one is reached from the button.

## PB-STATE

- `playPauseButton` has a property `playing`: true only while the player
  is playing. (A skin or the assembly plan chooses the icon from it.)
- The seek bar is enabled when the player is not stopped and the length of
  the track is known (more than 0). Its range is 0 to the length.
- When the player stops: seek bar at 0 and disabled, time played `0:00`.
- A new track starts with its length unknown until the player reports it.

## PB-SEEK

- The seek bar and the time played follow `Player::positionChanged`.
  Following never causes a seek.
- While the user holds the handle, reports from the player do not move it
  and nothing is sent. Letting go sends one `Player::seek` to where the
  handle is.
- Any other move by the user (a click beside the handle, a key, the wheel)
  sends one `Player::seek` to the new place at once.

## PB-TIME

`formatTime(ms)`: whole seconds, rounded down. Under an hour `m:ss`
(`0:00`, `3:20`, `59:59`); from an hour `h:mm:ss` (`1:00:00`). Below zero
is `0:00`.

## PB-TITLE

When the track changes, the label shows the file's name without folder
and extension. `setTrackText(text)` replaces it, for whoever knows the
tags; that text lasts until the next change of track. Stopping does not
clear it. A bar created while a track is current shows it at once.

## PB-VOLUME

The slider shows the player's volume times 100, rounded. Moving it sets
the volume. A volume set elsewhere moves the slider without being sent
back. `controls.volumeUp` and `controls.volumeDown` change it by 5 points,
stopping at 0 and 100. Muting does not move the slider.

## PB-STORE

`saveState` writes, in the group `player`: `volume` (0 to 100), `muted`,
`repeat` (`off`, `all` or `one`), `shuffle`. `restoreState` applies them
to the player and the queue. A missing or senseless value gives full
volume, not muted, no repeat, no shuffle.

## Not in this version

- Icons and skinning; album art; rating stars; a waveform.
- A button for stop-after-current on the bar itself (it is in the
  Controls menu).
- Time remaining instead of the length.
- Remembering the track and position between sessions.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| PB-VOLUME | The size of one volume step. |
| PB-COMMANDS | Decided 2026-11-14 by the owner (the wiki does not say): the repeat button cycles off, all, one (PB-MODES). |
