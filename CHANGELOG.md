# Changelog

linuxBee is numbered below 1.0 until it is ready to replace MusicBee for
daily use. Every release before 1.0 is for early testing: commands,
settings and the library file may still change.

## Unreleased

- **A rule can now match a regular expression heeding case.** `matches` has always ignored case and keeps doing so, so every stored rule set means what it meant. Two new operators, `matches-case` and `does-not-match-case`, heed case (a pattern's own `(?i)` still works). Rule editors and rule text read MusicBee's names: `match RegEx` heeds case, `match RegEx/i` does not.
- **Undo of a tag change could write the old tags into the wrong file**, when the edited file had been moved (by Organise, for one) and another file had its old name. Undo now follows a file the library moved, and leaves alone any file that is not the one it edited, saying so. One thing is given up for this: a file that another program saved after the change is also left alone, where Undo used to write over it.
- **Organise with "replace existing files" could lose a file** when the new names of one run overlapped (track 1 renamed to the name track 2 had, track 2 to the name of track 3): the middle file was replaced and went to the trash, or was deleted with `--delete-replaced`. A file of the run is no longer replaced by another: the newcomer gets a numbered name, and a second run gives the names wanted. All earlier versions are affected.
- **Convert believed the encoder.** A conversion that ended well but came out short (a damaged source) was reported as converted, and with "overwrite" replaced the file that was there. A result more than a second shorter than its source, or one that cannot be read, now fails and changes nothing. **`linuxbeectl convert --if-exists overwrite` could replace one of its own inputs** when a template or the output folder gave a result the name of another input; such a result now gets a numbered name, as in the window.
- **A move to another disk that was interrupted** (the program killed, the machine switched off) could leave a cut-off file under the track's name; the next run then gave the good file a numbered name. The file now gets its name only when it is whole. An interrupted run can leave a hidden file named `.lbmove-` and six characters in the target folder, which may be deleted. **Copy** is now as careful as a move to another disk, and a Copy that fails no longer removes a file that something else put at the target meanwhile.
- **`linuxbeectl playlist convert` wrote over its output without asking**, and a playlist converted onto itself lost its comments and relative paths. It now refuses a file that is there unless `--force` is given, and always refuses the file it reads. A script that converts onto the same output twice needs `--force`. **`linuxbeectl settings import --to` could write over the MusicBee file it was reading** when the path was spelled through `..`; that is refused now. Neither command writes through a link that leads to nothing.
- **A playlist could be emptied or mangled by a small edit.** If its file could not be read at that moment, adding a track replaced the whole list with that track. If the file was not UTF-8 (a playlist from another program in Latin-1 or UTF-16), any edit wrote every accented name back as a name of no file. Such a playlist is now left alone, and the window says why. It can still be seen and played.
- **Sending a file to the trash could destroy a file already in the trash**: one of the same name that had no record there (left by another tool, or after its record was removed by hand). The new file now gets another name, as for any name that is taken.
- `settings import` no longer writes the settings file of a running linuxBee; the import was lost when the program closed.
- **Undo of a rating change on an MP3 or WAV brings the rating frames back as they were:** a play counter, the frame of another player and the exact rating byte were lost.
- **Undo keeps a file it could not put back, and its removed cover,** so that Undo can be tried again; they were dropped at the first failure.
- A playlist that a disk error cut short while it was read is no longer taken for a short playlist and written back.
- Convert with Overwrite plays the result through before it replaces a file, and refuses one that is shorter than the source or than the file it would replace. This needs `ffprobe`, which comes with ffmpeg.
- Undo now says when the files changed since will not be put back by trying again: the message ends "(a new edit drops this)" when nothing failed and only changed files are left.
- A playlist that cannot be read whole is no longer rewritten when its files move (Relink), and a pipe with a playlist's name is no longer opened.
- **A conversion no longer replaces a file that appeared at its destination while it ran.** In Keep both the result goes to the next free name, and in Skip and Overwrite the job fails and says so; Overwrite replaces only the file it saw when it began.
- **A check that hangs no longer stops a conversion batch.** Convert with Overwrite gives up on `ffprobe` after 30 seconds of silence and fails the file (the file that is there stays), and Cancel is now answered during the check.
- **Playlists that could not follow files that moved are named.** When files move (Organise, for one), a playlist that cannot be read whole or written is passed over; the window now says which ones, instead of leaving a person to take every playlist to have followed.
- **Rating a file no longer removes the rating frames of other players or the play counter** (MP3 and WAV): the rating is now one byte of the first rating frame, and everything else in it stays. Remove Tags with "All tags" still takes every rating frame out.
- **Convert with Overwrite no longer replaces a file that another program changed while the conversion ran**, and a link that leads nowhere at the destination now counts as a file that is there: Skip leaves it, Keep both passes it over, and Overwrite replaces the link itself.
- **A check that prints without end no longer holds a conversion.** Convert with Overwrite ends `ffprobe` and fails the file when it counts far more than the file can hold, and lines that count nothing no longer keep it waiting.
- **Copy on a disk that can share data (btrfs, XFS) is immediate again and takes no second room.** The careful copy asks the disk to share the bytes first, and reads and writes them only when the disk says no.
- **A playlist that another program changed while linuxBee was editing it is no longer written over.** An editor, a sync client or another player that wrote the file between linuxBee reading it and replacing it had its change lost, and the call said it had worked. The change is now refused, and the file is left as the other program made it.
- **Playlists can be edited again on disks without hard links (FAT, exFAT).** The refusal of a change made by another program needed a second name for a file, which those disks do not give, and every edit failed there.
- **A file renamed or moved by another program keeps its plays, skips and date added at the next scan**, when it is otherwise unchanged (same size, time and tags); its playlists follow. Before, the scan saw a track gone and a track new, and the history stayed on the row marked missing.
- **Organise no longer moves a symbolic link to where it would lead nowhere.** A link whose text is relative (`../album/x.mp3`) was renamed into another folder and from there pointed at nothing; the run said it had moved the file, and the library then marked the track missing. Such a move now fails and says `the link would not lead to its file from there`; the link stays. A link that still leads to its file from the new folder, and one that leads nowhere already, are moved as before.
- **`linuxbeectl organise` says when it passed over files whose names are not valid UTF-8.** Such a file was left where it was, with no line and exit 0. After everything else (and after the plan of `--dry-run`) it now prints how many names it passed over and the folders that hold them. The exit status does not change.
- **Organise given a folder together with the folder above it no longer copies its files twice, and a Move no longer fails on them.** A file named twice was planned once for each naming: a Copy made `x.mp3` and `x_1.mp3`, and a Move moved it and then reported `the file is gone`. Now it is planned once, and a folder that was named is never removed when it is left empty.
- **A link and the file it leads to can be organised together again, in either order.** A link planned before its file was refused, and the file then moved and left it leading nowhere; one planned after its file failed with "the file is gone". The link now follows its file and is judged against it where it now is.
- **A few Ogg and Opus files in a thousand were shown about a second short** by the library and by Convert's check of lengths. They are now shown whole. The first scan after the update reads every file once, so that libraries scanned before get the right lengths too.
- **A library of an older version is copied before it is brought up to date.** The step changes the library file for good, and an older linuxBee then refuses it. The window and `linuxbeectl library` now first keep the library as it was, beside it, as `library.db.schema-1` (the version it had; `-2`, `-3` and so on when that name is taken), and say where in one line (on standard error for the command, so its answer is unchanged; also in the window's status bar). linuxBee never removes the copy: that is for the person to do. If no copy can be made (no room, say), the library is not brought up to date and does not open, and says why.
- **The copy of a library made before a schema step has the library's permissions**, so a library kept for its owner alone no longer has a copy that everyone can read. The folder is flushed to the disk once the copy has its name, before the step runs.
- **A deleted track's row is kept for thirty days** (the library's half; the window's Delete Files calls it in a later change). A track marked deleted leaves every list and count at once, and keeps its plays, skips, date added and id; a scan or Add of a file at its path brings it back. After thirty days the row is removed, unless a file is at its path. **A library opened by this version is refused by 0.8.0** (schema 3; the copy made before the step is `library.db.schema-2`).
- **Delete Files keeps the track's row for thirty days.** A file taken back out of the trash to where it was comes back at the next scan as the same track, with its plays and date added. Before, Delete Files removed the row, and the plays went with the file.
- **A `+` on its own joins two terms as OR does.** A search for `philter + sadistik` now finds tracks by either artist, as MusicBee's searching page says its `+` does. Before, the `+` was an ordinary word and the search wanted both. A `+` inside a word (`c++`, `a+b`), in quotes, or after a `-` is still a character.
- `linuxbeectl rules operators` lists every rule operator: its id, a tab, and the name a person reads (`matches-case` is MusicBee's "match RegEx", `matches` its "match RegEx/i"), so a rule set written by hand can use the right id.
- **The first keystroke of a search is quicker.** A field name written as the lists write it is now found by one lookup, not by comparing it with every name ignoring case; any other spelling is looked for as before. No answer changes.
- **A plain word in the search box is now looked for in every text field but Lyrics, and Filename,** 29 fields in all (Publisher, Conductor, Grouping, Mood, the Sort fields and the rest, where it was nine): a word in a track's Publisher or Mood now finds it. The seven number and flag fields (Track#, Disc# and the like) stay out, so `1` does not find every first track, and each can still be named (`track#:1`). A phrase still does not run from one field into the next, and a search ignores accents in all of them.
- **The first keystroke in a long track list is quick again.** With every text field searched, the first keystroke used to fold every value of every track (about 120 ms for 17,000 tracks). The track list now does that while the window is idle after tracks are loaded or changed, in pieces of at most 256 tracks, and a change to a few tracks prepares only those. The cost is about 23 MB of text held for 17,000 tracks.
- **A search ignores accents unless the box in the preferences says otherwise** (the box itself comes in a later change; MusicBee heeds them unless asked). Searching in the track list can now heed accents: the search folds each typed word both ways when it is read, a cache of folded values knows which way it was made, and the track list folds its tracks again in the background, piece by piece, when the way changes, then works the rows out again.
- **`$Date` reads a date written with slashes** (`2021/12/31`, `2021/1/5`, `1998/03`, which means the 1st), as MusicBee's formulas build one; a day written first (`31/12/2021`) is still not read. **`$Group(x,n)` is now the first n letters of x in upper case** (`$Group(Deafheaven,3)` is `DEA`), as MusicBee describes it, where it gave an alphabet bucket; a first character that is not a letter still gives `#`.

## 0.8.0 (2026-11-18)

- **Before the first start of this version, copy `~/.local/share/linuxbee` and `~/.config/linuxbee` somewhere safe.** This update moves the profile and changes the library file, and there is no way back without that copy.
- The profile moves at the first start: settings to `~/.config/linuxbee/`, the library to `~/.local/share/linuxbee/`. Close every linuxBee window and `linuxbeectl` first. If the start is refused with status 6 (`was not taken over`), nothing was lost: the message names both places.
- The library file gets a new layout when it is first opened. **linuxBee 0.7.1 and older cannot open it afterwards.**
- New: `linuxbeectl library import` brings the date added, play counts, skip counts and last played over from a library exported by MusicBee. It shows what it would do and changes nothing until `--apply` is given, and it never changes a music file.
- The fault 0.7.1 still warned of is fixed: saving one field no longer rewrites other tags of the file (MP3, WAV and M4A; see below).
- Still known: Remove Tags (and `linuxbeectl tagtool remove --all`) refuses a file that holds one field under two names (a label as both `LABEL` and `PUBLISHER`, a track total as `TOTALTRACKS`). It says so and changes nothing.
- A scan that cannot store what it finds, because another program has the library, stops at once and says so; it no longer holds the window for five seconds at every part.
- A change the library database refuses (a play that cannot be counted among them) stores
  nothing, and a change with nothing to do asks for no lock. Remapping a folder is all or
  nothing. `linuxbeectl library scan` says when it could not store, and a library in a
  read-only folder says it cannot be written to.
- The settings and the library now live in `~/.config/linuxbee/` and `~/.local/share/linuxbee/`, with the name once; the files of an older build are moved there at the first start, and one program uses a profile at a time (a second start on the same profile is refused). An older library is taken along whole, with its log, or not at all: it is settled first, never moved while another program has it open, and the start is refused (status 6) rather than shown an empty library while it waits.
- The window now says in the status bar when the library could not store a change (a play, added files, saved tags, a scan), once for each refusal or run of them, and never in a dialog.
- A tag save is refused on a file that has more than one name (a hard link, such as a library file linked from a seeding folder). Nothing is written, and the message says how many names the file has and how to make it a file of its own.
- A file that another program changed while linuxBee was saving it is no longer overwritten: the save is refused, with a message to try again, and the other program's change stays. A second save of the same file, while one is under way, is refused the same way.
- A play that cannot be counted because another program has the library no longer holds the window for five seconds, and is counted when the library is free again (while linuxBee stays open; up to 1,000 are kept).
- Sound is decoded and played on a thread of its own, so it goes on while the window is busy.
- On a library kept on a network disk, a play that could not be counted because another program had the library is no longer lost. Plays that were waiting are counted in one step.
- `linuxbeectl library import FILE --map Z:=/` brings the date added, play counts, skip counts and last played from a library exported by MusicBee, and checks its ratings and Love against the files. It changes nothing unless `--apply` is given, and never a music file.
- After an update that teaches linuxBee a new tag, the next scan reads every file again, once, so the new tag shows. Tracks loved in MusicBee show their heart after that scan. The library file is brought to a new layout when first opened; **an older linuxBee (0.7.1 and before) cannot open it afterwards**, so keep a copy of `~/.local/share/linuxbee` if you may want to go back.
- Saving a tag in an MP3 or WAV file no longer rewrites tags the change did not name. Two lyrics or two comments in a file stay lyrics and comments (a save of anything used to turn them into one custom tag, which players do not show); a label, mood or grouping stored twice stays as it was; a saved cover is no longer copied into an APE tag; a file that had only an APE tag keeps all its values when its tag is saved; a person (arranger, producer) held in a custom tag is changed there too; and several comments or lyrics are saved as one frame for each language and description they had, the rest on further lines.
- Saving a tag in an M4A file no longer writes a second copy, in capitals, of every custom tag another program wrote in small letters. A gain value or other custom tag written in small letters can now be changed and removed.
- Saving a tag in a WavPack file keeps the spelling of the tag names the file already had (a title saved as `Title` no longer comes back as `TITLE`).

## 0.7.1 (2026-11-17)

- A tag save with no room left (a full disk, a quota, a size limit) no longer replaces the song with a cut-off file and says saved; it now fails and leaves the song as it was.
- Still known: a save that changes one field can rewrite other MP3 frames (lyrics, comments,
  label and publisher values can be merged or lost). Keep a backup of music you tag.

## 0.7.0 (2026-11-17)

- linuxBee can be installed for one user with `install.sh`, and removed with `--uninstall`.
- A change made while another program has the library open waits for it. A scan that cannot
  store stores nothing (the window and `linuxbeectl` do not yet say so).
- Organise says when the library could not record its moves, and then records none of the run.
- The desktop entry no longer offers linuxBee for opening audio files, which
  it could not open yet.
- Love (a track marked loved or banned in its tags) is read from and written to files as a field, and can be used in templates and auto-playlist rules.
- Love can be set in the tag editor (L or B), and a Love column shows it as a heart. The window
  cannot add the column yet, and a library scanned before this version shows Love only once
  its files are read again.

## 0.6.0 (2026-11-17)

The first numbered version. It marks the state of `main` once the core
feature set was in place and the first set of data-safety fixes had
landed.

### What is in it

- A library that scans folders off the window's thread, with progress and
  Stop; Send To; Remove from Library; folders watched for new files.
- A track list, an Album and Tracks view with covers at each heading, and
  an artist browser.
- Playlists (including Add to Playlist) and auto-playlists that follow the
  library.
- Playback with a play queue, shuffle, repeat, ReplayGain (Controls >
  Replay Gain) and MPRIS, so desktop media keys work.
- A tag editor, five tagging tools in the window, and one step of undo for
  tag changes (Edit > Undo).
- Organise Files with naming templates and a preview; Convert Format with
  presets; Delete Files to the desktop trash, after a question.
- MusicBee settings import (hotkeys and the naming template), and plain
  `.xml` skins chosen from View > Skins.
- `linuxbeectl`, the command-line tool, for tags, the library, organising,
  converting, playlists, templates, rules, skins and the trash.

### Data safety

- An MP3 or WAV save keeps the file's own ID3v2 version, so ID3v2.4 tags
  (dates, mood and others) are no longer cut down to ID3v2.3.
- Remove from Library, Send To and Delete Files work on a selection of any
  size, all or nothing, and say so when the library refuses.
- A move to another disk is copied, checked and flushed before the
  original is removed, and a move that fails at any step loses no file.
- A file that Organise moves keeps its place in playlists, the play queue
  and the player.
- The undo keeps its pictures in its own folder in the cache, cleaned at
  start.
- linuxBee ends cleanly on SIGTERM, SIGINT and SIGHUP.

### Not yet

See the "Not yet" section of the README.
