# Covers

Which picture stands for a track: the one inside the music file, or a
picture file lying beside it. Core only; it reads files and writes none
(the command's `extract` aside). Showing covers in the window is a later
plan and uses these functions.

Code: `src/core/covers/cover.h`. Command: `linuxbeectl cover`.

## Sources

| Id | Source |
|---|---|
| W-ART | MusicBee wiki, "Album Art and Artist Pictures": a picture is embedded in the music file, or saved beside it as `folder.jpg`, or linked as an organised copy named `Cover.jpg` by default: https://musicbee.fandom.com/wiki/Album_Art_and_Artist_Pictures |
| W-TP | MusicBee wiki, "Tags Preferences", "set which pictures are displayed": MusicBee "will try each option in succession until it finds something"; the default order is Cover.\*, Artwork.\*, Front.\*, <Filename>.\*, Embedded, and Folder.jpg is an option, off by default: https://musicbee.fandom.com/wiki/Tags_Preferences |
| W-FAQ | MusicBee wiki, "FAQ": under "set which pictures are displayed" the sources are a list in an order, each of which can be unticked ("move 'embedded' to the top and untick all other options"): https://musicbee.fandom.com/wiki/FAQ |

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Album Art and Artist Pictures" (no
version given; 2019-03-03); "FAQ" (no version given; 2023-05-07).

## CV-EMBEDDED

`chooseEmbedded(pictures)` answers which of a file's pictures is its
cover, by position: the first whose type is `Front Cover` (case
ignored); when none is, the first picture; -1 when there are none.
A picture of no bytes is passed over as if it were not there.

## CV-FILE

`coverFileIn(folder)` answers the path of the folder's cover file, or
nothing.

- Only files directly in the folder count: not folders, not what lies
  in a folder below, not a file whose name begins with a dot, not a name
  that leads (through a symbolic link) to anything but a regular file,
  such as a device or a FIFO.
- A picture file is one whose extension is `jpg`, `jpeg` or `png`, case
  ignored.
- A file of no bytes, a file of more than 32 MiB (33,554,432 bytes) and
  a file this user cannot read are not picture files: not by name, and
  not as the lone picture. So the bytes of a cover file can always be
  asked for, and a cover is the same whether or not they are.
- A symbolic link to a regular file is followed, wherever that file
  lies; the cover is named by the link's path in the folder, and the
  limits above are those of the file it leads to.
- The names looked for, case ignored, in this order (`coverNames()`):
  `cover`, `artwork`, `front`, `folder`, `album`, `albumart`. The first
  three are MusicBee's (W-TP); the last three are linuxBee's own
  (CV-FIND). The first of these
  names that a picture file has wins; when one name has several
  extensions, `jpg` before `jpeg` before `png`; when two files differ
  only in case, the one that sorts first by code point.
- When no picture file has one of the names and the folder holds
  exactly one picture file, that one is the cover. With two or more,
  there is none: linuxBee does not guess.
- A folder that is not there has no cover.

## CV-FIND

`findCover(musicFile, options)` answers a `Cover`.

`options.order` lists the sources to try, in order; by default `File`,
then `Embedded` (W-TP; W-FAQ: the order is the user's). A source not in
the list is not tried; an empty list finds nothing. So by default the
order is MusicBee's: `cover`, `artwork`, `front`, the music file's own
name (its name without the extension, case ignored), the embedded
picture; and only then linuxBee's own: `folder`, `album`, `albumart`, the
lone picture. The last four are departures, kept so that a folder that
has a cover in linuxBee today keeps it (the owner's ruling of
2026-11-20 names `folder.jpg`; the orchestrator's of 2026-11-21 keeps
the other three). They come after every source in the order
(`options.fallbackLast`, true by default); with it false, they are tried
within the `File` step, after MusicBee's names.

- `Embedded`: the file is read (tag-reading.md) and CV-EMBEDDED chooses.
  The answer has `source` `Embedded`, `file` the music file, and the
  picture's `mimeType`, `type` and `byteCount`. A file that is not
  there, or is not audio, has no embedded cover.
- `File`: the picture files of the folder the music file is in (whether
  or not the music file itself is there), by the rules of CV-FILE:
  MusicBee's names, then the music file's own name. `coverFileIn(folder)`
  knows no music file, so it gives the folder's names in their own order
  and then the lone picture. The answer has `source` `File`, `file`
  the picture's path, `mimeType` `image/jpeg` for `jpg` and `jpeg` and
  `image/png` for `png`, an empty `type`, and `byteCount` the file's
  size.
- `data` holds the picture's bytes when `options.data` is true (the
  default); otherwise it is empty and `byteCount` still says how many
  there are.
- When nothing is found: `source` `None` and everything else empty or 0.

## CC-ASK: the cover cache

`CoverCache(edge, hooks)` in `src/ui/covers/covercache.h` gives the
window covers that are small and ready to draw. It finds no cover
itself: that is CV-FIND, with the default options. `edge` is 64 unless
given; below 1 counts as 1.

`cover(musicFile, stamp)` answers from what is kept and never reads a
file. `stamp` is the caller's word for the state of the file (a library
track's modification time will do); 0 unless given, and 0 is a stamp
like any other.

- Kept under that stamp: the picture, or a null image when the file has
  no cover. That is a use (CC-KEEP).
- Not kept, or kept under another stamp: a null image, and a load is
  asked for, unless one for that file and stamp is under way or waiting
  already. What is kept under another stamp is not handed out.
- `has(musicFile, stamp)` says whether it is kept under that stamp, with
  or without a picture. It is a look, not a use.

When a load has ended the answer is kept, in place of anything kept for
that file before, and `loaded(musicFile)` is emitted: also when the file
has no cover, or what was found is not a picture Qt can read. Both are
kept as "no cover" and are not looked for again under that stamp.

At most `waitingLimit()` requests wait (64 unless set; below 1 counts as
1). One more drops the request that has waited longest: it is forgotten,
nothing is emitted for it, and asking again asks anew. So a view that is
scrolled fast through thousands of albums loads the ones it stopped at.

`loads()` counts the loads begun since the cache was made, for tests and
for a person timing the program; nothing decides anything by it.
`waiting()` is the number of requests that have not begun.

## CC-KEEP

A picture is kept scaled down, smoothly and with its shape kept, to fit
a square of `edge` pixels; one that fits already is kept as it is.

At most `limit()` entries are kept (512 unless set; below 1 counts as
1), counting those without a picture. When there is one too many, the
one used longest ago goes; being kept at the end of its load is its
first use. `setLimit` lets go at once of what is over. `size()` is the
number kept.

## CC-THREAD

The cache lives on the thread it was made on. Files are looked at on
one other thread, the cache's own, one load at a time, in the order
asked: no call of the cache reads a file. `hooks.loading(musicFile)`,
when set, is called on that thread before the file is looked at; it is
there for tests. `loaded` is emitted on the cache's thread.

Destroying the cache drops the requests that wait, waits for the load
under way to end, and emits nothing more.

## CV-CLI

`linuxbeectl cover show FILE [--file-first]` prints what `findCover`
answers for FILE, one `name=value` line each, and exits 0:

```
source=embedded
file=/music/a/one.mp3
mime=image/png
type=Front Cover
bytes=69
```

`source` is `embedded`, `file` or `none`; with `none` that line is the
only one. Without options the order is CV-FIND's default. `--file-first`
tries every picture file before the embedded picture, linuxBee's own
names too (`fallbackLast` false).

`linuxbeectl cover extract FILE OUT [--file-first]` writes the cover's
bytes to OUT and prints `wrote N bytes to OUT`. It exits 3, writing
nothing, with a line on standard error beginning `failed: `, when there
is no cover (`no cover`), when OUT exists already (`exists`), or when
OUT cannot be written.
A symbolic link at OUT exists, even one that leads nowhere. OUT is
created by the command or not at all, and when writing fails part-way
the command removes what it made.

A missing subcommand, an unknown one, an unknown option or the wrong
number of files is a usage mistake: exit 2, nothing on standard output.

## Not in this version

- Downloading pictures; artist pictures; several pictures per track
  shown in turn (W-ART).
- Writing a picture into a music file or beside it (the tag editor
  writes embedded pictures already: tag-editor.md).
- Pictures in other formats (`gif`, `bmp`, `webp`).
- A cache of scaled pictures for the window.

## Open parity questions

| Rule | Question |
|---|---|
| CV-FIND | Answered (W-TP): Cover, Artwork, Front, <Filename>, Embedded; Folder.jpg off. linuxBee adds folder, album, albumart and the lone picture after them. |
| CV-FILE | MusicBee's `*.*` option (any picture) is not offered; the lone picture is linuxBee's narrower form of it. |
| CV-FILE | Whether MusicBee has a largest size for a picture file. linuxBee: 32 MiB, its own number. |
| CV-FILE | Whether MusicBee uses a lone picture with another name. linuxBee: yes, when it is the only one. |
