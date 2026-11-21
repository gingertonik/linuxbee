# Search

What the user types into the search box, and which tracks it finds.

This document is the contract for `src/core/search/search.h`. The locked
test `tests/locked/tst_search.cpp` checks it. A track is a `TagSource`, so
anything a template can read can be searched.

## Sources

| Tag | Source |
|---|---|
| W-S | MusicBee wiki, "Searching" (marked under construction): https://musicbee.fandom.com/wiki/Searching |
| S-… | A linuxBee decision. |
| W-DIA | MusicBee wiki, "Searching for Artists Like Beyoncé and Tiësto" (one user's guide): https://musicbee.fandom.com/wiki/Searching_for_Artists_Like_Beyonc%C3%A9_and_Ti%C3%ABsto |

Read on 2026-11-13. What the page establishes (W-S): the search covers all
fields unless a field is chosen; `fieldname:searchtext` restricts a term to
one field; phrases go in quotes; values can be joined with OR. The rest of
the syntax is not documented; what follows is linuxBee's.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Searching" (no version given;
2021-04-28).

## S-FOLD

Text is compared after folding: Unicode compatibility decomposition (NFKD),
combining marks removed, then lower case. So `Beyoncé`, `BEYONCE` and
`Beyonce` followed by a combining accent all fold to `beyonce`, and the `ﬁ`
ligature folds to `fi`. U+001F (the unit separator) becomes a space, so
that no folded value and no folded term holds it (S-CACHE); a broken tag
may hold one. Nothing else is changed; spaces and punctuation stay.

## S-FIELDS

A term with no field name is looked for in each field of
`SearchOptions::fields`. The default list is every text field of the
catalogue but Lyrics, and Filename, 29 in all: first the nine of before,
Title, Artist, Album, Album Artist, Genre, Composer, Year, Comment,
Filename; then Conductor, Publisher, Grouping, Mood, Lyricist, Copyright,
ISRC, Sort Title, Sort Artist, Sort Album, Sort Album Artist, Sort
Composer, Original Year, Original Artist, Original Album, Subtitle,
Language, Initial Key, Encoded By, Mix Artist.

Not in it: Lyrics (long, and a word in a song is not what a search box
looks for), the seven number and flag fields (Track#, Track Count, Disc#,
Disc Count, BPM, Compilation, Love: with them `1` would find every first
track and every one-disc album), and what the library counts or rates.
Each can still be named (`track#:1`). linuxBee's own choice, the owner's
ruling of 2026-11-20 (MusicBee searches "all fields" by default, W-S, and
does not say which).

## S-PARSE

The text is cut into tokens at white space, except inside double quotes.

- A token is the characters up to the next white space. A `"` opens a
  quoted part that runs to the next `"`, or to the end of the text if there
  is none; white space inside it is kept. Quoted and unquoted parts that
  touch are one token: `ab"c d"ef` is the token `abc def`.
- A token that begins with `-` followed by anything but white space is
  *negated*, and the `-` is dropped. A `-` anywhere else is an ordinary
  character.
- The first `:` outside quotes that has something before it splits the
  token into a field name and a text: `artist:jay`. The name may be quoted
  to hold a space: `"album artist":jay`. Later colons belong to the text.
  A `:` with nothing before it is an ordinary character.
- A token that is exactly `OR`, in upper case, unquoted, with no field and
  no `-`, is the operator. It joins the term before it and the term after
  it into one group. An `OR` with no term on one side is dropped.
- A `+` on its own (unquoted, with no field and no `-`) is the same
  operator. A `+` in a token with other characters (`c++`, `a+b`) is an
  ordinary character. (W-S gives one example in which `+` joins two
  artists as OR does; that it is the same operator is read from that one
  example, the owner's ruling of 2026-11-20.)
- Only `OR` in upper case and `+` join. `or` in lower case is a word, so
  a band or a title with "or" in it is still found. (linuxBee's own,
  2026-11-20. The same example in W-S also has an `or` in lower case
  joining; read here, it is a word the tracks must hold.)
- A token with an empty text and no field is dropped. A token with a field
  and an empty text (`genre:`) is kept.

Each remaining token is a term: field name and text, both folded, and the
negated mark. The query is a list of groups; a group is one term, or
several joined by OR. `OR` and `+` bind tighter than a space:
`x a OR b y` is x, (a or b), y.

Parsing never fails.

## S-MATCH

A track matches when every group has at least one term that holds. A
query with no groups (`isEmpty()`) matches every track.

A term with no field holds when its text is found inside the folded value
of at least one of the search fields. Each field is looked at on its own:
a phrase does not run from one field into the next. (With a cache it is
looked for in one joined value, S-CACHE; the answer is the same.)

A term with a field asks the track for that field by the name given. It
holds when the track knows the field and the text is found inside its
folded value; with an empty text, when the value is not blank (blank: empty
once white space is trimmed from both ends). A field the
track does not know never holds.

A negated term holds exactly when it would not have held.

## S-CACHE

Folding a value is the dear part of a match, and it depends on the
track only, not on what is searched for. `matches(track, options,
&cache)` answers exactly as `matches(track, options)` does, and
remembers in `cache` (a `SearchCache`, empty when made) each field it
asked the track for, by the field's name in lower case: its folded
value, whether it is blank, or that the track does not know the field. A field the cache has is not asked for or
folded again, whatever the query. The caller keeps one cache for each
track and throws it away when the track changes; a cache used with
another track gives wrong answers.

For a term with no field the cache also holds one value of the track,
made once: `joined`, the folded values of the fields of `options.fields`
the track knows, blank ones too, in the order of the list, each from the
next by U+001F; `joinedFor` is the list it was made for and `joinedMade`
says it is made. A term with no field is looked for in `joined` alone.
Since no folded value and no folded term holds U+001F (S-FOLD), a term
found there is found inside one field's value, as S-MATCH asks. A match
with another list makes `joined` again, from the fields the cache holds
(folding only those it does not), and the fields go into the cache as
they are folded, so a field named after it is not folded again. Each
value is folded once and counted once, joined or not.

`prepareSearch(track, options, &cache)` makes `joined` for `options.fields`
as a term with no field would, with no query: what the cache holds is not
folded again, and a cache already made for that list is left as it is.
With no cache it does nothing. A caller that has time before the first
keystroke (the track list, T-MODEL) spends it here.

`searchValuesFolded()` counts, for the whole process since it started,
the values folded for a match, with a cache or without. It is for tests
and for timing; nothing decides anything by it.

## S-CANONICAL

`canonical()` writes the query out: each term as its text in double
quotes, with `field:` before it if it has one (the name in double quotes
too when it contains a space) and `-` before that if it is negated; the terms of a group joined by ` OR `
(a `+` is written so too); the groups joined by one
space. An empty query is an empty text. Parsing the canonical text gives
the same query.

(A text that itself contains a double quote cannot be written this way
and is not expected to survive the round trip.)

## Not in this version

- Wildcards, regular expressions, comparison (`year:>2000`) and ranges.
  The rules engine does those; the search box is for typing.
- Ranking. A track matches or it does not.
- Search as a saved filter.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| S-PARSE | Is a space between words AND? Does `-word` exclude? What does `+` do? |

### Decided by the owner (2026-11-20)

| Rule | Question | Decision |
|---|---|---|
| S-FIELDS | Which fields does a plain search cover by default? | Every text field but Lyrics, and Filename; not the seven number and flag fields, since `1` would find every first track (the owner, 2026-11-20). MusicBee says "all fields" (W-S) and does not list them. linuxBee's own. |
| S-FOLD | Does searching "beyonce" find "Beyoncé"? | Yes here, always: a search ignores accents (the owner, 2026-11-20). In MusicBee it does so only when "ignore diacritic marks when searching" is ticked, and it is not by default (W-DIA). A difference, and meant. |
