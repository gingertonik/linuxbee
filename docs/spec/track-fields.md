# Track fields

A naming template is filled in from a *track*: a file's tags plus the
fields worked out from them and from the file itself. `TrackFieldSource`
is that set of fields, as a tag source for the template engine.

This document is the contract for `src/core/fields/trackfields.h`. The
locked test `tests/locked/tst_trackfields.cpp` checks it. Nothing here
reads the disk: the tags come from `readTags` (see
`docs/spec/tag-reading.md`) and the path is given by the caller.

Fields that need the library (Date Added, Play Count, Last Played,
Category, Playlist) are added by the library, not here.

## Sources

| Tag | Source |
|---|---|
| W-SF | MusicBee wiki, "Special Fields": https://musicbee.fandom.com/wiki/Special_Fields |
| W-H | MusicBee wiki, "How to Create a File-Naming Template": https://musicbee.fandom.com/wiki/How_to_Create_a_File-Naming_Template |
| F-… | A linuxBee decision. Listed under "Open parity questions" where MusicBee's behaviour is unknown. |

Pages were read on 2026-11-13.

## Rules

### F-NAMES

`trackFieldNames()` is `tagFieldNames()` followed by the computed fields in
the order of the table below. A field name is matched ignoring case. A name
that is not in the list is unknown (`nullopt`). Every name in the list is
known: when it has nothing to show, its value is an empty text.

### F-TAGS

A catalogue field gives its values joined with `"; "`, exactly as
`TrackTagSource` does (R-SOURCE).

### Computed fields

| Field | Value | Source |
|---|---|---|
| Disc-Track# | F-DISCTRACK | W-SF, W-H |
| Year (yyyy) | the four-digit year of Year (F-YEAR) | W-SF |
| Decade | the year with its last digit replaced by `0s`: `2010s` (F-DECADE) | W-SF |
| Album [Year] | `Album [yyyy]`; just Album when there is no year; blank when there is no Album | W-SF |
| Display Artist | the Artist values joined with `"; "` (F-DISPLAY) | F |
| Artist & Title | `Artist - Title` when both have a value, otherwise Filename | W-SF |
| ArtistAndTitle | the same | W-SF |
| Filename | the file's name with its extension, no folders | W-SF |
| .Ext | the extension, without the dot | W-SF |
| Path | the folder the file is in | W-SF |
| URL | the full path | W-SF |
| Kind | F-KIND | W-SF |
| Time | `formatDuration(durationMs)` (F-TIME) | F |
| Bitrate | the bitrate and a `k`: `320k` | F |
| Sample Rate | kilohertz with no trailing zeros, then ` kHz`: `44.1 kHz`, `48 kHz` | F |
| Channels | `Mono` for 1, `Stereo` for 2, otherwise the number | F |

### F-DISCTRACK

- Take the first value of Track# and of Disc#, trimmed.
- No track number: the field is blank, whatever the disc.
- A value that is a whole number is read by its value, so zeros it
  already has in front are dropped (`007` is 7), and is then padded with
  zeros on the left to `trackDigits` (track) or `discDigits` (disc); both
  default to 2. A number with more digits than that is not cut (`123`
  stays `123`). A value that is not a whole number is used as written.
- No disc number: the padded track alone. Otherwise disc, `-`, track.

Disc 1, track 11 is `01-11` (W-H).

### F-YEAR

The four-digit year is the first run of exactly four digits in the first
value of Year (a run with a digit on either side does not count).
`2011-01-24` and `24/01/2011` both give `2011`; `97` and `19975` give
nothing.

### F-DECADE

The first three digits of the four-digit year, then `0s`. Blank without a
year.

### F-DISPLAY

Display Artist is what a track list shows in its Artist column. Until the
artist roles exist it is the Artist values joined with `"; "`.

### F-FILE

The path uses `/`. Filename is the text after the last `/`. `.Ext` is the
text after the last dot of Filename, with its case unchanged, and is blank
when Filename has no dot or only a leading one. Path is the text before the
last `/`, without a trailing separator; for a file directly under the root
it is `/`. With an empty path, all four file fields are blank. Paths are
meant to be absolute; for a text with no `/` in it, Path is blank and the
whole text is the Filename.

### F-KIND

| `format` | Kind |
|---|---|
| mp3 | MPEG audio file |
| flac | FLAC audio file |
| ogg | Ogg Vorbis audio file |
| opus | Opus audio file |
| m4a | MPEG-4 audio file |
| wav | WAV audio file |
| wv | WavPack audio file |
| anything else | the format in upper case, then ` audio file` |

### F-TIME

`formatDuration` rounds to the nearest second (half a second rounds up; a
negative length counts as 0) and prints `m:ss` under an hour and `h:mm:ss`
from an hour up. Minutes under an hour are not padded: `0:07`, `3:05`,
`1:02:03`.

### F-AUDIO

When `TrackTags::format` is empty the tags did not come from a file, and
Kind, Time, Bitrate, Sample Rate and Channels are blank. Bitrate, Sample
Rate and Channels are also blank when their number is 0.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| F-DISCTRACK | With no disc number, is it `11` or `01-11`? With a one-disc album? The format is configurable in MusicBee; what is the default? linuxBee: 2 digits each, disc only when tagged. |
| F-DECADE | How is Decade written: `2010s`, `10s`, `2010`? linuxBee: `2010s`. |
| F-FILE | Does `<Path>` end with a separator? linuxBee: no. |
| F-KIND | The Kind text for each format other than MP3. |
| Table | How Time, Bitrate, Sample Rate and Channels are written in a template. |
| F-DISPLAY | What Display Artist shows for two artists. |
