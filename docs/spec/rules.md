# Rules

A rule set answers one question about one track: does it belong? Auto-
playlists, library filters and organiser exceptions all ask it.

This document is the contract for `src/core/rules/rules.h` and for
`linuxbeectl rules`. The locked tests `tests/locked/tst_rules.cpp` and
`tests/locked/cli_rules.sh` check it. Nothing here reads the disk or the
clock: the track is a `TagSource` (the template engine's view of a track)
and "now" is passed in.

## Sources

| Tag | Source |
|---|---|
| W-R | MusicBee wiki, "Filters" (rules, ANY or ALL, sub-rules, lists separated by semicolons) and "Playlists", the part on auto-playlists: https://musicbee.fandom.com/wiki/Filters , https://musicbee.fandom.com/wiki/Playlists#Auto-Playlists |
| W-API | MusicBee wiki, "API Method Reference": https://musicbee.fandom.com/wiki/API_Method_Reference |
| U-… | A linuxBee decision. |
| W-ANS | MusicBee wiki, the pages that answer the questions under "Answered from the MusicBee wiki": "Tracks View": https://musicbee.fandom.com/wiki/Tracks_View |

Pages were read on 2026-11-13.

What the public pages establish (W-R): a rule set matches ANY or ALL of its
rules; a rule can carry sub-rules, which are AND conditions on that rule;
operators named "is not", "contains", "is any of", "is not any of", and
regular-expression operators exist. The full list of operators, and which
apply to which field, is not documented. The list below is linuxBee's.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Filters" (no version given;
2016-03-23); "Playlists" (3.0.5992; 2020-04-28); "API Method Reference" (no
version given; 2025-05-19).

## 1. The model

A `RuleSet` has a `match` word (`All` or `Any`) and a list of rules. A
`Rule` has a field name, an operator, a list of values, and a list of
sub-rules.

### U-MATCH

- A set that fails `validateRules` matches nothing.
- A set with no rules matches every track.
- `All`: every rule must hold. `Any`: at least one must.

### U-SUB

A rule holds when its own condition holds and every one of its sub-rules
holds. Sub-rules may have sub-rules. A top-level rule is at depth 1; a rule
at depth 9 or more makes the set invalid.

## 2. Values

### U-VALUES

The field's text is fetched from the track by name. The text is split at
every `"; "` (a semicolon and a space, which is how several values are
joined), each piece is trimmed, and empty pieces are dropped. What is left
are the field's values.

### U-EMPTY

A field the track does not have, and a field whose text is blank, both have
no values.

The text operators are *positive* (`is`, `contains`, `starts-with`,
`ends-with`, `is-any-of`, `matches`, `matches-case`) or *negative*
(`is-not`, `does-not-contain`, `is-not-any-of`, `does-not-match`,
`does-not-match-case`). A positive operator
holds when at least one value passes its test. A negative operator holds
when no value passes the positive test. So with no values every positive
operator fails and every negative one holds.

### U-TEXT

Text is compared ignoring case. `is` passes when the value and the rule's
value are the same text, or when both read as numbers (U-NUMBER) and the
numbers are equal: `02` is `2`. `is-any-of` passes when `is` would pass for
any of the rule's values.

### U-REGEX

`matches` searches each value for the regular expression (Perl syntax, as
`QRegularExpression`), ignoring case. It need not cover the whole value
unless it is anchored. `matches-case` does the same and heeds case: `^Beat`
finds `Beatles` and not `beatles`. A pattern may still ask for itself to
ignore case (`(?i)`). `does-not-match` and `does-not-match-case` are their
negative forms.

(The owner, 2026-11-20: MusicBee's rules have a regular expression that
heeds case and a "/i" form that does not (W-R, "Filters"). `matches` keeps
the meaning it has had, so that a stored set means what it meant; the form
that heeds case is the new id.)

### U-NUMBER

A text reads as a number when, trimmed, it is a decimal number (`4`, `4.5`,
`-1`) or a clock time `m:ss` or `h:mm:ss`, which counts as its seconds
(`6:27` is 387).

`greater-than`, `less-than` and `in-range` look at the field's first value.
If it does not read as a number, the rule fails. `in-range` includes both
ends.

### U-DATE

A text reads as a date when, trimmed, it is `yyyy` (1 January of that
year), `yyyy-MM` (the first of that month), `yyyy-MM-dd`, or an ISO 8601
date and time. A date and time without a zone is taken as UTC.

`before` and `after` look at the field's first value and compare calendar
days in UTC. A first value that is not a date fails the rule. The same day
is neither before nor after.

### U-LAST

`in-the-last N UNIT` holds when the field's first value is a date at or
after `now` minus N units, and not after `now`. UNIT is `days`, `weeks`
(7 days each), `months` or `years` (calendar months and years). Moments are
compared, not days.

`not-in-the-last` holds when the field's first value is a date and
`in-the-last` does not hold. A field with no date matches neither: a
track that was never played is not in "not played in the last month"
(the owner's ruling, 2026-11-14; the wiki does not say what MusicBee
does).

## 3. Operators

### U-NAMES

In enum order: `is`, `is-not`, `contains`, `does-not-contain`,
`starts-with`, `ends-with`, `is-any-of`, `is-not-any-of`, `matches`,
`does-not-match`, `has-value`, `has-no-value`, `greater-than`, `less-than`,
`in-range`, `before`, `after`, `in-the-last`, `not-in-the-last`,
`matches-case`, `does-not-match-case`. Ids are lower case and matched
exactly. A new operator is added at the end, so that no stored id moves.

`ruleOperatorLabel(op)` is what a person reads for an operator: in a rule
editor, and in any rule text of `linuxbeectl` or the manual. For the
regular expressions it follows MusicBee's names: `matches-case` is `match
RegEx`, `matches` is `match RegEx/i`, `does-not-match-case` is `does not
match RegEx` and `does-not-match` is `does not match RegEx/i`. For every
other operator it is the id with a space for each `-` (`is not`).

`has-value` holds when the field has at least one value; `has-no-value`
when it has none.

### U-ARITY

| Operators | Values |
|---|---|
| `has-value`, `has-no-value` | none |
| `is-any-of`, `is-not-any-of` | one or more |
| `greater-than`, `less-than` | one, a number |
| `in-range` | two numbers, the lower first (equal is allowed) |
| `before`, `after` | one, a date |
| `in-the-last`, `not-in-the-last` | a whole number of 1 or more, then a unit |
| `matches`, `does-not-match`, `matches-case`, `does-not-match-case` | one, a valid regular expression |
| the rest | exactly one (an empty text counts) |

### U-VALID

`validateRules` returns one line per rule that breaks U-ARITY, has a blank
field name, or is nested too deep (U-SUB). The line starts with the rule's
position and a colon: `rule 2:` for the second top-level rule, `rule 1.2:`
for the second sub-rule of the first rule, and so on down.

## 4. Storing rules

### U-JSON

```json
{"match":"any","rules":[{"field":"Genre","op":"is-any-of","values":["Rock","Pop"],"and":[{"field":"Rating","op":"greater-than","values":["3"]}]}]}
```

`rulesToJson` writes compact JSON with no white space, keys in the order
`match`, `rules`; and for a rule `field`, `op`, `values`, `and`. `values`
is always written. `and` is written only when there are sub-rules.

`rulesFromJson` accepts any white space and any key order. `match` may be
left out (it is then `all`), as may `rules` and a rule's `values` and
`and`. It refuses, with a message in `*error` and without changing
`*rules`: text that is not a JSON object; a `match` that is not `all` or
`any`; a `rules` or `and` that is not a list; a rule that is not an object;
a `field` that is missing or not text; an `op` that is missing or not a
known id; a `values` that is not a list of texts; any key not named here.
`error` may be null.

Reading does not validate: a set that loads may still fail U-VALID.

## 5. Command line

### U-CLI

```
linuxbeectl rules check JSON
linuxbeectl rules match [--field NAME=VALUE]... [--now WHEN] JSON
```

- JSON that does not load: the message on standard error, exit 2.
- A set that fails U-VALID: the problem lines on standard error, exit 2.
- `check` prints the set as `rulesToJson` writes it, exit 0.
- `match` tests the set against the fields given. It prints `match` and
  exits 0, or prints `no match` and exits 1. `--now` is an ISO 8601 date and
  time; without it the current time is used.
- A usage mistake: a message on standard error, exit 2.

## Not in this version

- Reading MusicBee's auto-playlist files. The parts of the file are
  named in public (W-API); the codes for fields and comparisons are not.
- The auto-playlist options around the rules (limit, selection order,
  source): they belong to playlists.
- Which operators a rule editor offers for which field.
- A rule on "any field", and the value "[playing track]": MusicBee's
  rules have both (W-R).

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| U-NAMES | The operators MusicBee offers for a text, a number, a rating and a date field, as named in its rule editor (in part answered below). |
| U-VALUES | Does "Genre is Rock" match a track tagged Rock and Blues? linuxBee: yes. |
| U-LAST | Decided 2026-11-14 by the owner (the wiki does not say): "not in the last 30 days" does not match a track that was never played. |
| U-SUB | Can sub-rules have sub-rules? linuxBee: yes, to depth 8. |
| U-TEXT | Is "is" case-sensitive? linuxBee: no. |

### Answered from the MusicBee wiki (checked 2026-11-20)

| Rule | Question | What the wiki says |
|---|---|---|
| U-NAMES | The operators MusicBee offers for a text, a number, a rating and a date field, as named in its rule editor. | In part: "Tracks View" says a field's type (text, date, number, rating) decides which rule operators are offered, and a date or a number field has greater and less than. The names of the rest are still open above. |
