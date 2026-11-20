# The play queue

The Playing Tracks list: the tracks lined up to play, which one is
current, and what Next and Previous mean under shuffle and repeat. It also
says when a play or a skip is counted.

This document is the contract for `src/core/playback/playqueue.h`. The
locked test `tests/locked/tst_playqueue.cpp` checks it. The queue holds
paths and plays nothing; the player asks it where to go.

## Sources

| Tag | Source |
|---|---|
| W-NP | MusicBee wiki, "Now Playing Preferences": https://musicbee.fandom.com/wiki/Now_Playing_Preferences |
| W-PT | MusicBee wiki, "Playing Tracks" and "Player Controls": https://musicbee.fandom.com/wiki/Playing_Tracks , https://musicbee.fandom.com/wiki/Player_Controls |
| Q-… | A linuxBee decision. |
| W-ANS | MusicBee wiki, the pages that answer the questions under "Answered from the MusicBee wiki": "Now Playing Preferences": https://musicbee.fandom.com/wiki/Now_Playing_Preferences |

Read on 2026-11-13. What MusicBee documents: Play Now, Queue Next and
Queue Last as the three ways to add (W-NP); repeat and shuffle toggles
(W-PT); "play count +1" by default when more than 50% of the track or more
than 4 minutes was played, and "skip count +1" for a track played less
than 20 seconds (W-NP; the page's wording of the skip default is garbled).
What Previous does under shuffle, what happens when the playing track is
removed from the list, and how shuffle treats tracks added mid-way are not
documented; those rules are linuxBee's.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Now Playing Preferences" (3.3.7491;
2020-12-30); "Playing Tracks" (no version given; 2016-10-26); "Player
Controls" (no version given; 2016-03-23).

## Q-EDIT: changing the list

Each entry has a path and an entry id. Ids are unique for the life of the
queue, are never handed out twice, and stay with their entry when it
moves. The same path can be in the list more than once.

- `playNow(paths, startAt)` replaces the list. The entry at `startAt`
  becomes current; a `startAt` outside the list means the first entry; an
  empty list has no current entry.
- `queueNext(paths)` inserts the tracks right after the current entry, in
  the order given. A later `queueNext` goes in front of an earlier one.
- `queueLast(paths)` adds them at the end.
- `removeAt(positions)` removes the entries at those positions; positions
  that do not exist are ignored. `move(from, to)` moves one entry so that
  it ends up at `to`. In both, the current entry stays current wherever it
  lands.
- `removePaths(paths)` removes every entry whose path is one of them,
  except the current entry, which stays even when its path is among
  them. It returns how many went. A path is compared as it is written.
  It is one change (`entriesChanged` once); when nothing goes, nothing
  is emitted. Speed: one pass over the list, however many paths.
- `clear()` empties the list. `setCurrent(index)` makes an entry current.

**No current entry.** After the current entry is removed, nothing is
current (`currentIndex()` is -1) but the queue remembers the place: Next
goes to the entry that followed the removed one, Previous to the one
before it, and `queueNext` inserts at that place. In a queue that never
had a current entry the place is the top.

## Q-RENAME

`renamePaths(oldToNew)` (library.md, L-MOVED) gives each entry whose
path is one of the old ones its new path, and returns how many
entries changed. Nothing else changes: the entry ids, the order, the
current entry, the shuffle round and the way back. `entriesChanged`
is emitted once when any entry changed; `currentChanged` never.

## Q-NEXT: Next and Previous, in list order

`next(automatic)` and `previous()` return the new current index, or -1
when there is nowhere to go, in which case the current entry stays.

- Next is the following entry. After the last one: with `Repeat::All` the
  first; otherwise -1.
- Previous is the entry before. Before the first one: with `Repeat::All`
  the last; otherwise -1.
- `Repeat::One` only affects a track that ended by itself
  (`automatic == true`): `next(true)` returns the current index again and
  nothing changes. The user's own Next and Previous behave as under
  `Repeat::Off`.
- `hasNext(automatic)` says whether `next(automatic)` would return an
  index.

## Q-SHUFFLE

`setShuffle(true, seed)` starts a *round*: every entry except the current
one, in an order decided by the seed. Switching shuffle on does not change
the current entry. The same list, current entry and seed give the same
round; the method is the implementation's own.

- Next takes the next entry of the round. When the round is used up: with
  `Repeat::All` a new round starts (every entry except the current one);
  otherwise -1. A one-entry list under `Repeat::All` stays on its entry.
- Previous goes back through the entries that were current before, most
  recent first, and returns -1 when there are none. Going back does not
  use the round up: Next afterwards retraces the same entries forward.
- `queueNext` tracks are played next, in the order given.
- `queueLast` tracks join the current round at a place the seed's random
  sequence decides.
- A removed entry leaves the round and the way back.
- `setCurrent` jumps: the entry left joins the way back, and the entry
  jumped to is not played again in this round.
- `playNow` starts a new round on the new list.
- `setShuffle(true, seed)` while shuffle is on starts a new round with the
  new seed. `setShuffle(false, ...)` ends shuffling: Next continues in
  list order from the current entry.

## Q-PEEK: what Next would give

`peekNext(automatic)` returns the entry (its id and path) that
`next(automatic)` would make current, or nothing where it would return
-1. It changes nothing and emits nothing: the list, the current entry,
the round, the way back and every later Next are as if it had not been
asked, however often it is.

It holds in every mode. In particular:

- under `Repeat::One`, for a track that ended by itself, it is the
  current entry;
- when a shuffle round is used up under `Repeat::All`, it is the entry
  the new round will begin with, although that round is not dealt until
  Next is called;
- with no current entry, it is the entry at the place the queue
  remembers (Q-EDIT).

`nextChanged` is emitted during every call after which either
`peekNext(true)` or `peekNext(false)` gives a different answer than
before it. It may be emitted when the answer happens to be the same,
but not by a call that changes nothing (Q-SIGNALS). `setShuffle(true,
seed)` while shuffle is on deals a new round and emits it, although no
other signal tells of that call.

The player uses the two together to have the next file ready before
the current one ends.

## Q-SIGNALS

`entriesChanged` after the list changed. `currentChanged(index)` after the
current index changed, and after every `playNow` (a new list is a new
current entry even at the same index). `modeChanged` after repeat or
shuffle was switched to a different value. A call that changes nothing
emits nothing.

## Q-COUNT: plays and skips

`classifyPlay(listenedMs, durationMs, endedByUser, rule)`:

1. **Played** when more than `playedFraction` of the track was listened
   to (only when the duration is known, that is above 0), or more than
   `playedSeconds` seconds.
2. Otherwise **Skipped** when the user ended it (Next, Previous, choosing
   another track, Stop) and less than `skipSeconds` seconds were listened
   to.
3. Otherwise **Neither**.

A negative `listenedMs` counts as 0. The defaults are 0.5, 240 and 20
(W-NP). `listenedMs` is time actually heard, not the position reached, so
seeking forward does not make a play.

## Not in this version

- The Play Now preference (play all, play the album, queue and play):
  the caller decides which paths to pass.
- Shuffle that prefers higher-rated or newer tracks, or spreads artists
  apart; shuffling whole albums.
- Pruning played tracks, saving the list between runs, stop-after-current.
- Auto DJ.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| Q-SHUFFLE | With shuffle on, does Previous go back to what was played before, and does Next then retrace it? |
| Q-EDIT | When the playing track is removed from Playing Tracks, what does Next play? |
| Q-COUNT | Whether a track that is stopped (not skipped) early counts as a skip. |

### Answered from the MusicBee wiki (checked 2026-11-20)

| Rule | Question | What the wiki says |
|---|---|---|
| Q-SHUFFLE | Is Queue Next honoured while shuffle is on? | Yes: "Now Playing Preferences" says Queue Next "overrides shuffle setting"; Queue Last with shuffle on goes to a random place. Q-SHUFFLE already does both. |
| Q-COUNT | The exact skip-count default, and whether a track that is stopped (not skipped) early counts as a skip. | The default: a skip is counted when less than 20 seconds were played ("Now Playing Preferences"; the line that gives the default is garbled, its text says 20 seconds). Whether a stop counts is not said: still open above. |
