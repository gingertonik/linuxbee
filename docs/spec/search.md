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

Read on 2026-11-13. What the page establishes (W-S): the search covers all
fields unless a field is chosen; `fieldname:searchtext` restricts a term to
one field; phrases go in quotes; values can be joined with OR. The rest of
the syntax is not documented; what follows is linuxBee's.

## S-FOLD

Text is compared after folding: Unicode compatibility decomposition (NFKD),
combining marks removed, then lower case. So `Beyoncé`, `BEYONCE` and
`Beyonce` followed by a combining accent all fold to `beyonce`, and the `ﬁ`
ligature folds to `fi`. Nothing else is changed; spaces and punctuation
stay.

## S-FIELDS

A term with no field name is looked for in each field of
`SearchOptions::fields`. The default list is Title, Artist, Album, Album
Artist, Genre, Composer, Year, Comment, Filename.

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
- A token with an empty text and no field is dropped. A token with a field
  and an empty text (`genre:`) is kept.

Each remaining token is a term: field name and text, both folded, and the
negated mark. The query is a list of groups; a group is one term, or
several joined by OR. `OR` binds tighter than a space:
`x a OR b y` is x, (a or b), y.

Parsing never fails.

## S-MATCH

A track matches when every group has at least one term that holds. A
query with no groups (`isEmpty()`) matches every track.

A term with no field holds when its text is found inside the folded value
of at least one of the search fields. Each field is looked at on its own:
a phrase does not run from one field into the next.

A term with a field asks the track for that field by the name given. It
holds when the track knows the field and the text is found inside its
folded value; with an empty text, when the value is not blank. A field the
track does not know never holds.

A negated term holds exactly when it would not have held.

## S-CANONICAL

`canonical()` writes the query out: each term as its text in double
quotes, with `field:` before it if it has one (the name in double quotes
too when it contains a space) and `-` before that if it is negated; the terms of a group joined by ` OR `; the groups joined by one
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
| S-FOLD | Does searching "beyonce" find "Beyoncé"? |
| S-FIELDS | Which fields does a plain search cover by default? |
