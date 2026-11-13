# The device sink

Where sound leaves the program: a `PcmSink` (audio-engine.md) that hands
the engine's samples to the system's default output device.

This document is the contract for `src/ui/audio/devicesink.h`. The locked
test `tests/locked/tst_ui_audiodevice.cpp` checks what can be checked on a
machine that may have no sound device, without ever making a noise. That
is the edges: what the sink does when it is not started, when it is asked
for a format it must refuse, when it is paused, stopped and started again.
Whether music sounds right is judged by a person; the plan's last step
says how.

## Sources

| Tag | Source |
|---|---|
| QT | Qt 6 documentation, `QAudioSink`, `QAudioFormat`, `QMediaDevices`: https://doc.qt.io/qt-6/qaudiosink.html |
| AD-… | A linuxBee decision. |

`QAudioSink` is used in its push mode: `start()` returns a `QIODevice` to
write samples to, `bytesFree()` says how much it will take, `suspend()`
and `resume()` pause it (QT). On a current Linux desktop Qt reaches the
device through PipeWire or PulseAudio.

## AD-START

`start(format)`:

- refuses (returns false, leaving the sink not started) a sample rate
  outside 8000 to 192000 or a channel count other than 1 or 2, whatever
  the machine;
- refuses when there is no default output device, when the device does
  not support 32-bit float samples in that format, or when the device
  cannot be opened;
- otherwise opens the default output device for 32-bit float samples at
  that rate and channel count and returns true.

Starting a sink that is already started ends the old session first. A new
session is never paused. After a refused start there is no session.

`isStarted()` and `format()` describe the session; `format()` is
`{ 0, 0 }` when there is none.

## AD-WRITE

`bytesFree()` is what the device will take now, rounded down to whole
frames; 0 when not started or paused.

`write(data, bytes)` hands over at most `bytesFree()` bytes, in whole
frames, and returns how many it handed over; 0 when not started, when
paused, or when there is not room for one frame. It never blocks.

## AD-PAUSE

`setPaused(true)` suspends the device: it keeps what it has buffered and
plays nothing. `setPaused(false)` resumes. Without a session, both do
nothing.

## AD-IDLE

`idle()` is true when the device has played everything it was given (its
buffer is empty), and always when there is no session.

## AD-STOP

`stop()` ends the session at once and drops what was buffered. Destroying
the sink does the same.

## Not in this version

- Choosing an output device other than the default, and following the
  default when the user changes it while music plays.
- Exclusive or bit-perfect output.
- Reacting to the device going away (headphones unplugged).

## Open questions

| Rule | Question |
|---|---|
| AD-START | Whether every common device accepts float samples at 44100 and 96000 Hz, or the engine's 48000 Hz fallback is what most machines end up using. |
| AD-IDLE | Whether the end of a track is cut short or followed by a gap on real hardware. |
