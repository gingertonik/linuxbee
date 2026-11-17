# Playback in the window

The second assembly: the play queue, the player and the player bar are
created and connected to the library shell, so that activating a track
plays it, the bar controls it, and plays and skips reach the library.

This document is the contract for `src/ui/shell/playbackshell.h` and for
what `src/app/main.cpp` does with it. The locked tests
`tests/locked/tst_ui_playbackshell.cpp`, `tst_ui_playbackshell_nodes.cpp`
and `tests/locked/cli_playback.sh` check it.

The shell owns no rule. What Next does, when a play counts and what the
bar shows are decided in play-queue.md, player.md and player-bar.md.

## Sources

| Tag | Source |
|---|---|
| W-PC | MusicBee wiki, "Player Controls", "Playing Tracks" and "Now Playing": see player.md and play-queue.md |
| PS-… | A linuxBee decision. |

## PS-BUILD

Constructing the shell creates a `PlayQueue`, a `Player` over that queue
and the backend it was given, and a `PlayerBar`, which goes into the
window's player bar panel. The bar's stored state is restored at once
(player-bar.md, PB-STORE). Nothing is played.

Destroying the shell stops the player, removes the bar from the window
(the panel is empty again) and frees the commands. The library shell may
outlive it; its signals then go nowhere.

## PS-PLAY

When the library shell emits `playRequested(paths, index)`, the queue
plays those paths now, starting at `index` (`PlayQueue::playNow`). The
player follows the queue by itself.

## PS-TRASHED

When the library shell emits `filesTrashed(paths)`, those paths leave
the queue (`PlayQueue::removePaths`). The entry that is current stays
and the player is told nothing: the track plays to its end from the
file the player has open, and Next goes on to what is left. A seek in
it, or playing it again, fails as for any file that has gone
(player.md, X-ERROR).

"Remove from Library" leaves the file, and leaves the queue alone.

## PS-MOVED

When the library emits `pathsMoved` (library.md, L-MOVED), the shell
gives the moves to the player's `renamePaths` (player.md, X-RENAME).
A file that is playing goes on playing and is counted for its track.

## PS-UNQUEUE

When the library shell emits `queueRemovalRequested(paths)` (the
Delete key in Playing Tracks, SH-REMOVE), those paths leave the queue
in one change (`PlayQueue::removePaths`): every entry with one of the
paths, but the current one. The track that is playing is not pulled
out from under the player; it plays to its end and Next goes on to
what is left, as in PS-TRASHED. The player is told nothing.

The message, in the status bar and `lastMessage()`, N being the
entries that went:

| What happened | Message |
|---|---|
| N > 0, the current entry's path not among `paths` | `Removed N from the queue` |
| N > 0, the current entry's path among them | `Removed N from the queue; the playing track stays` |
| N = 0, the current entry's path among them | `The playing track stays in the queue` |
| N = 0 otherwise | nothing is said |

## PS-NODE

When the navigator reports a node activated (a double-click or Return on
it; navigator.md) and the node is a playlist (`playlist:` and an id):

1. if it is not the node on show, it is chosen first
   (`NavigatorPanel::selectNode`); a node the navigator does not have
   ends it here;
2. the queue plays the node's tracks now, from the first
   (`PlayQueue::playNow` with the paths of `LibraryShell::nodeTracks()`,
   in that order, and index 0).

That is the playlist's own order, whatever the list is sorted by, and
all of it, whatever the search box holds. A playlist with no track the
library knows plays nothing and leaves the queue alone.

When the node is an auto-playlist (`auto:` and a name): it is chosen
first in the same way, and the queue then plays the tracks the list
shows, in the order shown, from the first (`PlayQueue::playNow` with
`LibraryShell::shownPaths()` and index 0). An auto-playlist has no order
of its own, so the order is the list's sort, and what the search box or
a narrowing hides is not played: what is seen is what is played, as for
a double-click on a track. One that shows nothing plays nothing and
leaves the queue alone.

Every other node (a library section, Playing Tracks) plays nothing when
activated.

MusicBee plays a playlist on a double-click (the owner, from memory,
2026-11-14).

## PS-COMMANDS

Each works on the library shell's selected paths, in the order shown, and
does nothing when nothing is selected.

| Command | Does |
|---|---|
| `track.playNow` | `PlayQueue::playNow(selection, 0)`: the selection replaces the queue |
| `track.queueNext` | `PlayQueue::queueNext(selection)` |
| `track.queueLast` | `PlayQueue::queueLast(selection)` |

Queueing never starts or disturbs playback.

## PS-PLAYING

Whenever the queue's entries change, the library shell is told their
paths, in queue order (`LibraryShell::setPlayingPaths`), so the Playing
Tracks node shows the queue.

## PS-TITLE

`titleFor(path)`: for a track the library knows that has a Title, its
artists (all of them, joined as the tag reader joins them) followed by
` - ` and the title; the title alone when it has no artist. Empty for a
track with no Title and for a path the library does not know.

When the player changes track and `titleFor` is not empty, the bar is
given it (`PlayerBar::setTrackText`). Otherwise the bar keeps its own
fallback, the file's name.

## PS-COUNT

`Player::playCounted(path)`: `Library::recordPlay` for that track with the
time from `now`. `Player::skipCounted(path)`: `Library::recordSkip`. A
path the library does not know is ignored.

**Through `PendingPlays`** (`shell/pendingplays.h`), one, the shell's
child. A play count is the one thing in the library a scan cannot
bring back, and the library waits only a quarter second for it
(library.md, L-WAIT), so:

- a play or skip the library **refused** (it said `failed` during the
  call) is kept, with the time it happened. One it had nothing to do
  for (a track it does not have) is not;
- what is kept is offered again, **oldest first**, before the next
  play or skip is recorded, and once more when the program saves its
  state on the way out (PS-STORE). The offering stops at the first
  refusal, and then what is new joins the kept untried: one call asks
  the library to wait at most once;
- **one track refused twice is two plays**; nothing is merged;
- no more than 1,000 are kept. With that many, the oldest is let go
  for each new one, and `oldestDropped` says so the first time;
- they are kept in memory: a crash loses them, as it loses the play
  that was sounding.

The status bar (`lastMessage()` too), when a call leaves another
number kept:

| | Line |
|---|---|
| some are kept | `The library could not be changed: N plays or skips are not counted yet and will be tried again` (`1 play or skip is`) |
| none any more | `The plays and skips that were waiting are counted` |
| the first time one is let go | `The library could not be changed: more than 1000 plays or skips are waiting, and the oldest are no longer kept` |

## PS-ERROR

`Player::playbackError(path, message)`: the status bar shows
`Cannot play ` + the file's name + `: ` + the message, kept as
`lastMessage()`. Nothing is counted. What the player does next is the
player's rule (player.md, X-ERROR).

## PS-GAIN: ReplayGain on playback

(The owner's ruling, 2026-11-16: ReplayGain applied on playback is in
1.0.)

The shell implements `controls.gain.off`, `controls.gain.track` and
`controls.gain.album` (Controls > Replay Gain). They are checkable
and exactly one is checked: the mode. Choosing one checks it,
unchecks the others, stores it, and applies it to the track that is
playing at once. Choosing the one that is on changes nothing.

The mode is stored in the settings as `playback/gain`: `off`,
`track` or `album`, when it is chosen. A new shell starts with the
stored mode; no value, or any other value, is `off`.

The shell gives the player its track factor (player.md, X-VOLUME):
for a path, `gainFactor(gainFromTags(tags), options)` (replay-gain.md,
N-TAGS and N-APPLY), where `tags` are the tags the library holds for
the track at that path, and `options` has the mode and everything
else as `GainOptions` makes it (no pre-amp, clipping prevented). A
path the library does not know has the factor 1. No file is read.

X-VOLUME holds the result between 0 and 1: a gain that would make a
track louder uses the room the user's volume leaves and no more.

Speed: the library is asked for a track when the factor is first
wanted for its path under a mode, and the answer is kept until the
path or the mode changes, or the library says the track changed.
Moving the volume or muting asks the library nothing.

## PS-STORE

`saveState()` saves the bar's state (PB-STORE). The queue is not saved.

## PS-MAIN

`src/app/main.cpp` additionally:

- creates a `ThreadedAudioBackend` (audio-thread.md) whose factory is
  `makeDeviceBackend` (`audio/devicebackend.h`: an `FfmpegAudioBackend`
  over a `DeviceSink` that is its child), and a `PlaybackShell` over
  it, with the same `now` as the library shell. `main` itself makes
  neither the sink nor the engine and does not name them: they are
  made on the audio thread and never belong to the window's (AT-OWN).
  So sound goes on while the window is busy, and a file that is slow
  to open does not hold the window;
- on the way out, before anything else is saved or destroyed: the
  playback shell's `saveState()`, then the playback shell is destroyed
  (which stops the sound) while the backend still exists. The backend
  then ends its thread (AT-END): in time, or after three seconds
  without it.

The program starts and leaves cleanly on a machine with no sound device.

## Not in this version

- Remembering the queue and the position between sessions.
- Reordering and removing entries in the Playing Tracks list by hand, and
  marking the track that is playing.
- A now-playing panel with artwork and lyrics; a mini player.
- Media keys and desktop integration.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| PS-PLAY | What a double-click on a track does by default (play now and replace the queue, or queue it). |
| PS-TITLE | The default text of the now-playing line. |
