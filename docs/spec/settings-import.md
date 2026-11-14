# Importing settings from MusicBee

A MusicBee user has years of habits in a settings file. linuxBee reads
that file and carries over what it has a place for: the hotkeys, and the
template the organiser names files by. Everything else is left where it
is, and the user is told line by line what was and was not carried over.

This document is the contract for
`src/core/settingsimport/settingsimport.h` and for `linuxbeectl
settings`. The locked tests `tests/locked/tst_ui_settingsimport.cpp` and
`tests/locked/cli_settings.sh` check it.

## Sources

Each line says how it is known. **Seen** means: read in a settings file
written by a MusicBee 3 install belonging to the project's owner, or on
his screen. **Inferred** means: worked out from what was seen plus public
documentation, and not confirmed by changing the setting and looking
again.

| Tag | Source |
|---|---|
| FILE | Seen: `MusicBee3Settings.ini` in the program's settings folder. UTF-8 text, CRLF line ends, an XML declaration, a root element `ApplicationSettings`, about 900 simple settings as child elements, some lists. One value (`ArtistSortMapping`) holds the character U+0001 between entries, which XML does not allow. |
| HK | Seen: a `HotKeys` element holding `KeyCommandPair` elements, each with `Key` (a whole number), `Command` (a name such as `EditSelectAll`) and `IsGlobal` (`true` or `false`). |
| KEYS | Microsoft's documentation of the .NET enumeration `System.Windows.Forms.Keys` (key codes, and the modifiers Shift 65536, Control 131072, Alt 262144): https://learn.microsoft.com/en-us/dotnet/api/system.windows.forms.keys (read 2026-11-13). Inferred: that `Key` is such a value. It fits every hotkey seen on the Hotkeys page of the preferences: `EditClickedColumn` 113 is F2, `EditPreferences` 131151 is Ctrl+O, `EditCopy` 131139 is Ctrl+C, `EditProperties` 65549 is Shift+Enter. |
| NR | Seen: naming rules are elements holding `NamingRule` elements, each with `Field` (a number, 0 for none), `Text` (literal text that follows) and sometimes `Func` (a formula). |
| FIELDS | Inferred: the field numbers, from rules left at their documented defaults. `OrganisationDefaultFilenameRules` reads 31, `\`, 30, `\`, 53, a space, 65, and the organiser's default is `<Album Artist>\<Album>\<Disc-Track#> <Title>` (paths.md); `AutoTagFromFilenameTemplate` has 86 where the track number stands. |
| Qt | Qt's portable text for a key sequence (`QKeySequence::PortableText`), which is what the `hotkeys` group of `linuxbee.ini` holds (preferences.md, H-STORE). |
| SI-… | A linuxBee decision. |

## SI-READ: reading the file

`readMusicBeeSettings(bytes)`:

- The bytes are UTF-8, with or without a byte order mark; line ends are
  CRLF or LF. Every character below U+0020 other than tab, carriage
  return and line feed is read as a space (FILE).
- It is not ok, with a reason in `error`, when there is no root element
  `ApplicationSettings`, or when that element is never closed (a file cut
  short). It is ok otherwise, whatever else the file holds.
- `values`: every child of the root that holds text only, by name, with
  XML's entities decoded (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`
  and numbered ones). An empty element (`<X />` or `<X></X>`) is there
  with the empty text. Of a name met twice, the last counts.
- `hotkeys`: the `KeyCommandPair` elements of `HotKeys`, in order. `global`
  is true only for the text `true`. A pair whose `Key` is not a whole
  number, or whose `Command` is empty, is passed over.
- `namingRules`: every child of the root that holds one or more
  `NamingRule` elements and nothing else, by name; of each rule, `Field`
  (0 when missing or not a number), `Text` and `Func` (in `function`).
- Nothing else is kept: other lists (`SystemPlugin` and the like) are
  passed over.

## SI-KEY: a stored key as text

`hotkeyText(stored)`: the low 16 bits are a key code, the rest modifiers
(KEYS). The text is the modifiers present, in the order `Ctrl+`,
`Alt+`, `Shift+`, and then the key's name (Qt):

| Code | Name |
|---|---|
| 65 to 90 | `A` to `Z` |
| 48 to 57 | `0` to `9` |
| 112 to 135 | `F1` to `F24` |
| 8, 9, 13, 27, 32 | `Backspace`, `Tab`, `Return`, `Esc`, `Space` |
| 33, 34, 35, 36 | `PgUp`, `PgDown`, `End`, `Home` |
| 37, 38, 39, 40 | `Left`, `Up`, `Right`, `Down` |
| 45, 46 | `Ins`, `Del` |
| 106, 107, 109, 111 | `*`, `+`, `-`, `/` (the keypad's) |
| 186, 187, 188, 189, 190, 191 | `;`, `=`, `,`, `-`, `.`, `/` |
| 192, 219, 220, 221, 222 | `` ` ``, `[`, `\`, `]`, `'` |
| 173, 174, 175 | `Volume Mute`, `Volume Down`, `Volume Up` |
| 176, 177, 178, 179 | `Media Next`, `Media Previous`, `Media Stop`, `Media Play` |

The answer is empty for any other code, for a number below 1, and when a
bit other than the three modifiers is set above the low 16.

Every text this function gives reads back unchanged through
`QKeySequence(text, QKeySequence::PortableText)`.

## SI-TEMPLATE: a naming rule as a template

`templateFromNamingRule(parts)`: each part gives its field as a tag, then
its text as it is.

| Field | Tag |
|---|---|
| 0 | nothing |
| 30 | `<Album>` |
| 31 | `<Album Artist>` |
| 32 | `<Artist>` |
| 53 | `<Disc-Track#>` |
| 65 | `<Title>` |
| 86 | `<Track#>` |

The answer is empty (nothing is carried over) when the list is empty,
when a part has a field that is not in the table (FIELDS: the others are
not known), when a part has a formula, or when a part's text holds `<`,
`>` or `$`, which mean something in a template.

## SI-PLAN: what would be carried over

`planSettingsImport(settings)` gives one line per hotkey, in the file's
order, and then one line for the organiser's naming rule if the file has
`OrganisationDefaultFilenameRules`. For settings that are not ok it gives
nothing.

A hotkey's `what` is `hotkey ` and MusicBee's command name. Its command
is looked up here:

| MusicBee | linuxBee |
|---|---|
| `EditClickedColumn` | `edit.editField` |
| `EditSelectAll` | `edit.selectAll` |
| `EditCopy` | `edit.copyTags` |
| `EditPaste` | `edit.pasteTags` |
| `EditProperties` | `file.editTags` |
| `EditCustomSearch` | `edit.find` |
| `EditPreferences` | `edit.preferences` |
| `GeneralReload` | `view.refresh` |
| `FileScanNewMedia` | `file.scanFolders` |
| `PlaybackPlayNow` | `track.playNow` |
| `PlaybackPlayNext` | `track.queueNext` |
| `PlaybackPlayLast` | `track.queueLast` |
| `PlaybackNextTrack` | `controls.next` |
| `PlaybackPreviousTrack` | `controls.previous` |
| `PlaybackPlayPause` | `controls.playPause` |
| `PlaybackStop` | `controls.stop` |
| `PlaybackVolumeUp` | `controls.volumeUp` |
| `PlaybackVolumeDown` | `controls.volumeDown` |
| `PlaybackVolumeMute` | `controls.mute` |
| `ToolsAutoOrganise` | `file.organise` |
| `ToolsAnalyseVolume` | `tools.analyseVolume` |

The line is not imported (its `key` and `value` are empty), with the
first of these notes that applies:

1. `linuxBee has no such command`: the name is not in the table;
2. `global hotkeys are not supported`: the pair is global (media keys
   reach linuxBee through the desktop instead, desktop-mpris.md);
3. `the key has no name here`: `hotkeyText` is empty;
4. `already set by an earlier line`: an earlier line was imported for
   the same linuxBee command;
5. `the key is already taken by ID`: an earlier line was imported with
   the same key text, for the command `ID`.

Otherwise it is imported: `key` is `hotkeys/` and the linuxBee command,
`value` the key text, `note` empty.

The naming rule's line has `what` `organise template`. With a template
from SI-TEMPLATE it is imported: `key` `organise/template`, `value` the
template. Otherwise its note is `a part of the rule cannot be carried
over`.

## SI-APPLY

`applySettingsImport(plan, target)` sets, for every imported line, `key`
to `value` in `target`, and returns how many it set. No other key is
touched or removed. (A hotkey stored this way replaces the default for
that command at the next start: preferences.md, H-STORE.)

## SI-CLI

```
linuxbeectl settings import FILE
linuxbeectl settings import --apply --to INI FILE
```

- Without `--apply` nothing is written. Each line of the plan is
  printed: `import KEY = VALUE (WHAT)` or `skip WHAT: NOTE`. The last
  line is `N to import, M skipped`.
- With `--apply`, the same lines, and the imported ones are written to
  the settings file `INI` (made if it is not there; INI format, as
  `linuxbee.ini`). The last line is `N written, M skipped`.
- A file that cannot be read, or is not ok: `failed: FILE: REASON` on
  standard error, nothing on standard output, exit 3.
- Exit 2 for a usage mistake: no or an unknown subcommand, no file or
  more than one, `--apply` without `--to`, `--to` without `--apply`.

## Not in this version

- A command in the window for it: until there is one, the import is run
  with `linuxbeectl`, with the program closed.
- Everything else in the file: the player's settings (repeat, shuffle,
  volume levelling, crossfade), layouts and panels, the columns of each
  view, tag handling, the file converter's settings and naming, the
  other naming rules, confirmations. Their names are known (FILE); what
  each number means is not.
- The library itself and the playlists, which are in other files.
- Field numbers beyond the seven of SI-TEMPLATE, and formulas.
- Global hotkeys, and mouse buttons.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| SI-TEMPLATE | Set the organiser's template to one tag at a time and read `OrganisationDefaultFilenameRules` again: which number is which field. This confirms the seven and adds the rest. |
| SI-PLAN | The command names of the hotkeys not in the table, and which of them linuxBee's later commands answer. |
| SI-KEY | Whether a hotkey with the Windows key, or a chord of two keys, is stored differently. |
