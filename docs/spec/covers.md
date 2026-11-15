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
| W-FAQ | MusicBee wiki, "FAQ": under "set which pictures are displayed" the sources are a list in an order, each of which can be unticked ("move 'embedded' to the top and untick all other options"): https://musicbee.fandom.com/wiki/FAQ |

## CV-EMBEDDED

`chooseEmbedded(pictures)` answers which of a file's pictures is its
cover, by position: the first whose type is `Front Cover` (case
ignored); when none is, the first picture; -1 when there are none.

## CV-FILE

`coverFileIn(folder)` answers the path of the folder's cover file, or
nothing.

- Only files directly in the folder count: not folders, not what lies
  in a folder below, not a file whose name begins with a dot, not a name
  that leads (through a symbolic link) to anything but a regular file,
  such as a device or a FIFO.
- A picture file is one whose extension is `jpg`, `jpeg` or `png`, case
  ignored.
- The names looked for, case ignored, in this order (`coverNames()`):
  `cover`, `folder`, `front`, `album`, `albumart`. The first of these
  names that a picture file has wins; when one name has several
  extensions, `jpg` before `jpeg` before `png`; when two files differ
  only in case, the one that sorts first by code point.
- When no picture file has one of the names and the folder holds
  exactly one picture file, that one is the cover. With two or more,
  there is none: linuxBee does not guess.
- A folder that is not there has no cover.

## CV-FIND

`findCover(musicFile, options)` answers a `Cover`.

`options.order` lists the sources to try, in order; by default
`Embedded`, then `File` (W-FAQ: the order is the user's). A source not
in the list is not tried; an empty list finds nothing.

- `Embedded`: the file is read (tag-reading.md) and CV-EMBEDDED chooses.
  The answer has `source` `Embedded`, `file` the music file, and the
  picture's `mimeType`, `type` and `byteCount`. A file that is not
  there, or is not audio, has no embedded cover.
- `File`: CV-FILE on the folder the music file is in (whether or not the
  music file itself is there). The answer has `source` `File`, `file`
  the picture's path, `mimeType` `image/jpeg` for `jpg` and `jpeg` and
  `image/png` for `png`, an empty `type`, and `byteCount` the file's
  size.
- `data` holds the picture's bytes when `options.data` is true (the
  default); otherwise it is empty and `byteCount` still says how many
  there are.
- When nothing is found: `source` `None` and everything else empty or 0.

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
only one. `--file-first` tries the folder's file before the embedded
picture.

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
| CV-FIND | In which order MusicBee tries its picture sources on a new install, and which are ticked. linuxBee: embedded, then the folder's file. |
| CV-FILE | Which file names MusicBee accepts beside `folder.jpg` and its own `Cover.jpg`. linuxBee: cover, folder, front, album, albumart. |
| CV-FILE | Whether MusicBee uses a lone picture with another name. linuxBee: yes, when it is the only one. |
