# Templates

A template is a line of text that linuxBee fills in for one track. Templates name
files and folders, build virtual tags, and format text shown in the player.
The syntax follows the template language MusicBee users already know.

This document is the contract for `linuxbee::Template` in
`src/core/template/template.h`. The locked tests in
`tests/locked/tst_template.cpp` check it row by row.

## Sources

| Tag | Source |
|---|---|
| W-F | MusicBee wiki, "Functions": https://musicbee.fandom.com/wiki/Functions |
| W-T | MusicBee wiki, "Templates": https://musicbee.fandom.com/wiki/Templates |
| W-O | MusicBee wiki, "File Organization": https://musicbee.fandom.com/wiki/File_Organization |
| W-UV | MusicBee wiki, "Useful Virtual Tag Formulas" (users' examples): https://musicbee.fandom.com/wiki/Useful_Virtual_Tag_Formulas |
| F-C | MusicBee forum, functions cheatsheet, post 2: https://getmusicbee.com/forum/index.php?topic=35868.0 |
| T-… | A linuxBee decision. The public sources do not say. Listed again under "Open parity questions". |

Pages were read on 2026-11-12.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Functions" (3.1.6305; 2022-09-28);
"Templates" (3.0.5994; 2018-02-22); "File Organization" (3.0.5993;
2019-12-18); "Useful Virtual Tag Formulas" (no version given; 2017-07-26).

## 1. The three kinds of text

A template is read left to right and is made of:

1. **Fields**: `<Name>`, replaced by that field's value for the track.
2. **Function calls**: `$Name(argument,argument,...)`, replaced by the
   function's result.
3. **Literal text**: everything else, copied to the output unchanged.

### T-LEX: what counts as a field or a call

- A `<` starts a field only when a `>` follows it with at least one character
  between them and none of those characters is `<`, `>`, `,` or a line break.
  The field name is the text between the brackets, exactly as written.
  Otherwise the `<` is literal text. So `a < b` and `<3` are literal text.
- A `$` starts a call only when it is followed by one or more ASCII letters
  and then, immediately, `(`. Otherwise the `$` is literal text. So `$5` and
  `50% $ off` are literal text.
- The backslash has no special meaning to the template engine (W-T). Folder
  separators are handled later, by the code that turns a result into a path.

### T-NAMES: function names

Function names are matched without regard to case: `$upper(` is `$Upper(`.
A call to a name that is not in the table in section 4 is a parse error.

### T-UNKNOWN: a field the track source does not know

When the tag source returns "no such field" (`std::nullopt`), the field is
written to the output as it appeared in the template, brackets included:
`<Nope>` gives `<Nope>`. This holds at the top level and inside arguments.

A field's value is data. It is copied into the output and is never read
again as template text: a title of `$Lower(<Artist>)` comes out as exactly
those characters.

### T-BLANK: a field that is known but empty

A blank field gives an empty string. One exception: when
`TemplateOptions::unknownForBlank` is true, a blank field that sits at the
top level of the template (not inside any function argument) gives
`Unknown ` followed by the field name, for example `Unknown Album` (W-O).
A field inside a function argument is never replaced this way, which is what
makes the documented idiom `$IsNull(<Album>,,<Album>)` work (W-T).

`TemplateOptions::blankAs` maps field names (ignoring case) to a text: a
blank top-level field named there (empty, or only white space) gives that
text, before `unknownForBlank` is looked at. Inside a function argument the
field is blank as it is. Empty by default; organise uses it (paths.md,
P-PLACEHOLDER).

## 2. Arguments

Inside the parentheses of a call:

- An unquoted `,` ends the argument. An unquoted `)` ends the call.
- An argument may be empty: `$IsNull(<Album>,,<Album>)` has an empty second
  argument (W-T).
- An argument is a run of pieces written side by side: quoted text, unquoted
  text, fields and calls. The pieces are evaluated and joined in order, so
  `$Upper(<Artist>" - "<Title>)` upper-cases `Artist - Title` (F-C).

### T-QUOTE: quotes and spaces

- Inside an argument, text between two `"` characters is taken exactly as
  written, including spaces, commas, parentheses, `<`, `>` and `$`. The quote
  characters themselves are removed (W-F). There is no escape for a `"`
  inside quoted text.
- Unquoted text has the white space at the very start and very end of the
  argument removed. Space between pieces is kept. `$Upper( ab c )` gives
  `AB C`; `$Upper(" ab ")` gives ` AB `.
- An unquoted `(` inside an argument that does not belong to a call is
  literal text, and so is the `)` that balances it: `$Upper(a (b) c)` gives
  `A (B) C`.
- Outside any call, a `"` is ordinary literal text.

## 3. Conditions

Some arguments are conditions: the first argument of `$If`, and every
argument of `$And`, `$Or` and `$Not`.

### T-COMPARE

(How a condition in a template compares. track-list.md has a rule of
the same name, for the order of two values in a column; they are told
apart by the document.)

- A condition is split at its first unquoted `=`, `<` or `>` that is not
  inside a nested call and is not the start of a field (T-LEX decides that).
  The text before it is the left side, the text after it is the right side.
  Any later `=`, `<` or `>` is part of the right side.
- Both sides are evaluated like arguments (T-QUOTE applies to each side).
- If both results are numbers (section 5), they are compared as numbers:
  `4` equals `4.0`, and `1 < 10` is true.
- Otherwise `=` is true when the two texts are equal ignoring case, and `<`
  and `>` are false.
- A condition with no operator is true exactly when it evaluates to `T`.
  This is how `$If($Contains(<Genre>,rock),yes,no)` works (F-C).
- In an argument that is not a condition, `=`, `<` and `>` are literal text.

`$And`, `$Or`, `$Not`, `$Contains` and `$IsMatch` return the one-letter
texts `T` and `F` (W-F, F-C).

## 4. Functions

"n" arguments are counts (section 5). Regular expressions are section 6.

| Function | Args | Result | Source |
|---|---|---|---|
| `$Upper(x)` | 1 | x in upper case | W-F |
| `$Lower(x)` | 1 | x in lower case | W-F |
| `$Trim(x)` | 1 | x without leading and trailing white space | W-F |
| `$Len(x)` | 1 | number of characters in x | F-C |
| `$Left(x,n)` | 2 | first n characters of x | W-F |
| `$Right(x,n)` | 2 | last n characters of x | W-F |
| `$CutLeft(x,n)` | 2 | x with its first n characters removed | F-C |
| `$CutRight(x,n)` | 2 | x with its last n characters removed | F-C |
| `$Pad(x,n)` | 2 | x with `0` added on the left until it is n characters long | W-F |
| `$First(x)` | 1 | the first value of a multi-value field: the text before the first `TemplateOptions::multiValueSeparator` | W-F |
| `$RomanNumerals(x)` | 1 | x (1 to 3999) as a Roman numeral | F-C |
| `$IsNull(x,a,b)` | 3 | a when x is empty, otherwise b. `0` is not empty | W-F |
| `$If(cond,a,b)` | 3 | a when cond is true, otherwise b | W-F |
| `$And(cond,cond)` | 2 | `T` when both are true, else `F` | W-F |
| `$Or(cond,cond)` | 2 | `T` when at least one is true, else `F` | W-F |
| `$Not(cond)` | 1 | `T` when cond is false, else `F` | F-C |
| `$Contains(x,y)` | 2 | `T` when x contains y, ignoring case (T-CASE), else `F` | W-F |
| `$IsMatch(x,re)` | 2 | `T` when re matches somewhere in x, else `F` | W-F |
| `$Replace(x,from,to)` | 3 | x with every `from` replaced by `to`, case-sensitive (T-CASE) | W-F |
| `$RxReplace(x,re,to)` | 3 | x with every match of re replaced by `to` | W-F |
| `$RxMatch(x,re)` | 2 | the first part of x that re matches | F-C |
| `$Split(x,sep,i)` | 3 | section i of x, split at sep, counting from 1 from the left | W-F |
| `$RSplit(x,sep,i)` | 3 | the same, counting from 1 from the right | W-F |
| `$RxSplit(x,re,i)` | 3 | like `$Split`, split wherever re matches | W-F |
| `$Add(a,b)` `$Sub(a,b)` `$Mul(a,b)` `$Div(a,b)` `$Mod(a,b)` | 2 | arithmetic on two numbers | F-C |
| `$Abs(a)` | 1 | a without its sign | F-C |
| `$Sort(x)` | 1 | x with a leading ignore word moved to the end: `The Beatles` gives `Beatles, The` | F-C, W-F |
| `$RemoveSortWords(x)` | 1 | x with a leading ignore word removed: `Beatles` | F-C |
| `$Group(x,n)` | 2 | the alphabet bucket of x's first letter (T-GROUP) | F-C, W-F |
| `$Date(x,format)` | 2 | the date in x written in the given format (section 7) | W-F, F-C |

A call with the wrong number of arguments is a parse error.

### Ignore words

`$Sort` and `$RemoveSortWords` use `TemplateOptions::ignoreWords`. A word
applies when x starts with it (ignoring case), the next character is a
space, and something follows the space. The first word in the list that
applies is used. `$Sort` appends `, ` and the word as it was written in x.
When no word applies, x is returned unchanged. The default list is `The`
(W-F).

### T-GROUP

x is trimmed. If its first character is not a letter (of any script), the
result is `#`. Otherwise the result is the first n characters of x, in
upper case, or all of x when it is shorter. For `Deafheaven`: n=3 gives
`DEA`, n=4 gives `DEAF`; for `The Beatles`, n=1 gives `T` (W-F: "the first
# letters"; the owner's ruling of 2026-11-20, which drops the alphabet
buckets the forum's cheatsheet describes). The characters after the first
are taken as they are (`AC/DC`, n=3: `AC/`). An empty x or an n below 1
gives an empty string. The `#` for a first character that is not a letter is
linuxBee's own.

### T-CASE

`$Contains` ignores case, as does `=` in a condition. `$Replace`, `$Split`
and `$RSplit` are case-sensitive.

## 5. Numbers and counts

### T-MATH

- A number is optional white space, an optional `-`, digits, and an optional
  `.` followed by digits, with `.` as the decimal point whatever the system
  locale.
- If either input of an arithmetic function is not a number, the result is
  an empty string. Division or modulo by zero gives an empty string.
- A whole result is written without a decimal point (`$Div(6,3)` is `2`).
  Any other result is written with up to 15 significant digits and no
  trailing zeros (`$Div(7,2)` is `3.5`).

### T-RANGE

- A count (the n of `$Left`, `$Right`, `$CutLeft`, `$CutRight`, `$Pad`,
  `$Group` and the i of the split functions) must be a whole number, 0 or
  more. When it is not: `$Pad` returns x unchanged; every other function
  returns an empty string.
- A count larger than the text is not an error: `$Left(abc,99)` is `abc`,
  `$CutLeft(abc,99)` is empty, `$Pad(1234,3)` is `1234`.
- A split index of 0, or beyond the number of sections, gives an empty
  string. An empty separator means "do not split": section 1 is all of x.
- `$Replace` with an empty `from` returns x unchanged.
- `$RomanNumerals` of anything that is not a whole number from 1 to 3999
  returns x unchanged.
- `$RxMatch` with no match gives an empty string.
- Characters are counted as UTF-16 code units, as `QString` counts them.

## 6. Regular expressions

### T-REGEX

- Patterns are compiled with `QRegularExpression`, default options
  (case-sensitive).
- In the `to` text of `$RxReplace`, `$0` to `$9` stand for the whole match
  and capture groups 1 to 9. Every other character is literal.
- A pattern that does not compile never crashes and never matches:
  `$IsMatch` gives `F`, `$RxMatch` gives an empty string, `$RxReplace`
  returns x unchanged, `$RxSplit` treats x as one section.

## 7. Dates

### T-DATE

`$Date(x,format)` reads x as one of: an ISO 8601 date (`2021-12-31`), an ISO
8601 date and time (`2021-12-31T19:30:00`), a four-digit year (`1999`,
meaning 1 January), or a year, a month and a day with slashes
(`2021/12/31`, `2021/1/5`), or a year and a month (`1998/03`, meaning its
1st). W-VTF builds such a text to format it; the year and month alone are
linuxBee's own. A time may follow the day as in the ISO form
(`2021/12/31 19:30`). Spaces round x are ignored; a date that does not exist
(`2021/02/30`) or a day written first (`31/12/2021`) gives an empty string,
as anything else does. Tag sources supply
date fields in one of those forms.

The format is scanned left to right. At each position the longest token
below that matches is replaced; any other character is copied.

| Token | Meaning | Example |
|---|---|---|
| `yyyy` | year, four digits | 2021 |
| `yy` | year, two digits | 21 |
| `MMMM` | month name, English | December |
| `MMM` | short month name, English | Dec |
| `MM` | month, two digits | 12 |
| `M` | month | 12 |
| `dddd` | weekday name, English | Friday |
| `ddd` | short weekday name, English | Fri |
| `dd` | day, two digits | 31 |
| `d` | day | 31 |
| `HH` | hour 00 to 23 | 19 |
| `H` | hour 0 to 23 | 19 |
| `hh` | hour 01 to 12 | 07 |
| `h` | hour 1 to 12 | 7 |
| `mm` | minute, two digits | 30 |
| `m` | minute | 30 |
| `ss` | second, two digits | 00 |
| `s` | second | 0 |

`MMMM`, `MMM`, `dddd`, `ddd`, `HH` and `H` are linuxBee's own: W-F lists
only `yy`, `yyyy`, `M`, `MM`, `d`, `dd`, `h`, `hh`, `m`, `mm`, `s` and `ss`.

Names come from `QLocale::c()`, so they do not change with the system
language.

## 8. Errors

### T-ERRORS

`Template::parse` never throws and never crashes. A template with any of
these faults is invalid:

| Fault | Example |
|---|---|
| a call that is never closed | `$Upper(<Artist>` |
| a quote inside a call that is never closed | `$Upper("abc)` |
| a call to a name not in the table | `$Nope(x)` |
| the wrong number of arguments | `$Left(<Artist>)` |
| calls nested more than 256 deep | |

`error().position` is the index, counting from 0, of the `$` that begins the
faulty call: the innermost one when calls are nested, and for the depth
limit the `$` of the 257th level. `error().message` is a non-empty English
sentence. Only the first fault is reported. A valid template has
`error().position == -1`.

`evaluate` on an invalid template returns an empty string.
`referencedFields` on an invalid template returns an empty list.

## 9. referencedFields

Every field name in the template, in order of first appearance, each once,
including fields inside arguments and conditions, and including names the
tag source may not know.

## 10. Command line

### T-CLI

```
linuxbeectl template eval [--field NAME=VALUE]... [--unknown-for-blank] TEMPLATE
linuxbeectl template check TEMPLATE
```

- `eval` prints the result and a line break on standard output and exits 0.
  Fields come only from `--field`; NAME is the text before the first `=` and
  VALUE is everything after it, which may be empty.
- `check` prints `OK` and exits 0 when the template is valid.
- For an invalid template both print `error at N: message` on standard
  error, print nothing on standard output, and exit 2.
- A usage mistake (missing template, `--field` without `=`, unknown command
  or option) prints a message on standard error, nothing on standard output,
  and exits 2.

## Not in this version

- `$Count`, `$Sum`, `$Min`, `$Max`: they read the whole library and arrive
  with the library database.
- Functions that MusicBee gets from the third-party "Additional Tagging &
  Reporting Tools" plugin (`$Char`, `$Round`, `$Now`, `$TitleCase`, the
  duration functions and the rest). They are a later decision.

Until then a template that calls one of them is invalid ("unknown
function"), which is reported to the user and never silently dropped.

## Open parity questions

linuxBee decisions that someone with MusicBee in front of them could confirm or
correct. Each is one line in a template field of the original program.

| Rule | Question |
|---|---|
| T-COMPARE | Is `=` in `$If` case-sensitive? linuxBee: no. |
| T-CASE | Is `$Contains` case-sensitive? linuxBee: no. Is `$Replace`? linuxBee: yes. |
| T-MATH | What does `$Div(7,2)` give? linuxBee: `3.5`. What does `$Div(6,0)` give? linuxBee: empty. |
| T-UNKNOWN | What does a misspelled field such as `<Nope>` render as? linuxBee: the text `<Nope>`. |
| T-QUOTE | Are unquoted leading and trailing spaces in an argument dropped? linuxBee: yes. |
| T-GROUP | What does `$Group` give for a title starting with a digit? linuxBee: `#`. |
| T-RANGE | What does `$Split(<Title>," ",9)` give when there are 3 words? linuxBee: empty. |
| T-REGEX | Does `$RxReplace` use `$1` for groups? linuxBee: yes (the .NET convention). |
| T-CASE | Is `$IsMatch` case-sensitive? A formula on "Useful Virtual Tag Formulas" tests an upper-case letter against `[gioqu-z]`, which suggests it is not; the page does not say. linuxBee: as the pattern says. |
