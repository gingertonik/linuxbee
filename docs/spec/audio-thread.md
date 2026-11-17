# The audio thread

The engine (`audio-engine.md`) pumps samples from a timer. On the
window's thread that timer stands still whenever the window is busy:
a long list being filled, a dialog being built, a slow disk under a
tag being read. Sound then stops for as long. And `load` reads the
file's tags, so a click on a track waits for the disk.

`ThreadedAudioBackend` (`src/core/playback/threadedbackend.h`, locked)
is an `AudioBackend` that keeps another one on a thread of its own.
The player uses it as it uses any backend.

"The caller's thread" below is the thread the `ThreadedAudioBackend`
lives in. "The audio thread" is the one it starts.

## AT-OWN

The factory runs once, on the audio thread, never on the caller's.
The backend it returns is used only on the audio thread and is
destroyed there. A sink made inside the factory therefore lives there
too.

## AT-CALLS

`load`, `play`, `pause`, `stop`, `seek` and `setVolume` reach the
inner backend on the audio thread, each once, with their arguments,
in the order they were made. Calls made before the factory has run
are kept and arrive after it.

## AT-NOWAIT

A call returns without waiting for the audio thread: not for the
factory, not for the inner backend to finish what it is doing. The
caller's thread never blocks on the engine.

The other way round too: the audio thread does not need the caller's
thread. While the caller's thread is busy and runs no event loop, the
inner backend's timers and its decoder go on.

## AT-REPORTS

`positionChanged`, `durationChanged`, `ended` and `failed` of the
inner backend are emitted by the `ThreadedAudioBackend` on the
caller's thread, in the order the inner backend emitted them, with
their arguments.

## AT-STALE

Between the two threads a report can cross a call. The player has
asked for another file, and a report about the file before is still
on its way: an `ended` of the old file would make the player skip the
new one.

So: once `load` or `stop` has been called, no report that the inner
backend emitted before it handled that call is emitted. Every report
it emits from then on is.

## AT-SEEK

Likewise a `positionChanged` that the inner backend emitted before it
handled a `seek` is not emitted once `seek` has been called (the
position would jump back for a moment). The other three reports are
not held back by a seek: a file that ended just before the seek has
ended.

## AT-END

The destructor has the inner backend destroyed on the audio thread
and the audio thread ended before it returns. Nothing is emitted
afterwards. Calls still on their way are not promised to arrive.

## Not in this version

- The program using it (the next package: the device sink and the
  engine made in the factory).
- More than one caller's thread.
- A factory that returns nothing.

## Open questions

| Rule | Question |
|---|---|
| AT-END | An inner backend that never returns from a call holds the destructor. The engine has no such call; a watchdog would be guessing. |
| AT-OWN | The device sink on a thread that is not the window's: by ear, on the owner's sound card, with the next package. |
