# Converting files

`convertFile(job)` turns one music file into another format. ffmpeg does
the audio; linuxBee's own tag reader and writer carry the tags and pictures
across, so a field means the same thing before and after whatever the two
formats are.

This document is the contract for `src/core/convert/converter.h` and for
`linuxbeectl convert`. The locked tests `tests/locked/tst_convert.cpp` and
`tests/locked/cli_convert.sh` check it on the fixtures in
`tests/data/convert/`.

ffmpeg is a separate program that must be installed. It is run as a child
process and never linked.

## Sources

| Tag | Source |
|---|---|
| W-CV | MusicBee wiki, "Convert Format": https://musicbee.fandom.com/wiki/Convert_Format |
| FF | The ffmpeg manuals: https://ffmpeg.org/ffmpeg.html , https://ffmpeg.org/ffmpeg-codecs.html , https://ffmpeg.org/ffmpeg-formats.html |
| GEN | `tests/data/convert/make_fixtures.py` |
| C-… | A linuxBee decision. |

Pages were read on 2026-11-13. The manuals carry no version; the proofs for
this document were run with ffmpeg n9.0.2.

What MusicBee documents (W-CV): a choice of output format and of an
"encoding profile"; output to a new folder with sub-folders from the naming
template, or beside the source; converted files keep their tags; artwork is
carried over; an option to skip a file whose output exists and only bring
its tags in step. It gives no bitrate for any profile, so the presets below
are linuxBee's own.

## 1. Formats

### C-FORMATS

| `AudioFormat` | id | File extension | ffmpeg encoder | ffmpeg muxer | Lossless |
|---|---|---|---|---|---|
| Mp3 | `mp3` | `mp3` | `libmp3lame` | `mp3` | no |
| Flac | `flac` | `flac` | `flac` | `flac` | yes |
| OggVorbis | `ogg` | `ogg` | `libvorbis` | `ogg` | no |
| Opus | `opus` | `opus` | `libopus` | `opus` | no |
| Aac | `aac` | `m4a` | `aac` | `ipod` | no |
| Alac | `alac` | `m4a` | `alac` | `ipod` | yes |
| Wav | `wav` | `wav` | `pcm_s16le`, `pcm_s24le`, `pcm_s32le` | `wav` | yes |
| WavPack | `wavpack` | `wv` | `wavpack` | `wv` | yes |

`formatFromId` ignores case. Any source that `readTags` accepts and ffmpeg
can decode may be converted.

### C-PRESETS

`encoderPresets()` returns these, in this order. The first is the default.

| id | Name | Settings |
|---|---|---|
| `mp3-320` | MP3 320 kbps (CBR) | Mp3, bitrate 320 |
| `mp3-v0` | MP3 V0 (VBR, about 245 kbps) | Mp3, quality 0 |
| `mp3-v2` | MP3 V2 (VBR, about 190 kbps) | Mp3, quality 2 |
| `mp3-192` | MP3 192 kbps (CBR) | Mp3, bitrate 192 |
| `mp3-128` | MP3 128 kbps (CBR) | Mp3, bitrate 128 |
| `flac` | FLAC | Flac, compression level 5 |
| `flac-8` | FLAC (smallest files) | Flac, compression level 8 |
| `flac-cd` | FLAC, 16-bit 44.1 kHz | Flac, level 5, sample rate 44100, bit depth 16 |
| `ogg-q5` | Ogg Vorbis q5 (about 160 kbps) | OggVorbis, quality 5 |
| `ogg-q8` | Ogg Vorbis q8 (about 256 kbps) | OggVorbis, quality 8 |
| `opus-128` | Opus 128 kbps | Opus, bitrate 128 |
| `opus-96` | Opus 96 kbps | Opus, bitrate 96 |
| `opus-192` | Opus 192 kbps | Opus, bitrate 192 |
| `aac-256` | AAC 256 kbps | Aac, bitrate 256 |
| `aac-160` | AAC 160 kbps | Aac, bitrate 160 |
| `alac` | Apple Lossless | Alac |
| `wav` | WAV | Wav |
| `wavpack` | WavPack | WavPack |

Everything a row does not mention is left at "keep what the source has"
(sample rate 0, bit depth 0, channels 0, compression level -1).

## 2. Settings

### C-RATE

A lossy format is set by bitrate or by quality (`rateControl`):

| Format | Bitrate | Quality |
|---|---|---|
| Mp3 | constant bitrate; one of 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 | LAME's V level, a whole number 0 (best) to 9 |
| OggVorbis | average bitrate, 32 to 512 | -1 to 10 (best), fractions allowed |
| Opus | 6 to 510 (ffmpeg's default variable mode) | not offered |
| Aac | 32 to 512 | not offered |

Lossless formats ignore `rateControl`, `bitrateKbps` and `quality`.

### C-VALID

`validateSettings` returns one line per problem, each starting with the
name of the setting and a colon:

- `bitrate:` or `quality:` outside C-RATE, for the control that is selected;
- `bit depth:` set (not 0) on a lossy format; not 16, 24 or 32 on a lossless
  one; 32 on Alac;
- `compression level:` -1 means "not set" and is valid on every format. A
  level that is set is a problem on anything but Flac, and on Flac when it
  is outside 0 to 12;
- `sample rate:` not 0 and not one of 8000, 11025, 12000, 16000, 22050,
  24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000; above 48000 on
  Mp3; on Opus anything but 8000, 12000, 16000, 24000, 48000;
- `channels:` not 0, 1 or 2.

### C-DEPTH

For a lossless target the bit depth is `bitDepth` when it is set. Otherwise
it is the source's `bitsPerSample` when that is 24 or 32, and 16 in every
other case (a 16-bit source, an 8-bit one, a lossy source that has no bit
depth). Alac has no 32: it gets 24.

With sample rate 0 no rate is asked for. ffmpeg keeps the source's rate
when the encoder can hold it and otherwise picks the nearest it can (a
96 kHz file becomes 48 kHz as MP3 or Opus).

### C-ARGS

`ffmpegArguments(input, output, settings, sourceBitsPerSample)` returns, in
this order:

```
-nostdin -hide_banner -loglevel error -nostats -progress pipe:1 -y
-i INPUT -map 0:a:0 -map_metadata -1 -map_chapters -1 -fflags +bitexact
<encoder arguments>
[-ar RATE]            when sampleRateHz is not 0
[-ac CHANNELS]        when channels is not 0
-f MUXER OUTPUT
```

Only the first audio stream is taken. No tag, chapter or picture is copied
by ffmpeg (C-TAGS does that), and `+bitexact` keeps ffmpeg from writing its
own "encoder" tag.

Encoder arguments:

| Case | Arguments |
|---|---|
| Mp3, bitrate | `-c:a libmp3lame -b:a 320k` |
| Mp3, quality | `-c:a libmp3lame -q:a 0` |
| OggVorbis, quality | `-c:a libvorbis -q:a 6.5` |
| OggVorbis, bitrate | `-c:a libvorbis -b:a 192k` |
| Opus | `-c:a libopus -b:a 128k` |
| Aac | `-c:a aac -b:a 256k` |
| Flac | `-c:a flac`, then `-compression_level N` when the level is not -1, then the depth arguments with `s16` and `s32` |
| Alac | `-c:a alac`, then the depth arguments with `s16p` and `s32p` |
| WavPack | `-c:a wavpack`, then the depth arguments with `s16p` and `s32p` |
| Wav | `-c:a pcm_s16le`, `pcm_s24le` or `pcm_s32le` by depth |

Depth arguments: 16 is `-sample_fmt s16` (or `s16p`); 24 is
`-sample_fmt s32 -bits_per_raw_sample 24` (or `s32p`); 32 is
`-sample_fmt s32` (or `s32p`).

A quality is printed with no trailing zeros: `0`, `6.5`.

## 3. Converting

### C-STEPS

1. Invalid settings (C-VALID) or an empty destination: `InvalidJob`, with
   the problems joined by `"; "` in `message`.
2. Read the source with `readTags`. Not found: `SourceNotFound`. Not audio:
   `SourceNotAudio`.
3. If the destination names the source file itself: `InvalidJob`.
4. If the destination exists: C-EXISTS.
5. Call the progress function with 0. If it returns false: `Cancelled`.
6. Create the destination's folders.
7. Run ffmpeg, writing to the part file (C-CLEAN), reporting progress
   (C-PROGRESS).
   Then the part file is looked at (C-WHOLE).
8. Copy the tags to the part file (C-TAGS).
9. Move the part file onto the destination, replacing it when C-EXISTS said
   to overwrite. Call the progress function with 1. `Converted`, with
   `outputPath` set. The file is finished by then: what that last call
   answers changes nothing.

The source is never opened for writing.

### C-EXISTS

When the destination exists already:

| `ifExists` | What happens |
|---|---|
| Skip (default) | `Skipped`. Nothing is run or written. `outputPath` is the destination. |
| Overwrite | The conversion runs. The old file is replaced only at step 9, so a failed or cancelled run leaves it as it was. |
| Rename | The result goes to the first free name from `uniqueRelativePath` (`song_1.mp3`, `song_2.mp3`: P-DUP in `docs/spec/paths.md`). Two conversions started at the same moment into one folder may both choose the same free name; then one of them ends `Failed` and no file is overwritten. Callers that run several at once give each its own destination (convert-batch.md). |
| UpdateTags | ffmpeg is not run. C-TAGS is applied to the existing file: `TagsUpdated`. With `copyTags` false this is `Skipped`. (W-CV: "skip conversion when the output exists and sync tags only".) |

### C-TAGS

With `copyTags` true the target ends up with the source's tags, written
with `writeTags` and `job.tagOptions`:

- every catalogue field is set to the source's values, and fields the
  source lacks are removed;
- every `other` tag of the source is set, except `ENCODER`, `ENCODING`,
  `ENCODERSETTINGS` and `LENGTH`, which describe the source's encoding; an
  `other` tag the target has and the source lacks is removed;
- with `copyPictures` true the pictures become the source's pictures; with
  it false the target's pictures are not touched (a fresh file has none).

If `writeTags` refuses some entries (`InvalidChange`), those entries are
dropped, each is reported in `warnings` as `not copied: ` followed by the
problem line, and the rest is written. Any other failure to write the tags
makes the job `Failed`.

With `copyTags` false no tag and no picture is written; the file holds only
what the encoder put there itself (ffmpeg's Ogg writer adds an `ENCODER`
comment).

### C-PROGRESS

The progress function is called first with exactly 0, before ffmpeg
starts. While ffmpeg runs it is called for each `out_time_us=N` line ffmpeg
prints, with N divided by the source's length in microseconds, never less
than the previous value and never more than 0.99. After a successful job it
is called once with exactly 1.

An encoder that says nothing can still be stopped: while ffmpeg runs the
function is asked at least about once a second. When a second has gone by
without an `out_time_us` line, it is called again with the value it was
last given (0 before the first line).

Whenever it returns false the job stops: ffmpeg is killed and the result is
`Cancelled`.

### C-ENCODER

- ffmpeg cannot be started: `EncoderMissing`.
- ffmpeg exits with a failure: `Failed`. `message` is the last non-empty
  line it wrote to standard error, trimmed, or `ffmpeg failed` when it wrote
  nothing.

### C-WHOLE: the encoder is not taken at its word

(Found by the write-path audit, part 3, 2026-11-18: an encoder that
ended well was believed whatever it had written. With Overwrite a
short result replaced the user's file, which was deleted. With no
tags to copy, a result that was no sound at all went through.)

After ffmpeg has ended well and before anything else is done, the part
file is read with `readTags`, whether or not tags are to be copied:

- it cannot be read as music (or is not there): `Failed`, `the
  converted file could not be read`;
- **its length is more than one second less than the source's**
  (`durationMs` of each, as `readTags` gives them): `Failed`, `the
  converted file is shorter than the source`. A source whose length
  is not known (0) is not compared.

Either way the part file is removed and the destination is as it was
(C-CLEAN).

Why a second, and not a share of the length: an hour-long file may
not lose half a minute unseen. Lengths are read from headers; over
251 real files (MP3, FLAC, M4A, Opus) the length read was never more
than 71 ms above what ffmpeg decodes.

Known and left:

- The length is the one the result's own header states. A file cut
  off *after* it was finished states its whole length and passes. An
  encoder that stops early does not leave such a file: the formats
  here write their length at the end.
- A source whose header overstates its length by more than a second
  (an old MP3 of changing bitrate with no length header) cannot be
  converted: it fails with the second message. None was among the 251.
- What ffmpeg writes to standard error while ending well is not
  shown. It did so for 28 of the 251, and each of those decoded to
  its whole length, so a complaint alone does not fail a job.

### C-CLEAN

ffmpeg writes to a part file in the destination's folder. Its name is
`.lbpart-` + six characters chosen at random from `a` to `z` and `0` to
`9` + `-` + the destination's own file name: for `song.mp3`,
`.lbpart-k3x9qa-song.mp3`. If a file of that name exists, another is
chosen. (The same pattern as a safe tag save, tag-safe-save.md.)

A conversion never removes a file it did not make. In particular a file
called `song.part.mp3`, `song.part` or anything else is the user's and is
left alone. The one exception is what an earlier conversion of this very
destination left behind when it was killed: files in the destination's
folder named exactly `.lbpart-`, six such characters, `-` and the
destination's file name are removed before ffmpeg starts.

Whatever the outcome, no part file is left behind, and on any outcome other
than `Converted` or `TagsUpdated` the destination is exactly as it was.
Folders created at step 6 may remain.

## 4. Command line

### C-CLI

```
linuxbeectl convert --list-presets
linuxbeectl convert [--preset ID | --format F] [--bitrate K] [--quality Q]
                    [--level L] [--sample-rate HZ] [--bit-depth N] [--channels N]
                    (--output FILE | --out-dir DIR [--template T])
                    [--if-exists skip|overwrite|rename|update-tags]
                    [--no-tags] [--no-pictures] [--ffmpeg PATH] FILE...
```

- `--list-presets` prints one line per preset: id, a tab, name.
- Settings start from the first preset, or from `--preset ID`, or from
  `--format F`. `--format` starts from: bitrate 320 for mp3; quality 5 for
  ogg; bitrate 192 for opus and aac; nothing for the lossless formats.
  `--bitrate` selects bitrate control and `--quality` quality control. The
  number options then replace single settings.
- `--output FILE`: the destination, for exactly one input.
- `--out-dir DIR`: each input goes to DIR. Without `--template` the name is
  the input's name with the new extension. With `--template T` it is
  `buildRelativePath` (`docs/spec/paths.md`) of T over the input's
  `TrackFieldSource` (`docs/spec/track-fields.md`), with the default rules.
- **C-OWN: a run never writes onto one of its own inputs, nor onto
  the result of an earlier input**, whatever `--if-exists` says (the
  rule of B-DEST in convert-batch.md, which the window has). With
  `--out-dir`, every input is taken before any destination is chosen;
  a destination that is taken gets `_1` before its extension, or
  `_2`, and so on, the first that is free, and is then taken itself.
  A path is known by its real folder, so a folder named through a
  link is the same folder. Not for `--output`, which has one input
  (C-STEPS 3 refuses the source itself).
- One line per input on standard output, in order:
  `converted: INPUT -> OUTPUT`, `skipped: INPUT -> OUTPUT` or
  `tags updated: INPUT -> OUTPUT`.
- An input that fails prints `failed: INPUT: REASON` on standard error and
  the run goes on. REASON is `not found`, `not a music file`, `cancelled`,
  `missing tags: A, B` (P-MISSING), `the template leaves no file name`, or
  the job's `message`. Each warning prints as `warning: INPUT: TEXT` on
  standard error.
- Exit 0 when no input failed, 3 when any did.
- Usage mistakes exit 2 and convert nothing: no input; neither or both of
  `--output` and `--out-dir`; `--output` with several inputs; `--template`
  without `--out-dir`; `--preset` together with `--format`; an unknown
  preset, format or `--if-exists` word; a number option that is not a
  number; settings that fail C-VALID; a template that does not parse
  (`error at N: message`).

## Not in this version

- Several conversions at once. `convertFile` may be called from several
  threads; a queue with a thread limit comes with the convert dialog.
- Volume levelling ("permanently adjust volume" in W-CV) and ReplayGain.
- Replacing the source file, adding results to the library or inbox, and
  play-count handling: these belong to the dialog and the library.
- Encoders ffmpeg does not have, and user-defined command lines.
- Cue sheets and chapters.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| C-PRESETS | The names and the bitrates behind MusicBee's four encoding profiles for each format. |
| C-EXISTS | When the output exists and "skip and sync tags" is off, does MusicBee overwrite, rename or skip? linuxBee: skip. |
| C-TAGS | Does a converted file keep the source's ReplayGain tags? linuxBee: yes. |
| C-DEPTH | Does MusicBee keep 24-bit when converting FLAC to FLAC or ALAC? linuxBee: yes. |

### C-TAGS and a file that is not saved

When the file the tags go to is refused as `HardLinked` or
`ChangedOnDisk` (tag-safe-save.md), C-TAGS answers
`the tags could not be written: <the line>`, and the file is as it
was. This is reached by "update tags" on an output file that has
another name.
