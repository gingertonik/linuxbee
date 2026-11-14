# Volume levelling (ReplayGain)

Tracks are mastered at different loudness. Volume levelling measures each
track, writes the result into its tags, and turns the playback volume up
or down accordingly. The audio itself is never changed, and removing the
tags undoes everything.

This document is the contract for `src/core/sound/replaygain.h` and for
`linuxbeectl gain`. The locked tests `tests/locked/tst_replaygain.cpp` and
`tests/locked/cli_gain.sh` check it.

## Sources

| Tag | Source |
|---|---|
| W-AV | MusicBee wiki, "Analyze Volume": https://musicbee.fandom.com/wiki/Analyze_Volume |
| RG2 | ReplayGain 2.0 specification: https://wiki.hydrogenaud.io/index.php?title=ReplayGain_2.0_specification |
| R128 | RFC 7845, section 5.2.1 (the Opus `R128_TRACK_GAIN` and `R128_ALBUM_GAIN` tags): https://www.rfc-editor.org/rfc/rfc7845#section-5.2.1 |
| FF | ffmpeg's `ebur128` filter, as run with ffmpeg n9.0.2 |
| N-… | A linuxBee decision. |

Read on 2026-11-13. What MusicBee documents (W-AV): Analyze Volume
calculates the adjustment needed to normalise playback and writes Replay
Gain tags; the sound data is not changed; for an album level, all of the
album's tracks must be selected; Restore Original Volume removes the tags.

## N-TAGS: reading

`gainFromTags` looks only at `TrackTags::other`, keys in any case, first
value.

- `REPLAYGAIN_TRACK_GAIN` and `REPLAYGAIN_ALBUM_GAIN`: a decimal number
  with an optional sign and an optional `dB` after it (any case, with or
  without a space), white space around it ignored: `-6.50 dB`, `+3.8`,
  `-7dB`. Anything else (a comma, an exponent, two numbers, no number) is
  no value.
- `REPLAYGAIN_TRACK_PEAK` and `REPLAYGAIN_ALBUM_PEAK`: a decimal number
  above zero with no unit. Zero, a negative number or anything else is no
  value.

### N-R128

When a `REPLAYGAIN_*_GAIN` tag is missing or unreadable, `R128_TRACK_GAIN`
or `R128_ALBUM_GAIN` is used instead: a whole number from -32768 to 32767,
in 1/256 dB relative to -23 LUFS (R128). ReplayGain's reference is 5 dB
louder, so the gain is `value / 256 + 5`. These tags carry no peak.

## N-APPLY: the playback factor

`gainFactor(values, options)`:

1. Mode `Off`: 1.
2. The gain is the track gain in mode `Track` and the album gain in mode
   `Album`. If the track has no gain of that kind, the other kind stands
   in, together with its own peak.
3. No gain at all: `10^(untaggedDb / 20)`. The pre-amp is not added.
4. Otherwise the factor is `10^((gain + preampDb) / 20)`.
5. With `preventClipping`, when the peak that belongs to the gain used is
   known and `factor × peak` is above 1, the factor becomes `1 / peak`.

## N-REFERENCE

Loudness is measured in LUFS (integrated loudness, EBU R 128). The gain is
`-18 - loudness` (RG2: the reference level is -18 LUFS). The peak written
is the true peak as a linear value: `10^(dBFS / 20)`.

## N-PARSE: ffmpeg's summary

`parseLoudnessSummary` reads the text ffmpeg prints (FF). Only the text
from the last `Summary:` on counts. In it, the integrated loudness is the
number between `I:` and `LUFS`, and the true peak the number between
`Peak:` and `dBFS`.

It is not ok, with a reason in `error`, when there is no summary, when
either number is missing, or when the loudness is `-inf` or at or below
-70 LUFS (silence, or too short to measure). A peak of `-inf` with a real
loudness is read as -120 dBFS.

## N-MEASURE

`measureLoudness(files, ffmpegProgram)` runs ffmpeg as a child process
with the `ebur128` filter and `peak=true` on the first audio stream of
each file, waits for it and parses what it printed.

**One file** is measured as it is: its sample rate and its channels are
not changed on the way to the filter. A one-channel file is measured as
one channel and a six-channel file as six. (Mixing to two channels first,
as an earlier version of this document allowed, put a mono file's peak
3 dB too low and made a 5.1 file up to 3 dB too loud.)

**Several files** are an album, measured as one programme: the files
played one after another (RG2).

- The album's peak is the largest of the files' own true peaks, each
  measured as it is. It is never the peak of a converted signal.
- The album's loudness is that of the join. Files that all have the same
  sample rate and channel layout are joined as they are. Otherwise each
  file that differs is first brought to the highest sample rate among
  them and to the channel layout of the file with the most channels
  (among several such layouts, the one whose ffmpeg name sorts first).
  Going up to more channels keeps a file's loudness; mixing down would
  not.
- The order of the files does not change the result beyond rounding.

A one-channel file is not counted twice ("dual mono"): the tags say what
the file holds, whatever it is later played on.

It is not ok, with a reason, for an empty list, a path that is not a file,
a file ffmpeg cannot decode, or an ffmpeg that cannot be started. A file
name is only ever a file name: one that begins with `-` or contains `:`
must not be taken for an option or a protocol.

ffmpeg prints one decimal, so results are exact to 0.1 dB.

## N-FINITE

Every loudness, gain and peak is a finite number, at every door:

- `parseLoudnessSummary` is not ok when the loudness, or the peak, is
  `nan`, `inf` or any other text that is not a finite decimal number. The
  two exceptions stay as N-PARSE has them: a loudness of `-inf` is "too
  quiet", and a peak of `-inf` is -120 dBFS.
- `gainTagChanges` treats a measurement whose loudness or peak is not
  finite as not ok, whatever its `ok` says: it contributes nothing.
- `gainFromTags` gives no value for `nan`, `inf`, `infinity` (any case,
  any sign, with or without `dB`) or a number too large to hold.

## N-WRITE

`gainTagChanges(track, album)` returns `other` changes:

| Key | Value |
|---|---|
| `REPLAYGAIN_TRACK_GAIN`, `REPLAYGAIN_ALBUM_GAIN` | the gain with a sign, two decimals and ` dB`: `+3.80 dB`, `-8.70 dB`, `+0.00 dB` |
| `REPLAYGAIN_TRACK_PEAK`, `REPLAYGAIN_ALBUM_PEAK` | the linear peak with six decimals: `0.088105` |

A measurement that is absent or not ok contributes nothing, so its keys
are left as the file has them.

`removeGainTagChanges(tags)` removes every key of the track that starts
with `REPLAYGAIN_` (any case) and the keys `R128_TRACK_GAIN` and
`R128_ALBUM_GAIN`.

## N-CLI

```
linuxbeectl gain measure [--album] FILE...
linuxbeectl gain write   [--album] FILE...
linuxbeectl gain remove  FILE...
```

- `measure` prints, per file in the order given,
  `FILE: track +3.80 dB, peak 0.088105`, and with `--album` a last line
  `album: +3.50 dB, peak 0.088105` for the files as one album. Nothing is
  written.
- `write` does the same and saves the tags: track tags always, album tags
  with `--album`.
- `remove` takes the gain tags off each file and prints
  `FILE: N removed`.
- `measure` and `write` measure everything first: if any file cannot be
  measured, `failed: FILE: REASON` goes to standard error, the exit code is
  3, and no file is changed. A save that fails is reported the same way and
  the run goes on, exiting 3.
- Exit 2 for a usage mistake: no or an unknown subcommand, no file,
  `--album` with `remove`.

## Not in this version

- Applying the factor to sound: the player does that.
- Writing `R128_*` tags for Opus files (the `REPLAYGAIN_*` keys are written
  to every format).
- Measuring in parallel, and measuring new files automatically.
- An equaliser, crossfade and gapless playback.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| N-REFERENCE | Which reference level does Analyze Volume use (89 dB SPL of ReplayGain 1, or -18 LUFS)? |
| N-APPLY | Player preferences for replay gain: the modes offered, the pre-amp, and what happens to untagged tracks. |
| N-MEASURE | Does Analyze Volume count a one-channel file as two channels ("dual mono")? linuxBee does not. Analyse a mono file and compare its track gain with `linuxbeectl gain measure`. |
