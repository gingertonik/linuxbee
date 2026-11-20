# Auto-playlists

An auto-playlist is a question about the library: which tracks match these
rules, taken in this order, up to this limit. Its answer changes as the
library does.

This document is the contract for `src/core/playlists/autoplaylist.h`. The
locked test `tests/locked/tst_autoplaylist.cpp` checks it. It builds on
`docs/spec/rules.md`, `docs/spec/library.md` (L-FIELDS) and
`docs/spec/search.md` (S-FOLD).

## Sources

| Tag | Source |
|---|---|
| W-AP | MusicBee wiki, "Playlists", the part on auto-playlists: https://musicbee.fandom.com/wiki/Playlists#Auto-Playlists |
| W-API | MusicBee wiki, "API Method Reference" (the parts of an auto-playlist file, by name): https://musicbee.fandom.com/wiki/API_Method_Reference |
| A-… | A linuxBee decision. |

Read on 2026-11-13. What MusicBee documents (W-AP): rules joined by ANY or
ALL; "limit to", off by default, in minutes, hours, MB, GB, items (25 by
default) or albums; "selected by" random (the default), highest or lowest
rating, most or least recently played, most or least often played, most or
least recently added; an option to filter out duplicates, off by default;
automatic refreshing, on by default. How ties are broken, how a limit in
minutes treats the track that does not fit, and how a duplicate is decided
are not documented; those rules are linuxBee's. MusicBee's own file for
auto-playlists has its parts named in public (W-API) but not the codes
for fields and comparisons; it is not read.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Playlists" (3.0.5992; 2020-04-28);
"API Method Reference" (no version given; 2025-05-19).

## A-EVALUATE

`evaluateAutoPlaylist(playlist, tracks, context, seed)` works in four
steps and returns track ids.

### A-MATCH

A track is a candidate when it is not missing and
`matchesRules(playlist.rules, LibraryFieldSource(track), context)` holds.
Rules that do not validate give an empty playlist. Candidates keep the
order the tracks were given in.

### A-DUP

With `filterDuplicates`, a candidate is dropped when an earlier candidate
has the same artist, title and album. "The same" compares the track's
Artist values joined by `; `, its first Title and its first Album, each
folded (S-FOLD) and trimmed. The same song on two albums is two tracks
(as MusicBee does; the owner, from memory, 2026-11-14).

### A-SELECT

The candidates are put in order by `selectedBy`:

| Value | Order |
|---|---|
| `Random` | A shuffle driven by `seed`. The same seed and candidates give the same order; the method is the implementation's own. |
| `HighestRating`, `LowestRating` | By the track's Rating as a number; no rating counts as 0. |
| `MostRecentlyPlayed`, `LeastRecentlyPlayed` | By last played. |
| `MostOftenPlayed`, `LeastOftenPlayed` | By play count. |
| `MostRecentlyAdded`, `LeastRecentlyAdded` | By date added. |

Tracks that tie keep their earlier order. A track that was never played
comes last under `MostRecentlyPlayed` and first under
`LeastRecentlyPlayed`.

### A-LIMIT

With `limitEnabled`, tracks are taken from the front of that order:

- `Items`: the first `limitAmount` tracks.
- `Minutes`, `Hours`: tracks are taken while the total duration stays at
  or under the limit. The first track that would go over ends the
  playlist; later, shorter tracks are not tried.
- `Megabytes`, `Gigabytes`: the same with file sizes (1 MB is 1,048,576
  bytes, 1 GB is 1,073,741,824).
- `Albums`: an album is an Album Artist and an Album (first values,
  folded). The first `limitAmount` different albums met are in; every
  track of those albums is taken, wherever it stands in the order, and
  tracks of other albums are left out.

A `limitAmount` below 1 with the limit enabled gives an empty playlist.

## A-NAMES

Limit units, in enum order: `items minutes hours megabytes gigabytes
albums`. Selections: `random highest-rating lowest-rating
most-recently-played least-recently-played most-often-played
least-often-played most-recently-added least-recently-added`.

## A-JSON

```json
{"version":1,"rules":{"match":"all","rules":[]},"limit":{"amount":90,"unit":"minutes"},"selectedBy":"random","filterDuplicates":false}
```

`autoPlaylistToJson` writes exactly this shape: compact, keys in this
order, `rules` as `rulesToJson` writes it, and `"limit":null` when the
limit is off.

`autoPlaylistFromJson` accepts any white space and key order, and any key
may be missing (the default applies; a missing or `null` limit is off). It
refuses, with a message and without changing `*playlist`: text that is not
a JSON object; a `version` other than 1; an unknown key; `rules` that is
not an object or that `rulesFromJson` refuses; a `limit` that is not an
object with a whole `amount` of 1 or more and a known `unit`; an unknown
`selectedBy`; a `filterDuplicates` that is not true or false.

## A-SPEED

Speed: the rules are checked once for an evaluation, and their regular
expressions built once, however many tracks are given
(`CompiledRules`, compiled-rules.md): `ruleSetsChecked()` rises by
exactly one for one call of `evaluateAutoPlaylist`, also when the set
cannot be used. The answer is the one `matchesRules` gives for each
track.

## Not in this version

- Where auto-playlists are kept on disk and how the window refreshes them.
- A source other than the whole library (a filter, a playlist, a folder).
- "Select by album or work", ordering options after selection, automatic
  export of a static copy, and the add and remove lists.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| A-LIMIT | With "limit to 60 minutes", is a track that would pass 60 skipped and a shorter one tried? linuxBee stops. |
| A-DUP | Settled 2026-11-14 (the owner, from memory): artist, title and album. |
| A-SELECT | Under "most recently played", where do never-played tracks go? linuxBee: last. |
