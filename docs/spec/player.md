# The player

What Play, Pause, Stop, Next and Previous do, what happens when a track
ends, how the volume reaches the audio, and when a play or a skip is
counted.

This document is the contract for `src/core/playback/player.h`. The
locked test `tests/locked/tst_player.cpp` checks it.

The player makes no sound. It drives an `AudioBackend` (one file at a
time: load, play, pause, stop, seek, set volume) and listens to what the
backend reports (position, duration, ended, failed). Which entry is next
is the play queue's business (play-queue.md). A real backend is another
plan; the test uses a stand-in.

## Sources

| Tag | Source |
|---|---|
| W-PC | MusicBee wiki, "Player Controls" and the Controls menu (Play/Pause, Stop, Stop After Current, Next, Previous, Mute): https://musicbee.fandom.com/wiki/Controls_Menu |
| W-NP | MusicBee wiki, "Now Playing" preferences (when a play and a skip are counted): see play-queue.md |
| X-… | A linuxBee decision. |

Read on 2026-11-13. The wiki names the controls. It does not say what
Previous does in the middle of a track, what happens to a playing track
that is removed from the list, or what follows a file that cannot be
played.

## X-STATE

The player is Stopped, Playing or Paused. It starts Stopped, at volume 1,
with no track.

| Call | Stopped | Playing | Paused |
|---|---|---|---|
| `play()` | starts the queue's current entry from its beginning; with no current entry, the one Next would go to; with nowhere to go, nothing | nothing | resumes (the file is not loaded again) |
| `pause()` | nothing | pauses | nothing |
| `playPause()` | as `play()` | as `pause()` | as `play()` |
| `stop()` | nothing | stops | stops |

**Starting a track** is, in this order: load the file, set the volume
(X-VOLUME), play. The position is 0 and the duration 0 until the backend
reports it.

**Stopping** tells the backend to stop and puts the position at 0. The
queue's current entry stays, and `currentPath()` still names the track.

`stateChanged` is emitted when the state really changes. `trackChanged`
is emitted when a different queue entry is started, not when the same
entry starts again.

## X-QUEUE

When the queue gets a different current entry by any other hand than the
player's (`playNow`, `setCurrent`, a direct `next`), the player starts
it, whatever its state.

"Different" means a different entry, not a different index: adding,
moving and removing other entries, and switching repeat or shuffle, never
interrupt the music.

If the playing entry itself is removed, or the queue is cleared, the
track plays on. When it ends, X-END applies as usual: the queue goes on
from the place it remembers, or the player stops.

## X-NEXT

`next()` asks the queue for the user's Next. If there is one, it is
started, whatever the state. If there is none, nothing happens: a playing
track plays on.

`previous()`:

- when a track is playing or paused more than 3000 ms in, goes back to
  its start (a seek to 0);
- otherwise asks the queue for Previous and starts it, whatever the
  state;
- if the queue has none and a track is playing or paused, goes back to
  its start.

## X-END

When the backend reports that the file played to its end:

1. The track is counted (X-COUNT).
2. With "stop after current" set: the flag is cleared (and
   `stopAfterCurrentChanged(false)` emitted), the queue moves on as at a
   track's end so that Play will continue with the following entry, and
   the player stops without starting it.
3. Otherwise the queue is asked for the automatic Next. If there is one
   it is started; under Repeat One that is the same entry again, loaded
   afresh. If there is none, the player stops.

"Stop after current" is about a track's own end. The user's Next, or
choosing another track, neither obeys nor clears it.

## X-SEEK

`seek(ms)` works while Playing or Paused. The target is held between 0
and the duration (no upper limit while the duration is unknown). The
backend is told, and `positionMs()` is the target at once.

Position and duration reports from the backend are passed on
(`positionChanged`, `durationChanged`, only when the value changed).
Reports that arrive while no track is being played (before the first,
after a stop) are ignored.

## X-VOLUME

`setVolume` takes 0 to 1, pulling other values into range. `setMuted`
silences without forgetting the volume.

The backend's volume is 0 when muted, and otherwise the volume times the
track's factor, held between 0 and 1. The factor comes from the function
given to `setTrackVolume` (volume levelling), asked with the track's
path; a factor below 0 counts as 0; with no function it is 1.

The backend is told whenever the volume, the mute or the factor function
changes, and for every track after it is loaded and before it plays.

## X-RENAME

`renamePaths(oldToNew)` renames in the queue (play-queue.md,
Q-RENAME) and, when the track last started is one of the old paths,
takes its new path as `currentPath()`. The backend is told nothing:
the file it has open is the same file. The state, the position and
the time heard stay; `trackChanged` is not emitted. The play or skip
is counted, when the track ends, under the new path.

## X-LISTEN

The time heard of a track is the sum of the forward steps between one
position report and the next while the player is Playing. A seek moves
the starting point without adding anything. A report that goes backwards
adds nothing. Reports while Paused add nothing. When the backend reports
the end and the duration is known, the stretch from the last reported
position to the duration counts as heard.

## X-COUNT

A track is accounted for once, when it stops being the track played:
because it ended, or because the user moved on (Next, Previous to another
track, another entry chosen, a new list, Stop).

`classifyPlay` (play-queue.md, Q-COUNT) decides, from the time heard
(X-LISTEN), the duration, and whether the user ended it. Played emits
`playCounted(path)`; Skipped emits `skipCounted(path)`; Neither emits
nothing. `setCountRule` replaces the rule's numbers.

Going back to the start of the same track (X-NEXT) does not end it.

## X-BACKEND

What the player may assume of any backend:

- No signal is emitted from inside a call to `load`, `play`, `pause`,
  `stop`, `seek` or `setVolume`. Whatever a call leads to, a failure
  included, is reported from the event loop afterwards.
- After `stop()` or a new `load()`, nothing more is reported about the
  file that was loaded before.

A backend that breaks the first rule can make the player ignore a failure
or lose count of failures in a row (X-ERROR). The player does not guard
against it; the audio engine's own tests hold it to both rules
(audio-engine.md, AE-ASYNC).

## X-ERROR

When the backend reports that the file cannot be played:

1. `playbackError(path, message)` is emitted. The track is not counted,
   however much of it was heard.
2. The player moves on as at a track's end (X-END, steps 2 and 3).
3. To keep a queue of unplayable files from going round for ever: when as
   many tracks in a row have failed as the queue holds (at least one),
   the player stops instead. A track that plays to its end, and any step
   the user takes, starts the count again.

## Not in this version

- Gapless playback and crossfade (they need a backend that holds two
  files).
- Fading on pause and stop; remembering the position between runs.
- An equaliser and other sound effects.
- Internet radio and other things that are not files.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| X-NEXT | What Previous does a few seconds into a track, and where the line is. |
| X-QUEUE | What happens to the playing track when it is removed from Playing Tracks. |
| X-END | After "stop after current", does Play continue with the next track? |
| X-ERROR | What MusicBee does with a file it cannot play. |
