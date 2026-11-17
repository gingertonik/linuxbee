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

**A backend that is stuck.** `load` reads a file, and a file on a
mount that has died may never be read. The destructor therefore waits
for the audio thread no longer than the end wait (`setEndWait`; three
seconds unless set; a wait of nothing or less is not taken, and the
three seconds stand: with no wait a healthy thread would be left
behind). When the audio thread has not ended by then:

- the destructor returns all the same, and says so once, as a warning
  through Qt's logging that contains the words `audio thread`;
- nothing the inner backend does afterwards reaches the object that
  was destroyed, or anything else of the caller's: its reports go
  nowhere, and the calls that were on their way may still be made on
  it;
- if the stuck call does come back, the inner backend is destroyed on
  the audio thread and the thread ends, by themselves;
- what had to be left behind for that (the thread, and a little
  beside) is not freed. It is small, and happens once for each
  backend left.

(Tried before it was written: a program whose worker thread sits in a
read that never returns, and in `pause()`, ends with status 0 by
returning from `main`, by `exit` and by `_exit`, a second after it
gave up waiting.)

A thread cannot be stopped from outside without risking whatever it
holds, so it is not tried.

## Not in this version

- The program using it (the next package: the device sink and the
  engine made in the factory).
- More than one caller's thread.
- A factory that returns nothing.

## Open questions

| Rule | Question |
|---|---|
| AT-OWN | The device sink on a thread that is not the window's: by ear, on the owner's sound card, with the next package. |
