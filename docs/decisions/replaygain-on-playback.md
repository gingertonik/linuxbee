# ReplayGain is applied on playback; Smart Gain is not copied

**Status:** Decided by the owner, 2026-11-16 and 2026-11-20.

## The question

Whether playback applies ReplayGain in 1.0, and what to do with MusicBee's
default mode, Smart Gain, which its wiki does not describe.

## The decision

- ReplayGain applied on playback is in 1.0, with the modes off, track and
  album.
- Smart Gain is not copied: nothing public says what it does.
- The default stays off until the package on ReplayGain of 0.8.2 decides
  it.

## What it allows

linuxBee does not guess at a mode nobody has described.

## Where it is relied on

- `docs/spec/playback-shell.md`, PS-GAIN
