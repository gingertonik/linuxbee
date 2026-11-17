# Reading tags

`readTags(path)` returns what one music file says about itself: its tags
under linuxBee's field names, its pictures, and its audio properties.

This document is the contract for `src/core/tags/tagreader.h`. The locked
tests `tests/locked/tst_tagreader.cpp` and `tests/locked/cli_tags.sh` check
it against the fixture files in `tests/data/tags/`. Nothing in this
subsystem writes to a music file.

The work of parsing each format is done by TagLib (version 2 or later,
LGPL-2.1 or MPL-1.1). No TagLib type appears in a public header.

## Sources

| Tag | Source |
|---|---|
| W-TG | MusicBee wiki, "Tagging" (which frame holds which field): https://musicbee.fandom.com/wiki/Tagging |
| TL | TagLib's property mapping: https://taglib.org/api/p_propertymapping.html and the headers of the installed library |
| ID3 | ID3v2.3.0 and ID3v2.4.0 frame lists: https://id3.org |
| F-V | MusicBee forum, topic 18622, a user's list of Vorbis comment names (2016): https://getmusicbee.com/forum/index.php?topic=18622.0 |
| GEN | `tests/data/tags/make_fixtures.py`, which wrote the fixtures with mutagen |
| R-… | A linuxBee decision. Listed again under "Open parity questions" where MusicBee's behaviour is unknown. |

Pages were read on 2026-11-13.

## 1. Result

### R-STATUS

- The path does not name a readable regular file (missing, a folder, an
  empty path, no permission): `Status::NotFound`.
- The file is there but no audio stream with a sample rate and at least one
  channel could be found in it: `Status::NotAudio`. A text file named
  `x.mp3`, an empty file and a file cut off inside its header are all
  `NotAudio`.
- Otherwise `Status::Ok`. A music file with no tags at all is `Ok` with
  empty fields.

On any status other than `Ok`, `tags` is a default-constructed `TrackTags`.
`readTags` never throws and never writes to the file.

### R-QUIET

The tag library prints diagnostics of its own. They are switched off: a
call to `readTags` writes nothing to standard output or standard error.

### R-FORMAT

`format` names the container that was actually found, whatever the file is
called: `mp3`, `flac`, `ogg` (Ogg Vorbis), `opus`, `m4a` (any MPEG-4 audio),
`wav`, `wv`. For a format TagLib reads that is not in this list, `format` is
the file's extension in lower case. A file with a wrong or missing extension
is recognised by its content when TagLib can do so.

### R-TAGTYPES

`tagTypes` lists the kinds of tag present, in this order:
`ID3v2.3` or `ID3v2.4` (the major version found), `APEv2`, `ID3v1`,
`Vorbis`, `MP4`, `RIFF Info`. Ogg Vorbis, Opus and FLAC comments are all
`Vorbis`.

### R-AUDIO

`durationMs`, `bitrateKbps`, `sampleRateHz` and `channels` are what TagLib
reports with its accurate read style. `bitsPerSample` is reported for FLAC,
WAV, WavPack and MPEG-4 and is 0 for the other formats. Opus reports
48000 Hz, the rate it always decodes at.

## 2. Fields

### R-CATALOGUE

`tagFieldNames()` returns these names in this order. The second column is
the TagLib property key, or keys, the field is read from (TL). The first
key that has a value wins.

| Field | Read from |
|---|---|
| Title | TITLE |
| Artist | ARTIST |
| Album | ALBUM |
| Album Artist | ALBUMARTIST, ALBUM ARTIST |
| Year | DATE, YEAR |
| Track# | TRACKNUMBER |
| Track Count | TRACKTOTAL, TOTALTRACKS |
| Disc# | DISCNUMBER |
| Disc Count | DISCTOTAL, TOTALDISCS |
| Genre | GENRE |
| Composer | COMPOSER |
| Conductor | CONDUCTOR |
| Publisher | LABEL, ORGANIZATION, PUBLISHER |
| Grouping | GROUPING, CONTENTGROUP (and see R-GROUPING) |
| Comment | COMMENT |
| Rating | see R-RATING |
| BPM | BPM |
| Mood | MOOD |
| Lyricist | LYRICIST |
| Lyrics | LYRICS, UNSYNCEDLYRICS |
| Compilation | COMPILATION |
| Copyright | COPYRIGHT |
| ISRC | ISRC |
| Sort Title | TITLESORT |
| Sort Artist | ARTISTSORT |
| Sort Album | ALBUMSORT |
| Sort Album Artist | ALBUMARTISTSORT |
| Sort Composer | COMPOSERSORT |
| Original Year | ORIGINALDATE, ORIGYEAR |
| Original Artist | ORIGINALARTIST, ORIGARTIST |
| Original Album | ORIGINALALBUM, ORIGALBUM |
| Subtitle | SUBTITLE |
| Language | LANGUAGE |
| Initial Key | INITIALKEY |
| Encoded By | ENCODEDBY |
| Mix Artist | REMIXER |
| Love | LOVE RATING, LOVERATING (R-LOVE) |

The frame behind each key in each format is TagLib's mapping (TL): TITLE is
`TIT2` in ID3v2, `TITLE` in a Vorbis comment, `©nam` in MPEG-4, and so on.
The Vorbis names ORGANIZATION, CONTENTGROUP, TOTALTRACKS, TRACKTOTAL and
TOTALDISCS are the ones the MusicBee wiki gives for Publisher, Grouping and
the totals (W-TG). UNSYNCEDLYRICS, ORIGYEAR, ORIGARTIST and ORIGALBUM are
from a forum member's list of the names found in files MusicBee had written
(F-V).

### R-FIELDS

- Property keys are compared in upper case.
- `fields` holds one entry per field that has at least one value. Values
  keep the order they have in the file. A value that is empty or only white
  space is dropped; other values are kept exactly as stored, without
  trimming.
- A field may have several values (two `ARTIST` comments, an ID3v2.4 frame
  with several strings). They stay separate.

### R-PRECEDENCE

When a file has more than one tag, the values come from the tag TagLib
prefers for that format: for MP3, ID3v2 before APEv2 before ID3v1.

### R-NUMBERS

When the first value of Track# contains a `/`, the text before it (trimmed)
becomes Track# and the text after it (trimmed) becomes Track Count, unless
Track Count already has a value from its own key. An empty side sets
nothing. Disc# and Disc Count work the same way. Numbers are kept as
written: `03` stays `03`.

### R-DATE23

ID3v2.3 keeps a date in pieces: `TYER` (year), `TDAT` (day and month, as
DDMM) and `TIME` (ID3). Year is the date they make together, as TagLib
reports it under DATE: `TYER` 1997 with `TDAT` 2209 is `1997-09-22`. These
frames are part of Year and are never listed in `unsupported`.

### R-GROUPING

In an ID3v2 tag (MP3, WAV) the frame `TIT1` is Grouping (W-TG). TagLib
reports `TIT1` under the key WORK, so for these formats WORK is read first,
before GROUPING and CONTENTGROUP. In every other format WORK is not a
catalogue field and goes to `other`.

### R-RATING

The Rating field holds stars as text: `0.5` to `5` in steps of a half,
written without a trailing zero (`4`, `3.5`). No rating means no entry.

- **ID3v2:** the rating byte of the first `POPM` frame, through
  `ratingFromPopm`: 0 and anything outside 1 to 255 is no rating; 1 is one
  star; any other value takes the stars of the nearest anchor, a tie going
  to the higher rating.

  | Byte | 13 | 54 | 64 | 118 | 128 | 186 | 196 | 242 | 255 |
  |---|---|---|---|---|---|---|---|---|---|
  | Stars | 0.5 | 1.5 | 2 | 2.5 | 3 | 3.5 | 4 | 4.5 | 5 |

- **Every other format:** the first value of the RATING key, through
  `ratingFromText`: the text is trimmed and read as a number n. Not a
  number, n ≤ 0 or n > 100 is no rating. For n ≤ 5 the stars are n; for
  larger n the stars are n / 20. The result is rounded to the nearest half
  (a quarter rounds up) and is never less than 0.5.

If an ID3v2 tag has both a `POPM` frame and a RATING text, `POPM` wins.

### R-LOVE

The Love field says a track is loved or banned. It holds one value:
`L` for loved, `B` for banned. No entry means neither.

It is read from the key `LOVE RATING`, and after it `LOVERATING` (an
MPEG-4 file has the name without its space). The first value of the
key, with white space trimmed from its ends and compared without regard
to case, is the letter; the field holds it in upper case.

A value that is neither letter is not a Love value: the field has no
entry and the key stays in `other`, as a RATING that is not a number
does.

The ID3v2 and MPEG-4 forms are as found in files MusicBee had written
(a `TXXX` frame described `LOVE RATING`; the atom
`----:com.apple.iTunes:LOVERATING`; both with the value `L`). The
Vorbis name and the value `B` are as MusicBee's forum gives them; no
file was seen with either.

### R-OTHER

Every text property that no catalogue field claimed goes to `other` under
its upper-cased key, values in file order, empty values dropped. This
includes the losing key when two keys feed one field.

### R-UNSUPPORTED

`unsupported` lists what TagLib reports as unsupported data, by name,
except pictures and the `POPM` frame, which are reported in `pictures` and
in Rating, and the ID3v2.3 date frames of R-DATE23. An entry may carry a
prefix: `UNKNOWN/TDAT` is the `TDAT` frame.

## 3. Pictures

### R-PICTURES

`pictures` holds every embedded picture in file order, from TagLib's
`PICTURE` complex property: ID3v2 `APIC`, FLAC picture blocks, the
`METADATA_BLOCK_PICTURE` comment of Ogg files, MPEG-4 `covr`, APEv2 cover
art items.

- `type` is the ID3v2 picture type name TagLib gives (`Front Cover`,
  `Back Cover`, ...). A picture with no type is `Front Cover`.
- `mimeType` is what the tag says. When the tag says nothing (an APEv2
  picture item), it is `imageMimeType` of the data (R-MIME).
- `description` is the tag's description text. For an APEv2 picture item it
  is the file name stored in the item.
- `byteCount` is always the size of the picture.
- With `ReadOptions::pictureData` false, `data` is left empty.
- With `ReadOptions::pictures` false, the list is empty.

### R-MIME

`imageMimeType` looks at the first bytes: `89 50 4E 47 0D 0A 1A 0A` is
`image/png`; `FF D8 FF` is `image/jpeg`; `GIF87a` or `GIF89a` is
`image/gif`; `BM` is `image/bmp`; `RIFF`, four bytes, then `WEBP` is
`image/webp`. Anything else gives an empty text.

## 4. Helpers

- `TrackTags::first(name)`: the first value of the field, or an empty text.
- `TrackTags::joined(name)`: the values joined with `"; "`.

### R-SOURCE

`TrackTagSource` lets a template read a `TrackTags`. A name that matches a
catalogue field, ignoring case, gives `joined(field)`, which is an empty
text when the field has no value. Any other name is unknown (`nullopt`).

## 5. Command line

### R-CLI

```
linuxbeectl tags read [--field NAME] FILE
```

Without `--field`, the report is these lines in this order:

```
format=<format>
tags=<tag types joined with a comma>
duration_ms=<n>
bitrate_kbps=<n>
sample_rate_hz=<n>
channels=<n>
bits_per_sample=<n>
field <Name>=<value>          one per value, fields in catalogue order
other <KEY>=<value>           one per value, keys in ascending order
unsupported <name>            one per entry
picture type=<type> mime=<mime> bytes=<n> description=<text>
```

In a value, a backslash is written `\\`, a line feed `\n` and a carriage
return `\r`. Picture data is not read.

With `--field NAME`, only that field's values are printed, one per line,
escaped the same way; a field with no value prints nothing and exits 0. NAME
is matched ignoring case. A NAME that is not a catalogue field is a usage
mistake.

- `NotFound`: `not found: FILE` on standard error, exit 3.
- `NotAudio`: `not a music file: FILE` on standard error, exit 3.
- A usage mistake: a message on standard error, exit 2.

Nothing else is written to standard error.

## Not in this version

- Writing tags (the next subsystem).
- Fields computed from other fields or from the file (Disc-Track#, Decade,
  Filename, Date Added): they belong to the library.
- Custom tags, the artist roles, synchronised lyrics, WMA.
- Album rating, Keywords, Quality, Tempo, Occasion: they arrive with
  tag writing, once the frame for each is settled.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| R-RATING | Which `POPM` byte does each star rating, including the halves, produce? The anchor table is the reviewer's recollection of a widely used convention and is not from a MusicBee document. |
| R-RATING | What number does MusicBee write in a Vorbis `RATING` comment for 4 stars? linuxBee reads 80 and 4 both as 4 stars. |
| R-RATING | Is there a zero rating that differs from no rating, and how is it stored? linuxBee: not read. |
| R-NUMBERS | Is a track number with a leading zero shown as `03` or `3`? linuxBee: as written. |
| R-GROUPING | Does MusicBee also read the `GRP1` frame as Grouping? linuxBee: yes, after `TIT1`. |
| R-SOURCE | Are field names in a template matched ignoring case? linuxBee: yes. |
| R-PRECEDENCE | Does an APEv2 tag in an MP3 win over ID3v2? linuxBee: no. |
