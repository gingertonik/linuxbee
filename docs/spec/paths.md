# Paths from naming templates

The file organiser, the converter and device sync all need the same thing: a
naming template plus one track's tags, turned into a safe relative path such
as `Adele/21/01-11 Someone like You.mp3`.

This document is the contract for `src/core/paths/pathbuilder.h` and for
`Template::topLevelFields()`. The locked tests in
`tests/locked/tst_paths.cpp` and `tests/locked/cli_paths.sh` check it.
Nothing in this subsystem reads or writes the disk.

## Sources

| Tag | Source |
|---|---|
| W-O | MusicBee wiki, "File Organization": https://musicbee.fandom.com/wiki/File_Organization |
| W-H | MusicBee wiki, "How to Create a File-Naming Template": https://musicbee.fandom.com/wiki/How_to_Create_a_File-Naming_Template |
| W-T | MusicBee wiki, "Templates": https://musicbee.fandom.com/wiki/Templates |
| W-UV | MusicBee wiki, "Useful Virtual Tag Formulas" (users' examples): https://musicbee.fandom.com/wiki/Useful_Virtual_Tag_Formulas |
| P-… | A linuxBee decision. The public sources do not say. Listed again under "Open parity questions". |
| W-ANS | MusicBee wiki, the pages that answer the questions under "Answered from the MusicBee wiki": "File Organization", "Useful Virtual Tag Formulas", "Templates": https://musicbee.fandom.com/wiki/File_Organization , https://musicbee.fandom.com/wiki/Useful_Virtual_Tag_Formulas , https://musicbee.fandom.com/wiki/Templates |

Pages were read on 2026-11-13.

What the public pages establish: the default naming template is
`<Album Artist>\<Album>\<Disc-Track#> <Title>` and gives paths like
`Adele\21\01-11 Someone like You.mp3` (W-O, W-H); the backslash separates
folders in an organise template (W-T); characters that are illegal in Windows
file names are each replaced by a space unless the user sets another
replacement or none (W-O); a duplicate target gets `_#` appended (W-O); a file
missing a naming tag is skipped by default, or has `Unknown X` written when
the option is off (W-O). Everything more exact than that is a P- rule below.

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "File Organization" (3.0.5993;
2019-12-18); "How to Create a File-Naming Template" (no version given;
2016-08-09); "Templates" (3.0.5994; 2018-02-22); "Useful Virtual Tag
Formulas" (no version given; 2017-07-26).

## 1. The steps, in order

`buildRelativePath(template, tags, extension, rules)` does this:

1. An invalid template gives `Status::InvalidTemplate` with the template's
   error. Nothing else happens.
2. **P-MISSING.** Find the blank top-level fields (section 2). If there are
   any and `rules.skipWhenTagsMissing` is true, the result is
   `Status::MissingTags` with those names.
3. Evaluate the template. Field values are first passed through the value
   filter (section 3). `unknownForBlank` is on exactly when
   `skipWhenTagsMissing` is off.
4. **P-SEP.** Split the result at every `\` and every `/`.
5. Clean each piece with `cleanComponent` (section 4).
6. **P-EMPTY.** Drop the pieces that are now empty. If none are left, the
   result is `Status::EmptyName`.
7. The last piece is the file name. Add the extension (section 5), keeping
   the whole name within the length limit (section 6).
8. Join the pieces with `/`. That is `relativePath`, with `Status::Ok`.

On any status other than `Ok`, `relativePath` is empty.

## 2. Missing tags

### P-MISSING

- A field is *top-level* when it is written outside every function call.
  `Template::topLevelFields()` returns the top-level field names in order of
  first appearance, each once. An invalid template gives an empty list.
- A top-level field is *blank* when the tag source knows it and its value is
  empty or only white space. A field the source does not know is not blank;
  it prints as `<Name>` (templates spec, T-UNKNOWN) and is then cleaned like
  any other text.
- `missingFields` lists the blank top-level fields in template order,
  except one named in `PathRules::blankAs` (P-PLACEHOLDER).
- A field inside a function argument never counts. This is what makes the
  documented idiom `$IsNull(<Album>,,<Album>\)` work: a blank album simply
  leaves its folder out (W-T).

### P-PLACEHOLDER

- `PathRules::blankAs` names fields whose blank top-level value is written
  as a text of its own (templates.md, T-BLANK), whatever
  `skipWhenTagsMissing` says, and such a field never counts as missing. By
  default it holds one entry: `Disc-Track#` is `#`. So a track with no
  number is named `Adele/21/# Someone like You.mp3`, as W-H shows, and is
  not refused for it. W-H drops the field with `$IsNull(<Disc-Track#>,,...)`:
  inside a function the field is still blank.
- A caller that empties `blankAs` has the rule of P-MISSING alone.

## 3. Separators and the value filter

### P-SEP

- In the template's own text, `\` and `/` both separate folders. This
  includes text written inside a function argument.
- A `\` or `/` that arrives inside a field's value is never a separator.
  Before evaluation each field value has every `\` and `/` replaced by that
  character's entry in `rules.characterMap`, or removed when the map has no
  entry for it, and has its control characters removed (P-CTRL).
- A leading separator does not make the path absolute: it produces an empty
  first piece, which P-EMPTY drops.
- Because the filter runs before the template's functions, a function sees
  the filtered value: `$Replace(<Artist>,/,-)` finds no `/` to replace.

## 4. Cleaning one component

`cleanComponent(text, rules)` applies these steps in this order.

### P-CTRL

Remove every character below U+0020, the character U+007F, and every
UTF-16 surrogate that is not part of a valid pair. They are removed, not
replaced.

### P-MAP

Replace every character that has an entry in `rules.characterMap` with its
replacement, except the space, which waits for P-SPACES. A replacement may
be several characters or empty. Any `\` or `/` inside a replacement is
dropped, and a `\` or `/` in the text that the map does not mention is
removed: no separator ever survives this step.

`PathRules::defaultCharacterMap()` has nine entries, each replaced by a
single space: `"` `|` `?` `<` `:` `\` `>` `*` `/` (W-O: the characters that
are illegal in Windows file names, replaced by a space by default).

### P-SPACES

1. Replace every run of white space with a single space.
2. Apply P-TRIM.
3. If the map has an entry for the space character, replace each remaining
   space with it.

So with the space mapped to `_`, `A: B` becomes `A_B`, not `A__B`.

### P-TRIM

Remove spaces and dots from both ends, repeatedly, until neither end is a
space or a dot. Dots inside the text are kept. `...Baby One More Time`
becomes `Baby One More Time`; `T.N.T.` becomes `T.N.T`; `..` becomes empty.

This is why no component can ever be `.` or `..`, whatever a tag contains.

### P-RESERVED

When `rules.avoidReservedNames` is true: take the text before the first dot
(the whole text when there is no dot). If, ignoring case, it is one of `CON`,
`PRN`, `AUX`, `NUL`, `COM1` to `COM9`, `LPT1` to `LPT9`, insert `_` right
after it. `CON` becomes `CON_`; `aux.live` becomes `aux_.live`.

### P-LENGTH

If the text is longer than `rules.maxComponentBytes` when encoded as UTF-8,
keep the longest prefix that fits without cutting a character in half (a
surrogate pair counts as one character), then apply P-TRIM again.

## 5. The extension

### P-EXT

From the `extension` argument keep only the characters `A` to `Z`, `a` to
`z` and `0` to `9`, in order, with their case unchanged. If anything is
left, the file name is the cleaned last piece, a dot, and that text. If
nothing is left, the file name has no extension.

## 6. Length of the file name

The last piece is cut so that the piece, the dot and the extension together
fit in `rules.maxComponentBytes` (P-LENGTH, with P-TRIM applied after the
cut). If the cut leaves the piece empty, the result is `Status::EmptyName`.

## 7. Duplicate targets

### P-DUP

`uniqueRelativePath(path, exists, rules)`:

- If `exists(path)` is false, return `path`.
- Otherwise split the last component at its last dot into stem and
  extension (no dot means no extension; dots in folder names do not count).
  Try `stem_1`, `stem_2`, ... each with the same folders and extension, and
  return the first for which `exists` is false.
- If a candidate's file name would exceed `rules.maxComponentBytes`, shorten
  the stem as in P-LENGTH so that it fits.
- After `_9999` also exists, return an empty string.

Whether two names differ only by case is the caller's question: `exists`
decides.

## 8. Command line

### P-CLI

```
linuxbeectl path build [--field NAME=VALUE]... [--ext EXT] [--allow-missing]
                       [--map C=TEXT]... [--max-bytes N] TEMPLATE
```

- Prints `relativePath` and a line break on standard output, exit 0.
- `--allow-missing` sets `skipWhenTagsMissing` to false.
- `--map C=TEXT` sets the replacement for the single character `C`; `C=`
  removes that character. Repeatable.
- `--max-bytes N` sets `maxComponentBytes`; N must be a whole number of 8 or
  more.
- `Status::MissingTags`: `missing tags: A, B` on standard error (names joined
  with a comma and a space, in template order), nothing on standard output,
  exit 3.
- `Status::EmptyName`: a message on standard error, nothing on standard
  output, exit 3.
- An invalid template: `error at N: message` on standard error, exit 2.
- A usage mistake: a message on standard error, exit 2.

## Not in this version

- Exceptions (a rule that selects a different template) arrive with the
  rules engine and the organiser.
- A limit on the length of the whole path. Only single names are limited.
- Moving, copying or renaming anything.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| P-SEP | Does a `\` inside a tag value create a folder? linuxBee: never. |
| P-SPACES | `A: B` with default mapping: one space or two? linuxBee: one. |
| P-TRIM | Is a leading dot kept (`...Baby One More Time`)? linuxBee: removed. |
| P-TRIM | Is a trailing dot or space kept in a folder name? linuxBee: removed. |
| P-MISSING | Does a blank field inside `$If(...)` count as a missing tag? linuxBee: no. |
| P-LENGTH | What happens to a name that is too long? linuxBee: cut to 255 bytes. |
| P-RESERVED | Are names like `CON` changed? linuxBee: `CON_`. |

### Answered from the MusicBee wiki (checked 2026-11-20)

| Rule | Question | What the wiki says |
|---|---|---|
| P-DUP | Is the first duplicate `name_1`, `name_2` or something else? linuxBee: `_1`. | `_1`: "File Organization" appends `_#`, and "Useful Virtual Tag Formulas" gives the example `1-08 Right as Rain_1.mp3`. |
| P-SEP | Is `/` in a naming template a folder separator, or plain text? linuxBee: separator. | "Templates" names only `\` as the folder separator, and only in templates for organising; elsewhere it is plain text. Nothing is said of `/`: as a separator it is linuxBee's own. |
