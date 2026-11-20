# Rules where the public pages do not say what MusicBee does

**Status:** Decided by the owner, 2026-11-14.

## The question

A handful of rules have no public source. They are the owner's rulings, and
each stays an open question for someone who can look at the original
program.

## The decision

- A playlist plays on a double-click.
- Organising leaves emptied folders unless asked.
- A folder added in the preferences is taken in at once.
- "Not played in the last N days" does not match a track never played.
- The artist browser files a track under Album Artist, and Artist when
  that is blank; its field can be chosen.
- The repeat button cycles off, all, one.

## What it allows

Each can be changed when the documented behaviour turns out to differ.

## Where it is relied on

- `docs/spec/playback-shell.md`
- `docs/spec/organise.md`, O-EMPTY
- `docs/spec/background-shell.md`
- `docs/spec/library-preferences.md`, LP-ADD
- `docs/spec/rules.md`, U-LAST
- `docs/spec/artist-browser.md`, AB-ARTIST and AB-FIELD
- `docs/spec/player-bar.md`, PB-COMMANDS
