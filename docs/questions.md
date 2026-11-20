# Questions about MusicBee: how they are kept

linuxBee follows what MusicBee's public pages document. Where they say
nothing, a spec makes a rule of its own and lists it as an open
question. This file says where those questions are and how one is
closed.

## Where they are

Each spec in `docs/spec/` ends with a table "Open parity questions" (or
"Open questions"). A row names the rule, asks the question, and says
what linuxBee does meanwhile.

## How one is closed

- **From a public page.** The row moves to a table "Answered from the
  MusicBee wiki" in the same spec, with the date it was checked and what
  the page says; the page is added to Sources. When the answer means
  linuxBee should behave otherwise, the change is its own package, with
  a test, and the spec's rule changes with it.
- **By the owner's ruling.** The row moves to a table "Decided by the
  owner", or the rule says "the owner's ruling" and the date; a ruling
  that settles more than one rule has a file in `docs/decisions/`.
- **By someone who uses MusicBee.** What they saw is written as an
  observation with its date (`CLEAN-ROOM.md`, the sixth kind of
  source), and the row is closed as above.

## What has been closed

| Date | How | Where |
|---|---|---|
| 2026-11-14 | the owner's rulings where no page says | `docs/decisions/where-the-wiki-is-silent.md` |
| 2026-11-16 | the owner's rulings on deleting, undo, ReplayGain and ID3v2.3 | `docs/decisions/` |
| 2026-11-20 | 28 questions answered from the wiki, whole or in part | the "Answered from the MusicBee wiki" tables of 21 specs |
| 2026-11-20 | differences kept, and behaviour to follow | `docs/decisions/kept-differences-from-musicbee.md`, `docs/decisions/follow-the-documented-behaviour.md` |
