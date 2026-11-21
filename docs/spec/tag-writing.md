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
| W-ANS | MusicBee wiki, the pages that answer the questions under "Answered from the MusicBee wiki": "Tags Preferences": https://musicbee.fandom.com/wiki/Tags_Preferences |

Pages were read on 2026-11-13.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Tagging" (3.0.5962; 2020-10-11);
"Tags Preferences" (3.0.5976; 2020-04-26).

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
- the field is Love and the list is not a single `L` or `B`, in either
  case, white space about it allowed (what is written is the letter
  alone, in upper case);
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

A Speex file (`.spx`) carries Vorbis comments as Ogg Vorbis and Opus do,
and is written with their keys (the owner's ruling of 2026-11-20, N8,
names Speex). A scan does not take `.spx` files into the library
(tag-reading.md); the rule reaches them where tags are written alone.

| Field | ID3v2 (MP3, WAV) | Vorbis comment (FLAC, Ogg, Opus, Speex) | MPEG-4, APEv2 (WavPack) |
|---|---|---|---|
| Year | DATE | DATE | DATE |
| Track#, Track Count | TRACKNUMBER, as `n/total` | TRACKNUMBER and TRACKTOTAL | TRACKNUMBER, as `n/total` |
| Disc#, Disc Count | DISCNUMBER, as `n/total` | DISCNUMBER and TOTALDISCS (W-TG; W-DISCTOTAL) | DISCNUMBER, as `n/total` |
| Publisher | LABEL (`TPUB`) | ORGANIZATION (W-TG) | LABEL |
| Grouping | WORK (`TIT1`, W-TG) | CONTENTGROUP (W-TG) | GROUPING |
| Lyrics | LYRICS (`USLT`) | UNSYNCEDLYRICS (F-V) | LYRICS |
| Original Year | ORIGINALDATE | ORIGYEAR (F-V) | ORIGINALDATE |
| Original Artist | ORIGINALARTIST | ORIGARTIST (F-V) | ORIGINALARTIST |
| Original Album | ORIGINALALBUM | ORIGALBUM (F-V) | ORIGINALALBUM |
| Rating | the `POPM` frame (W-RATING) | RATING | RATING |
| Love | LOVE RATING (a `TXXX` frame) | LOVE RATING | LOVERATING |
| every other field | the first key in its R-CATALOGUE row | the same | the same |

### W-ONEHOME

When a field is written or removed, every key its R-CATALOGUE row reads
from is removed first (and WORK, for Grouping in an ID3v2 tag). A field
never ends up stored under two spellings, with the one exception (W-DISCTOTAL)
of a Vorbis DISCTOTAL.

### W-NUMBERS

A number and its count travel together: changing either rewrites both from
the values the file will have afterwards. In the `n/total` formats a
missing count gives a bare `n`, and a count with no number gives `/total`.
In a Vorbis comment a stored `2/8` is split into TRACKNUMBER `2` and
TRACKTOTAL `8` the first time either is written.

### W-DISCTOTAL

linuxBee's own (the orchestrator's word of 2026-11-21, on the owner's
ruling): other players read DISCTOTAL, and the owner's files carry it. So
when Disc# or Disc Count is written to a Vorbis comment (W-NUMBERS), the
count goes to TOTALDISCS, and a DISCTOTAL the file already has is given the
same value: the two never disagree. DISCTOTAL is never made where there was
none. When the count is removed, both go, since a DISCTOTAL left behind
would be read back as the count (R-CATALOGUE reads DISCTOTAL first). A save
that names neither field leaves both as they are. This is the one exception
to W-ONEHOME.

### W-MULTI

Several values are stored as the format stores a list: one comment per
value in a Vorbis comment, one frame with several strings in ID3v2, as
TagLib does it. `readTags` returns them as separate values again.

### W-RATING

- ID3v2: the rating byte of the `POPM` frame, from `popmFromRating`:

  | Stars | 0.5 | 1 | 1.5 | 2 | 2.5 | 3 | 3.5 | 4 | 4.5 | 5 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | Byte | 13 | 1 | 54 | 64 | 118 | 128 | 186 | 196 | 242 | 255 |

  **One byte of one frame.** (Ruled 2026-11-18. Until then every
  `POPM` frame was taken out and one written: another player's frame
  was lost, and taking a rating out lost the play counter.)

  - The rating is the byte of the first `POPM` frame, the one it is
    read from (R-RATING). Setting it changes that byte and nothing
    else of that frame: its address and its counter stay. This holds
    also when the first frame's byte is 0.
  - Every other `POPM` frame stays: its address, byte and counter,
    and its place among them.
  - A file with no `POPM` frame gets one, with an empty address and a
    counter of 0.
  - Taking the rating out sets the first frame's byte to 0 and keeps
    the frame when it has a counter above 0 or is not the only `POPM`
    frame. (Were the first of two taken out, the second would be
    read as the rating.) A lone frame with a counter of 0 is removed.
  - Where the file reads as not rated, taking the rating out is
    `Unchanged` (W-UNCHANGED), frames or none.
  - Remove Tags with "All tags" does not come this way: it hands the
    frames whole, none of them (tagging-tools.md, G-REMOVE).

  Known and left: the tag library writes a frame's two flag bytes as
  zeros, for every frame it writes, these among them.

  **The frames whole.** A change can carry the `POPM` frames
  themselves: the address, rating byte and counter of each, in order.
  The tag's `POPM` frames then become exactly these (none, for an
  empty list), whatever the change's Rating entry says. Such a change
  is `Unchanged` (W-UNCHANGED) only when the file's frames are these
  already. It is what undo uses (tag-undo.md, UN-RATING); for a file
  with no ID3v2 tag family it means nothing and is left out.
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
  exactly as before. In an ID3v2 tag they are more than read back
  the same: W-ONLY.
- Tags the reader lists as unsupported are kept when TagLib keeps them.

### W-ONLY

A save changes what it names and nothing else. This rule is for an ID3v2
tag (MP3, WAV). The locked test `tests/locked/tst_tagwrite_only_id3.cpp`
holds it: it reads the frames of the tag itself, byte by byte, and asks
the tag library nothing.

- **What a change names.** A key is named when the change gives it other
  values than the file has: the key a field is written under and every
  key it is read from (W-ONEHOME), the key of a number and its count
  (W-NUMBERS), the key of an entry of `other`. Rating names the `POPM`
  frames (W-RATING). `pictures` names the `APIC` frames (W-PICTURES).
- **The frames of a key** are the frames the tag library reads that key
  from. `TPUB` and a `TXXX` frame described `Label` are both frames of
  LABEL. A `COMM` frame with no description is a frame of COMMENT,
  whatever its language; one with a description is not.
- **Named:** the frames of the key are taken out, and the new values are
  written as the tag library writes that key.
- **Not named:** the frame is, after the save, what a save that changes
  nothing leaves of it. In a tag the tag library has written before,
  that is the same bytes, and the same order among the frames that are
  not named. (A tag written by another program may be written anew at
  its first save here, whatever is saved: the tag library writes a text
  without the zero another program ends it with, and it writes the
  frames of ID3v2.3 in an order of its own; it drops a lyrics frame
  that has no text, and gives a rating frame with no counter a counter
  of 0. In an ID3v2.3 tag it writes text another program stored as
  UTF-8 as UTF-16, and drops a date frame of ID3v2.4 (`TDRC`, `TDOR`)
  that stands beside the ID3v2.3 frame of the same date. An ID3v2.2
  tag becomes ID3v2.4 (W-ID3VER), its pictures' kind written out
  (`JPG` becomes `image/jpeg`). That happens once, and no value is
  changed by it. One thing takes a save more: in ID3v2.3 the
  tag library keeps the lists of people in one frame and sorts its
  pairs as it reads them, so the save after a tag was first written as
  ID3v2.3 may put the pairs of that frame in another order. No pair is
  lost.)
- **Comments and lyrics** are written as comment frames (`COMM`) and
  lyrics frames (`USLT`), never as a `TXXX` frame. ID3v2 allows one such
  frame for a language and a description, and a tagger that meets two
  alike keeps one. So a value gets a frame of its own only where it
  takes the place of a frame that was there: the frame for value n has
  the language and the description of the n-th frame replaced, frames
  that were alike in both counting once. The values beyond those are
  added to the last frame written, each on a line of its own. With no
  frame to replace, all the values are one frame, as the tag library
  makes it, a line each. (Read again, such a field has fewer values
  than were given, and its last value has several lines.)
- **Lists of people.** `TIPL` and `TMCL` hold many keys in one frame, as
  pairs of a role and a name: ARRANGER, ENGINEER, PRODUCER, DJMIXER,
  MIXER, and `PERFORMER:` with an instrument. (In ID3v2.3 the tag
  library keeps both lists in one `IPLS` frame.) Naming one of these
  keys changes the pairs of that role, compared ignoring case. Every
  other pair stays as it is and in its order, a role the tag library
  does not know among them. A list left with no pair is taken out.
  Where the tag library also reads such a key from another frame (some
  taggers write the arranger or the producer as a `TXXX` frame), that
  frame is a frame of the key as any other: naming the key takes it
  out, and the value is written into the list.
- **A file read from another tag.** When the values the file was read as
  having are not in its ID3v2 tag (an MP3 with no ID3v2 tag is read
  from its APEv2 tag, or else from its ID3v1 tag), the ID3v2 tag that
  is written holds all of them, not only the named ones: once the file
  has an ID3v2 tag it is read from that, and a value left behind in
  the other tag would no longer be shown. Every key of the values as
  read that no frame of the ID3v2 tag holds is written, whichever tag
  it came from.
- **What may change without being named:** the padding and the size of
  the tag; the ID3v1 tag, which is written from the current values
  (W-ID3V1); and what W-ID3VER says of a tag written in another version
  than it had.

**In an MPEG-4 file** the same holds of its items. The locked test
`tests/locked/tst_tagwrite_only_mp4.cpp` holds it; it too reads the
items itself and asks the tag library nothing.

- **The items of a key** are the items the tag library reads that key
  from. A freeform item in Apple's name space (`com.apple.iTunes`) is an
  item of the key that is its name in capitals: an item named
  `replaygain_track_gain` is an item of REPLAYGAIN_TRACK_GAIN, and so
  is one named `REPLAYGAIN_TRACK_GAIN` beside it.
- **Named:** every item of the key is taken out, and one item is
  written for the new values. It is written under the name the tag
  library gives that key; but where the file already had the key under
  that name in other letters (small, or mixed), under the file's own
  spelling. A key that is taken out leaves no item of it.
- **Not named:** the item is, after the save, what a save that changes
  nothing leaves of it. In a file the tag library has written before,
  that is the same bytes in the same order. (At its first save of a
  file another program wrote, whatever is saved, the tag library puts
  the items in the order of their names, drops an item that holds no
  data, and writes some numbers in more bytes than they had; `cnID` was
  seen. That happens once and changes no value. One thing it does then
  is a loss: see "Not in this version".)
- An item the tag library reads no key from (one in another name space;
  `rtng`, a shop's advisory mark) is never named by a key. `pictures`
  names the item `covr`; an empty list leaves no such item, not an
  empty one.
- A value the tag library cannot write for a key leaves the items of
  that key as they are.

**In a file of Vorbis comments** (FLAC, Ogg Vorbis, Opus) and **in a
WavPack file** (APEv2 items) the same holds of its comments and items.
The locked test `tests/locked/tst_tagwrite_only_vorbis_ape.cpp` holds
it; it reads them itself and asks the tag library nothing.

- **Vorbis comments.** The comments of a key are those of its name,
  whatever the case of its letters. Named: they are taken out, and one
  comment is written for each value, its name in capitals. Not named:
  the same bytes in the same order, in a file the tag library has
  written before. (At its first save of a file another program wrote,
  whatever is saved, the tag library writes every name in capitals,
  puts the comments in the order of their names, drops a comment with
  an empty value, and writes an old `COVERART` comment as a
  `METADATA_BLOCK_PICTURE` one. That happens once.)
- **APEv2 items.** The item of a key is the one of its name, whatever
  the case of its letters. Named: it is written anew **under the name
  the file gave it** (`Title` stays `Title`; a gain written as
  `replaygain_track_gain` keeps that name), several values with a zero
  byte between them; a key the file did not have is written in
  capitals, and so is one that was taken out and is put back (Undo of
  taking a key out brings the value back, its name in capitals). A key
  that is taken out leaves no item. Not named: the
  same bytes in the same order; that holds of an item that is not
  text too (a binary item, a link). `pictures` names the item
  `Cover Art (Front)`, which keeps that name. (At its first save of a
  file, the tag library puts the items in the order of their names and
  drops an item with an empty value.)
- The pictures of a FLAC file are blocks of their own beside the
  comments: a save that does not name the pictures leaves every block
  but the comments and the padding the bytes it was.

Known, and left:

- Where a new frame stands among the others is the tag library's choice.
- Undo gives a field its old values through this same save. The frames
  are then the bytes they were, except that the language of a comment
  or of lyrics that a change took out does not come back with its text:
  after two comments were made one and then two again, both have the
  language of the first.
- A list of people with a role the tag library does not know is not
  read at all: `readTags` shows none of its people (that is the reader's
  to mend). A name saved into such a list is in the file and is not
  shown, and saving it again writes it again.
- Undo of a field the file held in two frames (a publisher in `TPUB`
  and in a `TXXX` frame) brings the values back in one frame.
- When a tag has two lists of people of one kind, the first is the one
  changed.
- Not held by a test: the order of the pairs when one role has several;
  that frames alike in language and description count once.

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

A value the save meant to be empty (a field the change removes, or one
the file never had) is never named: only a value the file was meant to
hold can be "not kept".

NAME is the catalogue field's name, or the other tag's key; four keys
are written as a person would:

| Key | Written |
|---|---|
| `DISCSUBTITLE` | `Disc Subtitle` |
| `RELEASEDATE` | `Release Date` |
| `TAGGINGDATE` | `Tagging Date` |
| `PRODUCEDNOTICE` | `Produced Notice` |

The lines are sorted as written (`QStringList::sort`). The save is still `Ok` and
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
removed. An APEv2 tag in an MP3 is left as it is: no save writes into
it, a save of pictures included.

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
  only", W-TP).
- Writing APEv2 tags into MP3 files; synchronised lyrics; custom tags;
  the artist roles; album rating and the other MusicBee-only tags. (Love
  is written: see W-TABLE.)
- Undo here: tag-undo.md has one step of it, for the window.
- Keeping an MPEG-4 freeform item the tag library cannot hold. An item
  marked as text is read as text, and text ends at a zero byte: the
  item `Encoding Params`, which iTunes fills with bytes that are not
  text, is cut to its first four bytes at the first save of such a
  file, whatever is saved. Any tagger built on this tag library does
  the same.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| W-RATING | The byte for each star rating, and the address text MusicBee puts in a new `POPM` frame. linuxBee: the R-RATING anchors and an empty address. |
| W-RATING | The number MusicBee writes in a Vorbis `RATING` comment for 3.5 stars. linuxBee: 70. |
| W-TABLE | Vorbis totals. "Tagging" (W-TG) gives Total Tracks as "TOTALTRACKS or TRACKTOTAL", and Total Discs is TOTALDISCS only. linuxBee writes TRACKTOTAL and TOTALDISCS (the owner's ruling of 2026-11-20), keeps a DISCTOTAL the file has (W-DISCTOTAL), and reads both spellings of each. |
| W-MULTI | How MusicBee stores two artists in an ID3v2.3 frame. linuxBee: as TagLib does. |
| W-NUMBERS | Is a disc count with no disc number written at all? linuxBee: as `/4`. |
| W-ID3VER | Does saving an ID3v2.4 file with the default setting turn it into ID3v2.3? linuxBee: no; a save keeps the file's version (W-ID3VER). |

### Answered from the MusicBee wiki (checked 2026-11-20)

| Rule | Question | What the wiki says |
|---|---|---|
| W-ID3V1 | With "also save ID3v1 tags" off, does MusicBee remove an existing ID3v1 tag? linuxBee: yes. | Yes: "Tags Preferences" says existing ID3v1 tags "are cleared as soon as tags are edited". W-ID3V1 already does so. |
