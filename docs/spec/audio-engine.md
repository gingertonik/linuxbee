# The audio engine

How a file becomes sound: ffmpeg decodes it to plain samples in a child
process, and the engine feeds those samples to a sink a little at a time.

This document is the contract for `src/core/playback/audioengine.h`. The
locked test `tests/locked/tst_audioengine.cpp` checks it with real ffmpeg
and real files, and a sink that only counts.

The engine is an `AudioBackend` for the player (player.md): one file at a
time, load, play, pause, stop, seek, volume. The sink (`PcmSink`) is where
sound leaves the program. The sink that talks to the sound card is another
plan; nothing here needs a device.

## Sources

| Tag | Source |
|---|---|
| FF | ffmpeg documentation, "Main options" (`-ss` before `-i`, `-map`, `-vn`) and the `f32le` raw format: https://ffmpeg.org/ffmpeg.html |
| AE-… | A linuxBee decision. |

MusicBee plays through the BASS library, which is not free software and
is not used. Nothing in this document copies MusicBee's player; it only
has to play every format linuxBee reads.

Why not Qt Multimedia's player: tried on 2026-11-13 with Qt 6.12, it never
reported the end of a file when no sound device was attached, which makes
it impossible to test without one. ffmpeg is already required for
converting and for volume analysis.

## AE-FORMAT

Samples are 32-bit floats in the machine's byte order, channels
interleaved. `playbackFormat(info)` chooses the rest from the file:

- the file's own sample rate when it is from 8000 to 192000 Hz, otherwise
  44100;
- one channel for a mono file, two for everything else (more than two are
  mixed down).

If the sink refuses that format, the engine offers 48000 Hz stereo and has
ffmpeg resample. If the sink refuses that too, the file fails with
`No audio device could be opened.` (AE-FAIL).

## AE-ARGS

`decodeArguments(path, startMs, format)` is exactly:

```
-v error -nostdin [-ss SECONDS] -i PATH -map 0:a:0 -vn -sn -dn
-f f32le -acodec pcm_f32le -ar RATE -ac CHANNELS pipe:1
```

`-ss` and its value are present only when `startMs` is above 0; the value
is seconds with three decimals (`83.456`). The path is one argument, as
given.

## AE-ASYNC

No signal is emitted from inside a call to `load`, `play`, `pause`,
`stop`, `seek` or `setVolume`. Whatever a call leads to is reported from
the event loop afterwards.

After `stop()` or a new `load()`, nothing more is reported about the file
that was loaded before.

## AE-LOAD

`load(path)` stops whatever was loaded, then reads the file's length and
format with the tag reader. `durationChanged(ms)` follows, with the length
the tag reader gives. Nothing is decoded and the sink is not touched until
`play()`.

A file the tag reader cannot find or does not take for music is still
"loaded": the failure is reported when it is played (AE-FAIL).

## AE-PLAY

`play()` with a file loaded and not already playing: starts the sink with
the format (AE-FORMAT) if it is not started, starts ffmpeg (AE-ARGS) if it
is not running, and begins to move samples.

- The sink is offered only what `bytesFree()` says it will take, and only
  whole frames.
- Every sample is multiplied by the volume first (AE-VOLUME).
- With nothing loaded, `play()`, `pause()` and `seek()` do nothing.

## AE-POSITION

The position is the place playback started from, plus the samples handed
to the sink since. `positionChanged(ms)` is emitted as it advances: not
more often than once per 100 ms of music, always rising between seeks, and
once more with the final position just before `ended()`.

## AE-END

When ffmpeg has finished without error, every sample has been handed to
the sink, and the sink says it is idle (it has played what it was given),
the engine stops the sink, lets go of the file, and emits `ended()`. Not
before the sink is idle: the last notes are not cut off.

## AE-PAUSE

`pause()` while playing tells the sink to pause and hands it nothing more.
`play()` resumes with the next sample: none is lost and none repeated, and
the sink is not restarted.

## AE-SEEK

`seek(ms)` with a file loaded: the target is held between 0 and the file's
length. What the sink has buffered is dropped (the sink is stopped and
started again), ffmpeg is started afresh at the target, and positions
continue from the target. A paused engine stays paused. A seek before
`play()` only sets where playing will start.

A seek to the very end leads to `ended()`, not to an error.

## AE-VOLUME

`setVolume(linear)` takes 0 to 1, pulling other values into range, and
applies to samples handed over from then on. The engine never amplifies.
At 0 the file is still played, as silence.

## AE-STOP

`stop()` ends ffmpeg, stops the sink and forgets the file, at once. The
file is no longer open. Destroying the engine does the same: no child
process is left behind.

## AE-MEMORY

ffmpeg decodes far faster than music plays. The engine must not let the
decoded audio pile up: it holds back the decoder so that no more than a
few megabytes wait in the program at any time, however long the file.
(The test plays twenty minutes of silence, 400 MB once decoded, to a slow
sink for five seconds and checks that the program has grown
by less than 24 MB.)

## AE-FAIL

`failed(message)` is emitted once, and `ended()` is not, when a file
cannot be played. The sink is stopped and the engine is ready for the next
`load`.

| When | Message |
|---|---|
| the tag reader does not find the file | `The file was not found.` |
| the tag reader does not take it for music | `The file is not a music file.` |
| the sink takes neither format | `No audio device could be opened.` |
| ffmpeg cannot be started | `ffmpeg was not found.` |
| ffmpeg ends with an error, or gives no audio at all for a file played from its start | the last line ffmpeg wrote to its error output, or `The file cannot be played.` when it wrote none |

## Not in this version

- Gapless playback and crossfade: one decoder, one file.
- Volume above 1 (a pre-amp), an equaliser, other effects.
- Streams and anything that is not a local file.
- Choosing the output device (the sink's business).
- Bit-perfect output: samples are floats, and the volume is applied in
  software.

## Open questions

| Rule | Question |
|---|---|
| AE-FORMAT | Whether hi-res files should be played at their own rate (as now) or at the device's preferred rate. To be judged by ear on a real device. |
| AE-SEEK | Whether a seek is quick enough on large files and network drives, since each one starts ffmpeg again. |
