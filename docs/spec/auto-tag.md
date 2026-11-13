# Auto-tagging an album from MusicBrainz

The user selects an album's files and asks for its tags. linuxBee asks
MusicBrainz which releases fit, the user picks one, the files are matched
to its tracks, and the tags that differ are offered for saving.

This document is the contract for `src/core/autotag/musicbrainz.h`. The
locked test `tests/locked/tst_musicbrainz.cpp` checks it against two saved
answers in `tests/data/musicbrainz/`. Nothing in this unit uses the
network or touches a file: it builds the URLs, reads the answers and works
out the changes. Fetching and saving belong to the caller.

## Sources

| Tag | Source |
|---|---|
| W-AT | MusicBee wiki, "Auto-Tagging": https://musicbee.fandom.com/wiki/Auto-Tagging |
| MB | MusicBrainz API documentation: https://musicbrainz.org/doc/MusicBrainz_API , its search page https://musicbrainz.org/doc/MusicBrainz_API/Search and its rate limiting page https://musicbrainz.org/doc/MusicBrainz_API/Rate_Limiting |
| CAA | Cover Art Archive API: https://musicbrainz.org/doc/Cover_Art_Archive/API |
| M-… | A linuxBee decision. |

Read on 2026-11-13. What MusicBee documents (W-AT): Auto-Tag by Album runs
a search, lists the albums found, and matches the selected tracks to the
album picked; the results are loaded for the user to save; MusicBrainz is
the first of the services it can use. How MusicBee matches files to tracks
is not documented; M-MATCH is linuxBee's.

## M-AGENT

Every request carries the User-Agent
`linuxBee/VERSION ( URL )`, where VERSION is `versionString()` and URL is
the project's public `https://` address (MB requires a meaningful agent
with contact information, and blocks clients without one).

The caller must also keep to one request per second (MB). That is the
caller's job, not this unit's.

## M-QUERY

`releaseSearchUrl(artist, album, limit)` is
`https://musicbrainz.org/ws/2/release/` with exactly three query items:

- `query`: `artist:"A" AND release:"B"`, each value trimmed; with one of
  the two blank, only the other part; with both blank, an invalid URL.
  Inside the quotes a backslash is written `\\` and a double quote `\"`
  (the search syntax's escapes), so no text can break out of its phrase.
- `fmt`: `json`.
- `limit`: held to 1 through 100.

The URL as sent is ASCII: everything else is percent-encoded, including
the characters that would split a query string.

`releaseLookupUrl(id)` is
`https://musicbrainz.org/ws/2/release/ID?inc=recordings+artist-credits+labels+release-groups&fmt=json`.

`frontCoverUrl(id, size)` is
`https://coverartarchive.org/release/ID/front-SIZE` for sizes 250, 500 and
1200, and `.../front` for size 0 (CAA). Any other size is an invalid URL.

A release id is 36 characters: lower-case hex digits in groups of 8, 4, 4,
4 and 12 joined by hyphens. Anything else gives an invalid URL, so
nothing from an answer can steer a request elsewhere.

## M-SEARCH and M-RELEASE: reading answers

An answer is not ok, with a message, when it is not a JSON object, when it
has an `error` member (that text is the message: the service says "busy"
this way), or when it lacks the member that makes it what it should be:
`releases` (an array) for a search; a valid `id` and `media` (an array)
for a release.

An *artist credit* is an array of parts; the text is each part's `name`
(or, when that is missing, its `artist.name`) followed by its
`joinphrase`, all joined, then trimmed.

**Search.** Each entry of `releases` with a valid `id` gives a candidate,
in the order given: `title`, the artist credit, `date`, `country`,
`status`, `track-count`, `score` (a number, or a number as text), the
`label.name` and `catalog-number` of the first `label-info` entry, the
number of `media` as `discCount`, and `format`: the media formats grouped
in order of first appearance, a count and `×` before a format that occurs
more than once, joined by ` + ` (`2×CD + DVD`); a medium with no format
counts as `?`; no media gives an empty text.

**Release.** `title`, artist credit, `date`, `country`, label and
catalogue number as above; `originalDate` from
`release-group.first-release-date`; `hasFrontCover` from
`cover-art-archive.front`; `discCount` is the number of media. Tracks are
read medium by medium: `disc` counts media from 1 and `position` counts
tracks within the medium from 1, whatever numbers the answer carries.
A track's title is its own `title`, else its recording's; its artist is
its own credit, else its recording's, else the release's; its length is
its own `length`, else its recording's, else 0.

Anything else that is missing is an empty text or 0.

## M-MATCH

`matchTracks(files, release)` gives each file a release track or none.

For every file and every release track a score is added up:

| Points | When |
|---|---|
| 3 | the file's Track# (its leading digits) equals the track's position, and the file's Disc# equals the track's disc; a file with no Disc# counts as disc 1 |
| 4 | the titles are equal after folding (compatibility decomposition, lower case, everything but letters and digits removed) |
| 2 | instead of the 4: one folded title contains the other, and neither is empty |
| 2 | both lengths are known and differ by 3 seconds or less |
| 1 | instead of that 2: they differ by 10 seconds or less |

A pair needs at least 3 points to count. The pairs are then taken from
the highest score down; among equal scores, the earlier file first, then
the earlier track. A pair is accepted when its file and its track are
both still free. A file left over has `releaseTrack` -1 and score 0.

## M-CHANGES

`releaseTagChanges(tags, release, releaseTrack, options)` names a field
only when the release has a value for it and the file's value differs:

| Field | Value |
|---|---|
| Title, Artist | the track's |
| Album, Album Artist | the release's title and artist |
| Year | the release's date |
| Original Year | `originalDate` |
| Track#, Track Count | the position, and the number of tracks on that disc |
| Disc#, Disc Count | the disc, and `discCount` |
| Publisher | the label |

A value the release does not have never clears a field. With
`blankFieldsOnly`, a field that has any value is left alone. An index
outside the release, or a release that is not ok, gives no changes.

## Not in this version

- The network: sending requests, the one-per-second limit, retries when
  the service is busy, and fetching the cover image.
- Identify Track (acoustic fingerprints), other services, lyrics, genres
  from Last.fm.
- Choosing between releases automatically.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| M-CHANGES | Which fields does Auto-Tag by Album fill, and does Year get the release's date or the original year? |
| M-MATCH | How does MusicBee line files up with the album's tracks when numbers and titles disagree? |
| M-AGENT | (For Dillon, not about MusicBee.) The contact URL in the User-Agent: the public repository address is assumed. |
