# Auto-playlists in the navigator

Auto-playlists are kept as files, listed in the navigator under
`Playlists`, and show their tracks when chosen: the rule set is asked
again each time.

This document is the contract for
`src/core/playlists/autoplayliststore.h`,
`src/ui/shell/autoplaylistnodes.h`, and for what `src/app/main.cpp` does
with them. The locked tests `tests/locked/tst_ui_autoplaylistnodes.cpp`
and `tests/locked/cli_autoplaylists.sh` check it. What an auto-playlist
holds is auto-playlists.md (A-EVALUATE); its text form is A-JSON; the
navigator's rows are navigator.md (V-AUTO, V-MENU).

## Sources

| Tag | Source |
|---|---|
| W-AP | MusicBee wiki, "Playlists", the part on auto-playlists: see auto-playlists.md |
| AN-… | A linuxBee decision. MusicBee's own files (`.xautopf`) are not read here; importing them is plan 21's and 22's business. |

## AN-STORE

One folder; one file per auto-playlist, named NAME and `.lbauto`,
holding the text of A-JSON in UTF-8.

- A name is valid by the rule for playlist names (playlists.md, Y-NAME).
  There are no sub-folders.
- `names()`: every file of the folder with that ending and a valid name,
  without the ending, in ascending order with case ignored. Other files
  are not looked at. A folder that does not exist has none.
- `exists(name)`: exact, case included.
- `load`: fails with a reason when there is no such auto-playlist, and
  with what `autoPlaylistFromJson` says when the file cannot be
  understood. On failure `*playlist` is left as it was.
- `save`: writes the file whole or not at all (a temporary file, then a
  rename), making the folder if needed. A name that is not valid is
  refused with the reason of Y-NAME; a new name that differs from an
  existing one only by case is refused (`an auto-playlist named "OTHER"
  is already there`). Saving over an existing name replaces it.
- `rename`: the same checks for the new name; renaming to its own name in
  another case is allowed; to the very same name succeeds and changes
  nothing. An unknown name fails.
- `remove`: deletes the file; false when there was none.
- `listChanged` is emitted after a new one is saved, a rename that
  changed something, and a removal. `changed(name)` after an existing one
  is saved again. Nothing is emitted for anything refused or failed.

## AN-NODES

`AutoPlaylistNodes(window, shell, library, store, hooks)`:

- gives the navigator the store's names (`setAutoPlaylists`), now and at
  every `listChanged`;
- adds a node source to the library shell (library-shell.md, SH-EXTEND)
  that answers for every node whose id begins with `auto:`.

Destroying the object takes the rows out of the navigator, and its node
source answers for nothing from then on.

## AN-SHOW

The tracks of the node `auto:NAME` are

`evaluateAutoPlaylist(playlist, library.tracks(), context, seed)`

with the playlist loaded from the store at that moment, `context.now`
from `hooks.now()` at that moment, and every track of the library,
whatever its section.

- `seed` is asked from the hooks once, when the object is made, and used
  for the object's whole life: a random choice stays the same while the
  program runs and changes with the next start.
- The node is read again whenever the library changes (SH-EXTEND), and
  when the store says the auto-playlist shown was saved again.
- An auto-playlist that cannot be loaded shows no tracks, and the status
  bar says `Cannot show the auto-playlist "NAME": REASON`, REASON being
  the store's.

The track list sorts what it is given by its own columns; the order in
which the auto-playlist chose its tracks is not shown in this version.

## AN-MENU

`commandsFor("auto:NAME")` is `delete` when the store has NAME, and
nothing for every other row. `label("delete")` is `Delete`.

`run("delete", "auto:NAME")` asks `confirm("Delete the auto-playlist
\"NAME\"? Its tracks stay in the library.")`; on yes it removes it from
the store, and the status bar and `lastMessage()` say `Deleted the
auto-playlist "NAME"`. On no: false, nothing changed, no message. If the
file cannot be removed: false and `Could not delete the auto-playlist:
the file could not be removed`.

The object listens to the navigator's `menuRequested` as the playlist
commands do (playlist-editing.md, PE-WIRING): for a row with commands,
`showMenu` and then `run`. Without hooks, `confirm` is a yes-or-no
question and `showMenu` a pop-up menu.

## AN-MAIN

`src/app/main.cpp` additionally:

- creates an `AutoPlaylistStore` on the folder `auto-playlists` beside
  the folder `playlists` in the data folder, and an `AutoPlaylistNodes`
  with the default hooks, after the library shell;
- adds to what `--parts` prints the line `auto-playlists=N`, N being how
  many the store has.

## AN-SPEED

Speed: a showing hands each track of the library out once
(`Library::tracks()`), and takes the tracks it shows from that one
answer: `tracksRead()` rises by the number of tracks in the library,
whether the playlist holds none of them or all.

## Not in this version

- **Making and editing an auto-playlist in the window.** The command
  `file.playlists.newAuto` stays unimplemented: a rule editor is a plan
  of its own. Until then an auto-playlist is a `.lbauto` file put into
  the folder (the text `linuxbeectl` prints for a rule set, inside the
  form of A-JSON).
- Renaming from the menu; folders of auto-playlists.
- Showing the order the auto-playlist chose.
- Reading `.xautopf` files.
