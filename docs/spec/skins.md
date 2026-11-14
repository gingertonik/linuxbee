# Skins

linuxBee can take its colours from a MusicBee skin file. It reads the
plain `.xml` kind, works out from it the colours linuxBee's own parts
use, and gives them to Qt. A skin is somebody's work: linuxBee reads the
file the user points it at and ships none.

This document is the contract for `src/core/skin/skin.h`,
`src/ui/skins/skinapplier.h`, `linuxbeectl skin`, and for what
`src/app/main.cpp` does with them. The locked tests
`tests/locked/tst_ui_skins.cpp` and `tests/locked/cli_skin.sh` check it.

## Sources

**Seen** means: read in the 38 plain skins a MusicBee 3 install puts in
its `Skins` folder (their shapes only; none is copied). **Inferred**
means worked out from that and from the wiki, not confirmed in the
running program.

| Tag | Source |
|---|---|
| W-SE | MusicBee wiki, "Skinning Elements" (about 410 element names with what each accepts and where it applies; matches version 2.3): https://musicbee.fandom.com/wiki/Skinning_Elements |
| W-SG | MusicBee wiki, "Skinning Guide": a skin is lines of `<element id="..." bg="r,g,b" bg2="..." fg="..." bdr="..." />`; bg is the background, bg2 a gradient's second colour, fg usually the text, bdr the border: https://musicbee.fandom.com/wiki/Skinning_Guide |
| F-DEP | MusicBee forum, "dependent skins" (v3.1): `<root dependsOn="Other.xmlc">` lays a small skin over another in the same folder; a compiled `.xmlc` skin's own images cannot be overridden. Seen: 29 of the 38 skins are such overlays on a compiled skin. |
| SEEN | Seen: a `<variables>` block of lines `name = "r,g,b"`, and those names used as colour values; `fg2`; colours of four numbers; the sections `<colours id="Default">`, `<settings>` and `<images id="Default">` around the elements; values between the tags (`true`, a number, a picture as base64). 176 element names are in the files and not on the wiki page. |
| SK-… | A linuxBee decision. Which element colours which part of linuxBee is always one (inferred from the element's name and W-SE's description). |

## SK-READ: reading a skin

`readSkin(bytes)`:

- The bytes are UTF-8 XML, with or without a byte order mark. It is not
  ok, with a reason, when there is no root element `root` or it is never
  closed. (A compiled `.xmlc` skin is not text and so is not ok.)
- `dependsOn`: the root's attribute of that name, trimmed.
- **A colour** is three or four whole numbers from 0 to 255 separated by
  commas, spaces allowed around each: red, green, blue, and alpha (255
  when there are three).
- `variables`: in the text of every `variables` element, each line of
  the shape `NAME = "VALUE"` (spaces and tabs allowed at both ends and
  around `=`; NAME made of letters, digits, `_` and `-`) whose value is a
  colour. A value that is not a colour adds the problem
  `variable NAME: cannot read "VALUE"`. Other lines mean nothing.
- `elements`: every `element` with an `id` that is not empty, wherever
  under the root it stands, by id. Its `bg`, `bg2`, `fg`, `fg2` and `bdr`
  are each a colour or the name of a variable of this file (the
  variables are read first, wherever the block stands). An empty value
  is no value. Any other value is no value and adds the problem
  `ID ATTRIBUTE: cannot read "VALUE"`. `text` is what stands between the
  tags, trimmed.
- An id met again adds to the earlier one: each colour the later element
  has replaces the earlier one's, the others stay; its text replaces the
  earlier text when it is not empty.
- Anything else (other tags, other attributes) is passed over.
- `problems` are in the order met: variables first, then elements.

## SK-LAYER

`layeredSkin(base, top)`: when `top` is not ok, the answer is `top`. When
`base` is not ok, the answer is `top`. Otherwise: ok; the variables of
both, `top`'s winning; the elements of both, an id in both joined as
SK-READ joins a repeated id (`top` being the later); the problems of
`base`, then of `top`; `dependsOn` as `base` has it.

## SK-COLOURS: linuxBee's colours

`skinColours(skin)` gives a value for each name below whose first source
the skin has, trying the sources in the order written. A name with no
source is not in the answer: linuxBee keeps its own colour there. For a
skin that is not ok the answer is empty.

| Name | From (element, colour) |
|---|---|
| `window` | `WindowBackColour` bg; `ApplicationBackColour` bg |
| `windowText` | `Panel.Body.Default` fg |
| `base` | `Panel.Body.Default` bg |
| `text` | `Panel.Body.Default` fg |
| `alternateBase` | `Content[TrackDetail].ListAlternating.Default` bg |
| `dimText` | `Panel.Body.Lowlight` fg |
| `highlight` | `Panel.Body.Highlight` bg |
| `highlightText` | `Panel.Body.Highlight` fg |
| `button` | `Controls.Button.Default` bg |
| `buttonText` | `Controls.Button.Default` fg |
| `header` | `Panel.Header.Default` bg; `Panel.ChildHeader.Default` bg |
| `headerText` | `Panel.Header.Default` fg; `Panel.ChildHeader.Default` fg |
| `menuBar` | `MenuBar.Default` bg |
| `menuBarText` | `MenuBar.Default` fg |
| `menu` | `Menu.DropDown.Default` bg |
| `menuText` | `Menu.DropDown.Default` fg |
| `menuHighlight` | `Menu.DropDown.Highlight` bg |
| `menuHighlightText` | `Menu.DropDown.HighlightText` fg; `Panel.Body.Highlight` fg |
| `statusBar` | `Panel.StatusBar.Default` bg |
| `statusBarText` | `Panel.StatusBar.Default` fg |
| `player` | `Player.Background` bg; `PlayerFlat.Background` bg; `PlayerSidebar.Background` bg |
| `playerText` | `Player.Background` fg; `PlayerFlat.Background` fg; `PlayerSidebar.Background` fg |
| `tab` | `Controls.TabHeader.Default` bg |
| `tabText` | `Controls.TabHeader.Default` fg |
| `tabSelected` | `Controls.TabHeader.Highlight` bg |
| `tabSelectedText` | `Controls.TabHeader.Highlight` fg |

`skinColourText(colour)` is `r,g,b`, or `r,g,b,a` when the alpha is not
255.

## SK-SHEET: the style sheet

`skinStyleSheet(colours)` gives these lines, each only when every name it
uses is there, in this order, joined by line feeds, with no line feed at
the end. A colour is written `rgb(r,g,b)`, or `rgba(r,g,b,a)` when its
alpha is not 255.

| Needs | Line |
|---|---|
| `header`, `headerText` | `QHeaderView::section { background-color: HEADER; color: HEADERTEXT; }` |
| `menuBar`, `menuBarText` | `QMenuBar { background-color: MENUBAR; color: MENUBARTEXT; }` |
| `menu`, `menuText` | `QMenu { background-color: MENU; color: MENUTEXT; }` |
| `menuHighlight`, `menuHighlightText` | `QMenu::item:selected { background-color: MENUHIGHLIGHT; color: MENUHIGHLIGHTTEXT; }` |
| `statusBar`, `statusBarText` | `QStatusBar { background-color: STATUSBAR; color: STATUSBARTEXT; }` |
| `player`, `playerText` | `#playerBar { background-color: PLAYER; color: PLAYERTEXT; } #playerBar QLabel { color: PLAYERTEXT; }` |
| `tab`, `tabText` | `QTabBar::tab { background-color: TAB; color: TABTEXT; padding: 4px 10px; }` |
| `tabSelected`, `tabSelectedText` | `QTabBar::tab:selected { background-color: TABSELECTED; color: TABSELECTEDTEXT; }` |

The other names of SK-COLOURS are not in the sheet: they go into the
palette (SK-APPLY).

## SK-APPLY: giving them to the application

`SkinApplier(application, settings)` remembers the palette the
application has at that moment (linuxBee's own look).

`apply(file)`:

1. reads the file. If it cannot be read, or is not ok, the answer is
   false, `lastError()` says why, and nothing changes.
2. If the skin names a file in `dependsOn`, that file is looked for in
   the same folder. Read and ok: the skin is laid over it (SK-LAYER) and
   `basis()` is `read`. Missing, unreadable or not ok (a compiled skin):
   the skin is used alone, over linuxBee's own look, and `basis()` is
   `unreadable`. What that base depends on in turn is not followed. With
   no `dependsOn`, `basis()` is `none`.
3. The application's palette becomes the remembered one with these roles
   replaced, for the names the skin has: `window` Window, `windowText`
   WindowText, `base` Base, `alternateBase` AlternateBase, `text` Text,
   `dimText` PlaceholderText, `highlight` Highlight, `highlightText`
   HighlightedText, `button` Button, `buttonText` ButtonText.
4. The application's style sheet becomes `skinStyleSheet` of the skin's
   colours.
5. `appearance/skin` is stored at once: the file's absolute path.
   `currentFile()` is that path. `changed` is emitted.

`clear()` gives back the remembered palette and an empty style sheet,
removes `appearance/skin`, makes `currentFile()` and `basis()` empty and
emits `changed`; when no skin is applied it does nothing.

At construction, a path stored in `appearance/skin` is applied. If that
fails, linuxBee keeps its own look, `currentFile()` is empty,
`lastError()` says why, and the stored path is left as it is (the file
may be on a drive that is not there today).

## SK-CLI

```
linuxbeectl skin show FILE
```

Prints, for the file alone (what it depends on is not read):

- `depends on: NAME`, when it names one;
- one line per colour of SK-COLOURS it has, in the order of the names'
  code points: `NAME = r,g,b`;
- one line `problem: TEXT` per problem;
- last, `N colours, M elements, P problems`.

A file that cannot be read or is not ok: `failed: FILE: REASON` on
standard error, nothing on standard output, exit 3. Exit 2 for a usage
mistake: no or an unknown subcommand, no file or more than one.

## SK-MAIN

`src/app/main.cpp` additionally:

- creates a `SkinApplier` over the application and the settings, before
  the main window is made;
- prints, for `--parts`, one more line directly after those of the earlier
  plans (the last of which begins `main-view=`): `skin=none`, or `skin=`
  and the file's name without its folder. Later plans add lines of their
  own below it.

## Not in this version

- A place in the window to choose a skin. Until there is one,
  `appearance/skin` in `linuxbee.ini` is set by hand to the file's path.
- Compiled skins (`.xmlc`), and so the base of most of the skins a
  MusicBee install ships (F-DEP): of those, only what the small file on
  top says is used.
- The skins' pictures and icons, and their numbers and switches
  (margins, gaps, which buttons are drawn): they are read (`text`) and
  not used.
- Gradients (`bg2`, `fg2`), borders, scroll bars, buttons' and inputs'
  own colours beyond the palette, and every element not in SK-COLOURS.
- A separate colour set per panel.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| SK-READ | Whether an element given twice in one skin adds to the first or replaces it whole. |
| SK-READ | Whether the fourth number of a colour is its alpha. |
| SK-COLOURS | For each name, whether the element chosen is the one that colours that part in MusicBee: judged by eye, one of the nine complete skins beside the same skin in linuxBee. |
| SK-APPLY | Whether a dependent skin's base may itself depend on another. |
