# How linuxBee stays a clean-room project

linuxBee aims to feel familiar to MusicBee users. MusicBee is closed-source
freeware, and its licence forbids decompiling, disassembling and reverse
engineering the program. linuxBee is written without doing any of those things.

## Where behaviour may be learned from

1. Public documentation: the MusicBee wiki, the official help and API pages,
   and public forum posts in which features are described.
2. The published plugin interface file that MusicBee's author distributes
   for plugin writers, read as documentation of the plugin contract.
3. Skin files published by their authors as plain XML, read as data.
4. A user's own data that MusicBee exported in a documented format (for
   example the iTunes-format XML library export, or M3U playlists).
5. Open standards and their documentation: ID3, Vorbis comments, MP4 atoms,
   APE tags, ReplayGain, MPRIS, the XDG specifications.
6. What a user can see by using the program normally, described in words by
   a person. Only with the project owner's say-so, and recorded in the spec
   as an observation with a date.

## What is never used

- Decompiled, disassembled or leaked MusicBee code, or notes derived from it.
- Third-party code that was itself produced that way.
- MusicBee's name as this project's name, its icons, artwork, or bundled
  skins.
- Wiki or forum text copied into this tree. Specs are written in our own
  words, with a link to the page that was read. A short quoted phrase is
  marked as a quote.
- Undocumented binary formats (the library database, compiled skins) unless
  the project owner has decided, in writing in `docs/decisions/`, that a
  particular reader is in scope and on what basis.

## How a spec records its sources

Every behaviour in `docs/spec/` carries one of two marks:

- a source tag pointing at a public page, or
- a rule id for a decision linuxBee made because the public sources are silent.

A linuxBee decision is a guess about what users expect. It is listed in the
spec's "open parity questions" so it can be corrected when someone who knows
the original behaviour reports a difference.

## If you are unsure

Stop and ask the project owner. Nothing in this project is urgent enough to
justify a source that is not on the first list.
