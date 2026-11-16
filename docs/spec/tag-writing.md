# Writing tags

`writeTags(path, changes, options)` changes the tags and pictures of one
music file and leaves everything it was not asked to change exactly as
it was. The file is never changed in place: how a save reaches the disk
is in `docs/spec/tag-safe-save.md`.

This document is the contract for `src/core/tags/tagwriter.h`. The locked
tests `tests/locked/tst_tagwriter.cpp` and `tests/locked/cli_tagwrite.sh`
check it on copies of the fixtures in `tests/data/tags/`. It builds on
`docs/spec/tag-reading.md`; field names, the catalogue and the rating
anchors are defined there.

TagLib does the writing. No TagLib type appears in a public header.

## Sources

| Tag | Source |
|---|---|
| W-TG | MusicBee wiki, "Tagging": https://musicbee.fandom.com/wiki/Tagging |
| W-TP | MusicBee wiki, "Tags Preferences": https://musicbee.fandom.com/wiki/Tags_Preferences |
| F-V | MusicBee forum, topic 18622, a user's list of Vorbis comment names: https://getmusicbee.com/forum/index.php?topic=18622.0 |
| TL | TagLib's property mapping and the headers of the installed library |
| ID3 | ID3v2.3.0 and ID3v2.4.0 documents and the ID3v1 layout: https://id3.org |
| W-… | A linuxBee decision, except W-TG and W-TP above. |

Pages were read on 2026-11-13.

## 1. The order of work

1. **W-STATUS.** Read the file as `readTags` does. A path that is not a
   readable regular file gives `NotFound`; a file with no audio in it gives
   `NotAudio`.
2. **W-INVALID.** Check every entry of the change (section 2). If any is
   refused, the result is `InvalidChange` with one line per refused entry
   and nothing is written.
3. **W-UNCHANGED.** Work out what the file would say afterwards. If that is
   what it says now, the result is `Unchanged` and the file is not opened
   for writing: its bytes and its modification time stay as they are.
4. If the file cannot be written to, the result is `NotWritable`.
5. Save. If the tag library reports failure, the result is `Failed`.
   Otherwise `Ok`.

`writeTags` never throws and prints nothing (R-QUIET).

## 2. What a change may contain

### W-FIELDS

- `changes.fields` maps a catalogue field name, matched ignoring case, to
  its new list of values. Values that are empty or only white space are
  dropped from the list. If the list is then empty, the field is removed.
- A field that is not named is not touched.
- `changes.other` does the same for tags outside the catalogue. Keys are
  upper-cased.

### W-INVALID

An entry is refused when:

- its field name is not in the catalogue;
- the field is Rating and the list is not a single number from 0.5 to 5 in
  steps of 0.5;
- the field is Track#, Track Count, Disc# or Disc Count and the list has
  more than one value or the value contains `/`;
- an `other` key is empty, has a character outside printable ASCII
  (U+0020 to U+007E), or contains `=`;
- an `other` key, upper-cased, is one of the keys the catalogue reads
  (R-CATALOGUE, for example `TITLE` or `TRACKTOTAL`) or is
  `METADATA_BLOCK_PICTURE`: such a change belongs to the field or to
  `pictures`;
- a picture has no data.

Each line in `problems` starts with the field name or key exactly as the
caller gave it, then a colon. For a picture it starts with `picture N:`,
counting from 1.

### W-UNCHANGED

"What the file says" is what `readTags` returns: `fields`, `other` and
`pictures`. Pictures are compared by data and MIME type, and by type and
description too in formats that store them (every format here except
MPEG-4). Removing a field that is not there is no change.

## 3. Where each field is written

### W-TABLE

The key each field is written under. TagLib turns the key into the frame,
comment or atom for the format (TL).

| Field | ID3v2 (MP3, WAV) | Vorbis comment (FLAC, Ogg, Opus) | MPEG-4, APEv2 (WavPack) |
|---|---|---|---|
| Year | DATE | DATE | DATE |
| Track#, Track Count | TRACKNUMBER, as `n/total` | TRACKNUMBER and TRACKTOTAL | TRACKNUMBER, as `n/total` |
| Disc#, Disc Count | DISCNUMBER, as `n/total` | DISCNUMBER and DISCTOTAL | DISCNUMBER, as `n/total` |
| Publisher | LABEL (`TPUB`) | ORGANIZATION (W-TG) | LABEL |
| Grouping | WORK (`TIT1`, W-TG) | CONTENTGROUP (W-TG) | GROUPING |
| Lyrics | LYRICS (`USLT`) | UNSYNCEDLYRICS (F-V) | LYRICS |
| Original Year | ORIGINALDATE | ORIGYEAR (F-V) | ORIGINALDATE |
| Original Artist | ORIGINALARTIST | ORIGARTIST (F-V) | ORIGINALARTIST |
| Original Album | ORIGINALALBUM | ORIGALBUM (F-V) | ORIGINALALBUM |
| Rating | the `POPM` frame (W-RATING) | RATING | RATING |
| every other field | the first key in its R-CATALOGUE row | the same | the same |

### W-ONEHOME

When a field is written or removed, every key its R-CATALOGUE row reads
from is removed first (and WORK, for Grouping in an ID3v2 tag). A field
never ends up stored under two spellings.

### W-NUMBERS

A number and its count travel together: changing either rewrites both from
the values the file will have afterwards. In the `n/total` formats a
missing count gives a bare `n`, and a count with no number gives `/total`.
In a Vorbis comment a stored `2/8` is split into TRACKNUMBER `2` and
TRACKTOTAL `8` the first time either is written.

### W-MULTI

Several values are stored as the format stores a list: one comment per
value in a Vorbis comment, one frame with several strings in ID3v2, as
TagLib does it. `readTags` returns them as separate values again.

### W-RATING

- ID3v2: the rating byte of the `POPM` frame, from `popmFromRating`:

  | Stars | 0.5 | 1 | 1.5 | 2 | 2.5 | 3 | 3.5 | 4 | 4.5 | 5 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | Byte | 13 | 1 | 54 | 64 | 118 | 128 | 186 | 196 | 242 | 255 |

  If the file has a `POPM` frame, its address text and its play counter are
  kept. A new frame has an empty address and a counter of 0. Removing the
  rating removes the frame.
- Every other format: the RATING key, holding stars times 20 as a whole
  number (`70` for 3.5 stars).

### W-PICTURES

When `changes.pictures` is set, the file's pictures become exactly that
list, in order; an empty list removes them all. When it is not set, the
pictures are not touched. A picture with an empty `type` is a
`Front Cover`. A picture with an empty `mimeType` gets `imageMimeType(data)`
(R-MIME in `docs/spec/tag-reading.md`).

## 4. What is kept

### W-KEEP

- The audio is never re-encoded or moved between containers: its packets
  are byte-for-byte the same afterwards.
- Fields, other tags and pictures that the change does not name read back
  exactly as before.
- Tags the reader lists as unsupported are kept when TagLib keeps them.

## 5. Options

### W-ID3VER

`id3Version` decides the version of an ID3v2 tag that is saved (MP3, WAV).

| `id3Version` | The tag is written as |
|---|---|
| `Keep` (the default) | the version the file's ID3v2 tag has now: ID3v2.4 stays ID3v2.4, ID3v2.3 stays ID3v2.3. A file with no ID3v2 tag, or with an older one (ID3v2.2, which cannot be written), gets ID3v2.4. |
| `V23` | ID3v2.3, whatever the file had |
| `V24` | ID3v2.4, whatever the file had |

An MP3 with no ID3v2 tag gets one.

Why `Keep` (the owner's library, measured 2026-11-16: 15,694 of 15,740
MP3 files are ID3v2.4): ID3v2.3 has no frame for the mood, the disc
subtitle, the release date and others, and holds an original date as a
year only. Turning an ID3v2.4 tag into ID3v2.3 drops them, and the save
says nothing. Under `Keep` no save changes a tag's version, so no save
loses a value the user did not name. MusicBee's own default is ID3v2.3
(W-TP); this is a difference, and it is meant.

The version is found before anything is changed: it is the version of
the tag in the file, not of what the tag library makes of it in memory.

**Not kept.** When the tag written is ID3v2.3 (the file's own version
under `Keep`, or `V23`), a value with no place in ID3v2.3 is not dropped
in silence. After the new version of the file is written and before it
replaces the original, it is read once, and what it holds is compared
with what the save meant it to hold: the file's tags with the changes
applied. `WriteResult::notKept` has one line for each catalogue field,
then each other tag, that differs:

- `NAME` when the value is not there at all;
- `NAME: MEANT became IS` when it is there but different; several
  values are joined with `; `.

The lines are sorted (`QStringList::sort`). The save is still `Ok` and
the file is still replaced: the user asked for this version. Nothing is
moved into a `TXXX` frame (the owner's ruling, 2026-11-16). `notKept` is
empty for every other status, for an ID3v2.4 tag, and for the other
formats; no extra read is made for those.

`linuxbeectl tags write` prints `saved` as before, exit 0, and one line
on standard error for each: `not kept in ID3v2.3: ` and the line.

On the command line (W-CLI): `--id3v2.3` and `--id3v2.4` choose `V23` and
`V24`; with neither the save is `Keep`. Both together is a usage error
(exit 2) and nothing is written.

### W-ID3V1

MP3 only. With `writeId3v1` true (MusicBee's default, W-TP), the file ends
with an ID3v1 tag holding what ID3v1 can hold of the current values; one is
added if the file had none. With it false, an existing ID3v1 tag is
removed. An APEv2 tag in an MP3 is left as it is.

### W-WAV

A WAV file's tags are saved in its ID3 chunk. No RIFF Info chunk is created.

### W-MTIME

With `keepModificationTime` true, the file's modification time after a
save is what it was before. The default is false (W-TP).

## 6. Command line

### W-CLI

```
linuxbeectl tags write [--set NAME=VALUE]... [--clear NAME]...
                       [--set-other KEY=VALUE]... [--clear-other KEY]...
                       [--picture IMAGE | --no-pictures]
                       [--id3v2.3 | --id3v2.4] [--no-id3v1] [--keep-mtime] FILE
```

- `--set` given several times for one NAME gives that field several values.
- `--clear NAME` removes the field.
- One field cannot be both set and cleared in one command: naming the
  same NAME in a `--set` and a `--clear`, in either order and whatever
  the case of the letters, is a usage mistake. The same holds for one KEY
  in `--set-other` and `--clear-other`. The message names the field, and
  the file is not touched.
- `--picture IMAGE` makes the image file the only picture, a front cover.
  An IMAGE that cannot be read: `not found: IMAGE`, exit 3. One that
  `imageMimeType` does not know is a usage mistake.
- `--no-pictures` removes every picture. Giving both picture options is a
  usage mistake.
- `Ok` prints `saved`; `Unchanged` prints `unchanged`; both exit 0.
- `InvalidChange`: the problem lines on standard error, exit 2.
- `NotFound`: `not found: FILE`. `NotAudio`: `not a music file: FILE`.
  `NotWritable`: `not writable: FILE`. `Failed`: `could not save: FILE`.
  All on standard error, exit 3.
- A usage mistake: a message on standard error, exit 2.

`linuxbeectl tags read` keeps working as before.

## Not in this version

- A save that cannot leave a damaged file behind if the machine loses power
  mid-write. TagLib rewrites the file in place. The organiser and the
  converter work on copies; the tag editor will need a decision here.
- Choosing the text encoding of ID3 frames (MusicBee's "ID3v2.3, ASCII
  only").
- Writing APEv2 tags into MP3 files; synchronised lyrics; custom tags;
  the artist roles; album rating, Love and the other MusicBee-only tags.
- Undo.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| W-RATING | The byte for each star rating, and the address text MusicBee puts in a new `POPM` frame. linuxBee: the R-RATING anchors and an empty address. |
| W-RATING | The number MusicBee writes in a Vorbis `RATING` comment for 3.5 stars. linuxBee: 70. |
| W-TABLE | Vorbis totals: TRACKTOTAL and DISCTOTAL, or TOTALTRACKS and TOTALDISCS? The public sources give both. linuxBee writes the first pair and reads both. |
| W-MULTI | How MusicBee stores two artists in an ID3v2.3 frame. linuxBee: as TagLib does. |
| W-NUMBERS | Is a disc count with no disc number written at all? linuxBee: as `/4`. |
| W-ID3V1 | With "also save ID3v1 tags" off, does MusicBee remove an existing ID3v1 tag? linuxBee: yes. |
| W-ID3VER | Does saving an ID3v2.4 file with the default setting turn it into ID3v2.3? linuxBee: no; a save keeps the file's version (W-ID3VER). |
