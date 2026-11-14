# Browsing by artist, or by another tag

A list of artists beside the track list. Choosing one narrows the track
list to that artist's tracks; "All Artists" shows everything again. The
list can be switched to another tag (genre, year, composer and so on),
and then works the same way with that tag's values.

This document is the contract for `src/core/library/artistindex.h`,
`src/ui/browser/artistbrowser.h`, `src/ui/shell/artistbrowsing.h`, and
for what `src/app/main.cpp` does with them. The locked tests
`tests/locked/tst_ui_artistbrowser.cpp` and `tests/locked/cli_parts.sh`
check it.

## Sources

| Tag | Source |
|---|---|
| W-MP | MusicBee wiki, "Main Panel" (the artist column beside the tracks in the Artists and Album Artists views): see track-list.md |
| W-LNE | MusicBee wiki, "Library Navigation Elements", Thumbnail Browser: a column of one tag's values at the left of the library that filters the main panel by the value chosen, with a menu to select which field it uses: https://musicbee.fandom.com/wiki/Library_Navigation_Elements (matches version 3.0; read 2026-11-13) |
| SHOT | The user's own MusicBee window, 2026-11-13: the column is headed `Album Artist`, with a menu arrow beside the heading. |
| AB-… | A linuxBee decision. |

## AB-ARTIST: the artist a track is filed under

The first value of Album Artist that is not blank; without one, the
first value of Artist that is not blank; without one, the empty text.
The value is trimmed. (A compilation is filed under its album artist,
not under each performer.)

## AB-SAME

Two names are the same artist when they are equal once trimmed and with
the case of letters ignored. Accents are not ignored: `Bjork` and
`Björk` are two.

## AB-FIELD: browsing by another tag

`browseFields()` is, in this order: `Album Artist`, `Artist`, `Album`,
`Composer`, `Genre`, `Grouping`, `Year`.

`browseValues(track, field)` is what the track is filed under:

- `Album Artist`: one value, the artist of AB-ARTIST.
- `Year`: one value, from the first value of Year, trimmed: its first
  four characters when those are four digits (`1969-09-26` is `1969`),
  and otherwise the whole of it (`c. 1970`).
- Any other field offered: every value of the field that is not blank,
  trimmed, in the order the track has them; a value that is the same
  (AB-SAME) as an earlier one is left out. A track with two genres is
  filed under both.
- A field that is not offered: as `Album Artist`.

It is never an empty list: a track with nothing to be filed under gives
one empty text.

## AB-INDEX

`artistIndex(tracks, options, field)` gives one entry per value of the
field (AB-SAME); `field` is `Album Artist` when not given. A track counts
under each of its `browseValues`.

- `name`: the spelling of the first track met, in the order given;
- `tracks`: how many tracks are filed under it;
- `albums`: how many different Album values those tracks have, trimmed
  and with case ignored; tracks with no album count as one more album
  between them.

The entries are in the order `compareFieldValues(field, a, b, options)`
gives, `field` being `Album Artist` when it is not one offered (track-list.md, T-COMPARE: so `The Beatles` sorts under
B with the default options), ties by the name's code points. The entry
for "no artist" (the empty name) comes last.

## AB-LIST: the browser

Widgets, by object name:

| Name | Widget | |
|---|---|---|
| `artistFilterEdit` | line edit with a clear button | AB-FILTER |
| `artistList` | list | the rows below |

`setTracks(tracks)` fills the list from `artistIndex(tracks)`:

- the first row is `All Artists (N)`, N the number of artists;
- then one row per entry, `NAME (T)`, T its number of tracks; the entry
  with no name reads `[Unknown Artist] (T)`.

`artists()` is every name held, in that order, whatever the filter
hides.

## AB-CHOICE

There is either no choice ("all artists": the first row is the current
one) or one artist chosen (its row is the current one).

- Making a row the current one, by hand or by `chooseArtist` and
  `chooseAll`, makes that the choice. `chooseArtist` with a name that is
  not held means all.
- `setTracks` keeps the choice when the artist is still held (AB-SAME),
  and otherwise falls back to all.
- `choiceChanged` is emitted exactly when the choice becomes a different
  one, for whatever reason; not when the same artist is chosen again and
  not when the list is merely filled again.
- `keeps(track)` is true for every track while there is no choice, and
  otherwise true when the track's artist (AB-ARTIST) is the one chosen
  (AB-SAME).

## AB-BYFIELD: the browser's tag

| Name | Widget | |
|---|---|---|
| `browseFieldCombo` | combo box, above the filter | the entries of `browseFields()`; the current one is `field()` |

A new browser browses by `Album Artist`. `setField(name)`, or making
another entry of the combo box the current one, changes the tag; a name
that is not offered means `Album Artist`. When the tag becomes a
different one:

- the choice ends (so `choiceChanged` is emitted if there was one);
- the filter text is cleared;
- the list is filled again from the tracks last given, by the new tag;
- `fieldChanged` is emitted, once. It is not emitted when the tag asked
  for is the one already in use.

With another tag, everything above reads with that tag's values in place
of artists: `artists()`, `currentArtist()` and `chooseArtist` deal in its
values, and `keeps(track)` is true when any of the track's
`browseValues` is the one chosen. Only the two fixed texts differ:

| Tag | First row | Row with no name |
|---|---|---|
| `Album Artist`, `Artist` | `All Artists (N)` | `[Unknown Artist] (T)` |
| `Album` | `All Albums (N)` | `[Unknown Album] (T)` |
| `Composer` | `All Composers (N)` | `[Unknown Composer] (T)` |
| `Genre` | `All Genres (N)` | `[Unknown Genre] (T)` |
| `Grouping` | `All Groupings (N)` | `[Unknown Grouping] (T)` |
| `Year` | `All Years (N)` | `[Unknown Year] (T)` |

## AB-FILTER

Text in `artistFilterEdit` hides the artist rows whose name does not
contain it, both folded as for search (`foldForSearch`: case and accents
ignored). The first row is never hidden, and neither is the row of the
chosen artist. The filter changes what is visible, never the choice.

## AB-SHELL

`ArtistBrowsing(shell, settings)`:

- reads `browser/artistsShown` from the settings: shown unless the
  stored text is `false` (in any case of letters) or `0`;
- when shown, makes an `ArtistBrowser`, hands it to
  `LibraryShell::setBrowser`, and gives it the shell's `nodeTracks()`
  now and at every `nodeTracksChanged`;
- when the browser's choice changes, narrows the shell
  (`LibraryShell::setNarrowing`) to the tracks the browser keeps, or
  ends the narrowing when there is no choice.

The tag is kept in `browser/field`: every browser this object makes is
set to the stored tag (a text that is not offered, or none, means
`Album Artist`), and when the browser's tag changes the new one is
stored at once.

`setShown(false)` takes the browser out of the shell and ends the
narrowing; `setShown(true)` puts a new one in, with no choice. Either
stores `browser/artistsShown` at once. `browser()` is the browser while
it is shown, and null otherwise.

The choice is not stored: a new start begins with all artists.

## AB-MAIN

`src/app/main.cpp` additionally:

- creates an `ArtistBrowsing` over the library shell and the settings,
  after the library shell;
- understands `--parts` (added to the usage line): after everything has
  been created, and without showing the window, it prints
  `artist-browser=shown` or `artist-browser=hidden` and ends with status
  0. Later plans add lines of their own.

## Not in this version

- A command in the View menu to show and hide the browser: the command
  catalogue has no entry for it. Until it has, the setting is changed in
  `linuxbee.ini`.
- A second column, artist pictures, and the number of albums under each
  name (`ArtistEntry::albums` is worked out but not yet shown).
- Tags other than the seven offered.
- Remembering the chosen artist and the browser's width.
- Choosing several artists at once.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| AB-ARTIST | Whether the artist column files a track under Album Artist or under Artist by default. |
| AB-LIST | What the "all" row is called and what number it shows. |
| AB-FIELD | Which tags the menu beside the column's heading offers, and in which order. |
| AB-FIELD | Whether a track with several values of a tag appears under each of them. |
