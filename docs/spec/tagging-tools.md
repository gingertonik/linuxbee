# Tagging tools

Tools that change the tags of many tracks in one go: search and replace,
capitalise, remove tags, renumber tracks, infer tags from the file name,
copy a tag and swap two tags.

This document is the contract for `src/core/tagtools/tagtools.h` and for
`linuxbeectl tagtool`. The locked tests `tests/locked/tst_tagtools.cpp` and
`tests/locked/cli_tagtool.sh` check it.

Every tool is a pure function: it is given the tags a track has
(`TrackTags`, see `docs/spec/tag-reading.md`) and returns the changes to
make (`TagChanges`, see `docs/spec/tag-writing.md`). It reads and writes no
file. That is what lets a dialog show a preview: the preview is the result.

## Sources

| Tag | Source |
|---|---|
| W-TM | MusicBee wiki, "Tools Menu": https://musicbee.fandom.com/wiki/Tools_Menu |
| W-TT | MusicBee wiki, "Tagging Tools": https://musicbee.fandom.com/wiki/Tagging_Tools |
| W-AT | MusicBee wiki, "Auto-Tagging": https://musicbee.fandom.com/wiki/Auto-Tagging |
| W-CAP | MusicBee wiki, "How to Automate Song Title Capitalization" (one user's guide): https://musicbee.fandom.com/wiki/How_to_Automate_Song_Title_Capitalization |
| W-AS | MusicBee wiki, "Appending Some Text to Tags" (one user's guide; it copies tags with a third-party add-on): https://musicbee.fandom.com/wiki/Appending_Some_Text_to_Tags |
| G-… | A linuxBee decision. |
| W-ANS | MusicBee wiki, the pages that answer the questions under "Answered from the MusicBee wiki": "Auto-Tagging": https://musicbee.fandom.com/wiki/Auto-Tagging |

Pages were read on 2026-11-13. The wiki pages for the Search and Replace,
Renumber Tracks and Remove Tags dialogs could not be fetched, so what is
known of MusicBee is this:

- Search and Replace works "on all fields or a specific field" and offers
  regular expressions, match case, match whole word and a preview (W-TM,
  W-TT).
- Capitalise "capitalises the first letter of all words in major tags"
  (W-TM). A user's guide says it only capitalises the first word (W-CAP).
  The two disagree; linuxBee offers both and more.
- Remove Tags offers "remove all" or chosen fields (W-TT).
- Renumber Tracks will "fix track numbering for an album" (W-TM).
- Infer and Update Tags From Filename "attempts to fill in tags based on
  the folder path and filenames of tracks and a template you provide"
  (W-AT).

Everything more exact than that is linuxBee's.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Tools Menu" (3.0.5902; 2024-08-27);
"Tagging Tools" (no version given; 2020-12-29); "Auto-Tagging" (3.0.5999;
2016-10-26); "How to Automate Song Title Capitalization" (no version given;
2014-07-26); "Appending Some Text to Tags" (no version given; 2014-07-26).

## G-CHANGES: what every tool returns

- A field is named in the result only when its values would really change.
  A track that is already as the tool would leave it gives an empty result.
- New values are trimmed, and empty ones are dropped. A field left with no
  value is named with an empty list, which removes it.
- Field names are matched ignoring case and written as the catalogue
  writes them (`tagFieldNames()`).
- Unless a rule says otherwise, a tool touches catalogue fields only: not
  `other` keys and not pictures.

## G-REPLACE: search and replace

`fields` names the fields to work on. An empty list means every catalogue
field except Rating.

Each value of a field is searched on its own, and every occurrence in it
is replaced.

- Plain search: `find` is literal text; no character is special, in `find`
  or in `replace`.
- `regex`: `find` is a regular expression (Perl syntax, as
  `QRegularExpression`, with Unicode properties on). In `replace`, `$1` to
  `$9` stand for the groups, `$$` for a dollar sign, and a group that took
  no part is empty. Any other `$` is itself.
- Case is ignored unless `matchCase` is set.
- `wholeWord`: a match counts only when the character before it and the
  character after it, if any, are neither a letter nor a digit (of any
  script). With `regex` this applies to the expression as a whole.

`validateSearchReplace` returns a message when `find` is empty, a field
name is unknown, or the regular expression is not valid; otherwise an
empty text. `searchReplace` with options that do not validate returns no
changes.

## G-CASE: capitalise

(MusicBee's Tools menu has Capitalise; the finer changes of case in
W-CAP are a third-party add-on's. The rules here are linuxBee's own.)

`changeCase(text, options)` changes one text. A *word* is a run of
characters between white space. White space is kept exactly.

| Style | Result |
|---|---|
| `EachWord` | In each word the first letter becomes a capital, unless a digit comes before any letter (`1st` stays). Nothing else changes: `mcCartney` becomes `McCartney`, `up-tempo` becomes `Up-tempo`, `(live)` becomes `(Live)`. |
| `TitleCase` | A word that has a capital after its first letter is left alone (`DJ`, `AC/DC`, `mcCartney`). Of the others: a *small word* that is neither the first nor the last word is written in lower case; every other word is lower-cased and then treated as `EachWord` does. |
| `Sentence` | The whole text is lower-cased, then its first letter becomes a capital (same digit rule). |
| `Upper`, `Lower` | The whole text. |

A word is a small word when, with any characters that are not letters or
digits taken off both ends and case ignored, it is in `smallWords`. An
empty `smallWords` means the default list:

`a an and as at but by for from in into nor of off on onto or over per the to via with`

`capitalise(tags, options)` applies `changeCase` to every value of the
fields named. An empty `fields` means Title, Artist, Album and Album
Artist. An unknown field name gives no changes at all.

## G-REMOVE: remove tags

- `fields`: each name that is a catalogue field (case ignored) and has a
  value is removed. Any other name is taken as an `other` key, matched
  exactly, and removed if the track has it.
- `allTags`: every catalogue field that has a value and every `other` key.
  Where the track has an ID3v2 tag (`tagTypes` names one), the change
  also carries an empty list of rating frames (tag-writing.md,
  W-RATING, "the frames whole"), so every rating frame goes: with its
  play counter, another player's too, and one that only counts plays
  and rates nothing. (Ruled 2026-11-19: "all" means all. Naming
  Rating in `fields` takes the rating out and no more.)

  Whether the file has such a frame is the writer's to see, not this
  function's: it is given tags, often the library's, and those do not
  say. So a track with an ID3v2 tag that holds nothing to remove gets
  a change that changes nothing; writing it is `Unchanged`. A track
  with no ID3v2 tag gets no list, and one with no tag at all an empty
  change.
- `removePictures`: the picture list becomes empty, if the track has
  pictures. `allTags` alone leaves pictures alone.

## G-RENUMBER: renumber tracks

`renumberTracks(tracks, options)` returns one result per track, in the
order given. The first track gets Track# `start`, the next `start + 1`,
and so on, written with at least `digits` digits (zero-padded). With
`writeTotal`, Track Count becomes the number of tracks. Numbers are
compared as text: `04` is not `4`. A negative `start` or `digits`, or a
`digits` of more than 9, gives an empty list. (Nine digits is more than
any track number has; the limit is there so that a slip of the hand
cannot ask for a number two thousand million characters wide.)

An M4A file stores its track number as a number, not as text, so
padding cannot be held there: for a track whose `format` is exactly
`m4a`, Track# is written without padding whatever `digits` says. (With
padding the file would read back unpadded, and the same change would
be offered at every run.) Track Count was never padded.

The caller decides the order (for a dialog: the order shown in the list).

## G-INFER: tags from the file name

A pattern is text with fields in angle brackets:
`<Album Artist>/<Album>/<Track#> - <Title>`.

`validateInferPattern` returns a message when the pattern is empty, names
no field, has an unclosed `<`, names a field that is not in the catalogue,
names a field twice, or has two fields with nothing between them.

`inferFromPath(path, pattern)`:

1. The path's extension is taken off: the text from the last dot of the
   file name, unless that dot is the name's first character.
2. The pattern must match the end of what is left, folder for folder: a
   pattern with one `/` is matched against the last folder and the file
   name, and its first part must match that folder's whole name.
3. Text outside the brackets must appear exactly, case included.
4. Track#, Track Count, Disc#, Disc Count and Year take digits only. Any
   other field takes as few characters as it can, and never a `/`.
5. Each text found is trimmed. If any is then empty, there is no match.

`inferTags(tags, path, pattern, blankFieldsOnly)` turns a match into
changes (G-CHANGES). With `blankFieldsOnly`, a field that has any value is
left alone. No match, or a pattern that does not validate, gives no
changes.

`InferPattern(pattern)` is a pattern made ready once, for a caller that
asks about many paths: `problem()`, `match(path)` and
`changes(tags, path, blankFieldsOnly)` answer what
`validateInferPattern`, `inferFromPath` and `inferTags` answer for that
pattern. Speed, locked by counting: `inferPatternsPrepared()` rises by
one when an `InferPattern` is built from a usable pattern and not at
all when it is asked or copied; by one for each call of
`inferFromPath` or `inferTags` with a usable pattern; never for a
pattern that does not validate, and never for validating.

## G-COPY: copy and swap

(Not a tool of MusicBee itself: its Tools menu has no copy or swap
(W-TM), and W-AS does it with a third-party add-on. linuxBee's own.)

`copyTag(tags, from, to)` gives `to` the values of `from`; if `from` has
none, `to` is removed. `swapTags(tags, a, b)` exchanges the two. Unknown
names, or the same field twice, give no changes.

## G-CLI: command line

```
linuxbeectl tagtool replace  --find TEXT [--replace TEXT] [--field F]... [--regex] [--match-case] [--whole-word] FILE...
linuxbeectl tagtool case     [--style each-word|title|sentence|upper|lower] [--field F]... FILE...
linuxbeectl tagtool remove   [--field F]... [--all] [--pictures] FILE...
linuxbeectl tagtool renumber [--start N] [--digits N] [--total] FILE...
linuxbeectl tagtool infer    --pattern P [--overwrite] FILE...
linuxbeectl tagtool copy     --from F --to F FILE...
linuxbeectl tagtool swap     --from F --to F FILE...
```

All take `--dry-run`.

1. Usage is checked first: exit 2, nothing read. That covers a missing or
   unknown tool, no file, a missing required option, anything the
   `validate` functions refuse, an unknown field name (except for
   `remove`, where it is an `other` key), an unknown style, a `--start` or
   `--digits` that is not a whole number of 0 or more, a `--digits` of
   more than 9, and `remove` with
   none of its three options.
2. Every file is read. If one is missing or not music: `failed: FILE:
   REASON` on standard error, exit 3, and no file is changed.
3. For each file in the order given, each change is printed, fields in
   code-point order of their names, then `other` keys, then pictures:

   ```
   FILE: Field: "old values" -> "new values"
   FILE: other KEY: "old values" -> "new values"
   FILE: pictures: 1 -> 0
   ```

   Values are joined with `; `. A file with nothing to change prints
   nothing.
4. Unless `--dry-run`, each file's changes are saved with `writeTags`. A
   save that fails prints `failed: FILE: ...` on standard error; the run
   goes on and exits 3. Otherwise exit 0.

`infer` fills blank fields only unless `--overwrite`. `renumber` numbers
the files in the order given.

`infer` matches the pattern against where the file is: its absolute
path, made from the working directory when FILE is relative, with `.`
and `..` parts worked out as text. Links are not followed, so a link is
matched by its own name and folder. A pattern with folders therefore
matches the same whatever the working directory. What is printed is
still FILE as it was typed.

## Not in this version

- Presets, and chains of several replacements in one preset.
- Undo. A caller that wants it keeps the old values, which are in the
  `TrackTags` it passed in.
- Searching `other` keys.
- The tools that need the network (auto-tag by album, identify track,
  artwork, lyrics) and volume analysis: separate plans.
- Finding duplicates.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| G-CASE | What does Tools > Tagging Tools > Capitalise do to "the dark side of the moon", and to "DJ shadow"? Which fields are its "major tags"? |
| G-REPLACE | With no field chosen, which fields does Search and Replace cover? Is Rating one? |
| G-REPLACE | What does the replacement text use for a group: `$1` or `\1`? |
| G-RENUMBER | What options does Renumber Tracks offer (start, padding, total, per disc)? |
| G-INFER | Whether a part of the infer pattern can be skipped (the rest is answered below). |

### Answered from the MusicBee wiki (checked 2026-11-20)

| Rule | Question | What the wiki says |
|---|---|---|
| G-INFER | The syntax of the infer pattern, and whether a part can be skipped. | In part: the pattern is a template, and each piece of plain text in it matches its first occurrence in the path; the preview shows only the tags that would change ("Auto-Tagging"). Whether a part can be skipped is still open above. |
