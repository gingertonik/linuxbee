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
| W-UV | MusicBee wiki, "Useful Virtual Tag Formulas" (users' examples): https://musicbee.fandom.com/wiki/Useful_Virtual_Tag_Formulas |
| F-… | A linuxBee decision. Listed under "Open parity questions" where MusicBee's behaviour is unknown. |
| W-ANS | MusicBee wiki, the pages that answer the questions under "Answered from the MusicBee wiki": "How to Create a File-Naming Template", "Tracks View", "Special Fields": https://musicbee.fandom.com/wiki/How_to_Create_a_File-Naming_Template , https://musicbee.fandom.com/wiki/Tracks_View , https://musicbee.fandom.com/wiki/Special_Fields |

Pages were read on 2026-11-13.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Special Fields" (3.0.5972;
2020-12-31); "How to Create a File-Naming Template" (no version given;
2016-08-09); "Useful Virtual Tag Formulas" (no version given; 2017-07-26).

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
| Display Artist | the DISPLAY ARTIST tag (F-DISPLAY), else the Artist values joined with `"; "` | W-SF, W-TG |
| Artist & Title | `Artist - Title` when both have a value, otherwise Filename | W-SF |
| ArtistAndTitle | the same | W-SF |
| Filename | the file's name with its extension, no folders | W-SF |
| .Ext | the extension, without the dot | W-SF |
| Path | the folder the file is in | W-SF |
| URL | the full path | W-SF |
| Kind | F-KIND | W-SF |
| Time | `formatDuration(durationMs)` (F-TIME) || F (the name: W-SF) |
| Bitrate | the bitrate and a `k`: `320k` | F |
| Sample Rate | kilohertz with no trailing zeros, then ` kHz`: `44.1 kHz`, `48 kHz` || F (the name: W-SF) |
| Channels | `Mono` for 1, `Stereo` for 2, otherwise the number || F (the name: W-SF) |

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

The field itself is blank with no track number; when organising, a blank
Disc-Track# in the naming template is written `#` (paths.md, P-PLACEHOLDER).

### F-YEAR

The four-digit year is the first run of exactly four digits in the first
value of Year (a run with a digit on either side does not count).
`2011-01-24` and `24/01/2011` both give `2011`; `97` and `19975` give
nothing.

### F-DECADE

The first three digits of the four-digit year, then `0s`. Blank without a
year.

### F-DISPLAY

Display Artist is what a track list shows in its Artist column. It "is
saved to your files in its own frame" (W-SF): TXXX `DISPLAY ARTIST` in
ID3v2 and the `DISPLAY ARTIST` comment in Vorbis (W-TG); TagLib reports
it as the key `DISPLAY ARTIST` in every format, and linuxBee keeps it among
the other tags (tag-reading.md, R-OTHER). Its values joined with `"; "` and
trimmed are the field; when it is missing or blank, the Artist values
joined with `"; "` are (the owner's ruling of 2026-11-20). The tag is not a
catalogue field: it is not edited with the others, and changing the Artist
leaves it as it is (as MusicBee keeps the two apart, W-TG).

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
| F-DECADE | How is Decade written: `2010s`, `10s`, `2010`? linuxBee: `2010s`. |
| F-KIND | The Kind text for each format other than MP3. |
| Table | How Time, Bitrate, Sample Rate and Channels are written in a template. |
| F-DISPLAY | Answered (W-SF, W-TG): the stored DISPLAY ARTIST tag, else the join. |
| Year (yyyy) | Is the four-digit year written `<Year(yyyy)>` in a template? "Special Fields" lists the field as "Year (yyyy)"; a guide on "How to Create a File-Naming Template" writes `<Year(YYYY)>`. linuxBee: the name as in the table. |

### Decided by the owner (2026-11-20)

| Rule | Question | Decision |
|---|---|---|
| F-FILE | Does `<Path>` end with a separator? | No: a difference, and meant. MusicBee's seems to: "Special Fields" calls Path the folder without the file name, and "Useful Virtual Tag Formulas" shows how to take its last backslash away. |
| F-DISCTRACK | The default format of Disc-Track#. | `01-11`: `discDigits` stays 2, as the owner's own files are named. "Tracks View" gives `0-00` (`1-11`) as MusicBee's default; this is a difference, and it is meant. |

### Answered from the MusicBee wiki (checked 2026-11-20)

| Rule | Question | What the wiki says |
|---|---|---|
| F-DISCTRACK | With no disc number, is it `11` or `01-11`? With a one-disc album? The format is configurable in MusicBee; what is the default? linuxBee: 2 digits each, disc only when tagged. | With no disc number the track alone: "How to Create a File-Naming Template" gives `11` for a one-disc album whose disc is not tagged. The default format is still open above; two pages disagree: "Tracks View" gives `0-00` (so `1-11`), "How to Create a File-Naming Template" shows `01-11`. |
