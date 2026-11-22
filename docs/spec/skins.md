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

The sources are community-published only (the owner's ruling of
2026-11-20): the wiki, the forum, and one community skin. No skin
bundled with MusicBee, no compiled `.xmlc` file and nothing of an
install is read. **Inferred** means worked out from these, not
confirmed in the running program.

| Tag | Source |
|---|---|
| W-SE | MusicBee wiki, "Skinning Elements" (about 410 element names with what each accepts and where it applies; matches version 2.3): https://musicbee.fandom.com/wiki/Skinning_Elements |
| W-SG | MusicBee wiki, "Skinning Guide": a skin is lines of `<element id="..." bg="r,g,b" bg2="..." fg="..." bdr="..." />`; bg is the background, bg2 a gradient's second colour, fg usually the text, bdr the border: https://musicbee.fandom.com/wiki/Skinning_Guide |
| F-DEP | MusicBee forum, "Dependent skins" (topic 2797, 2011): `<root dependsOn="...">` lays a small skin over another; the example puts its element in `<colours id="Default">`. https://getmusicbee.com/forum/index.php?topic=2797.0 |
| F-MOD | MusicBee forum, "How to make modifications to a skin that you like" (topic 29815, hiccup, 2019, edited 2023): a small `.xml` skin with `dependsOn` an `.xmlc` skin overrides each element it names ("Any element specified in your xml file will override"); "elements under <colours> are defined by RGB values, elements under <images> are defined by base64 encoded images". https://getmusicbee.com/forum/index.php?topic=29815.0 |
| F-VAR | MusicBee forum, topic 23605 (redwing, 2017): "the variables section", lines such as `theme = "93,7,42"`. https://getmusicbee.com/forum/index.php?topic=23605.0 |
| F-FLAT | MusicBee forum, the skin FlatUI (topic 18475, 2016-2017): "on top you will find the variables section"; an alpha value is accepted on some attributes (the developer, 2017-05-15). https://getmusicbee.com/forum/index.php?topic=18475.0 |
| C-KRY | The community skin "Kryptonite Dark" 1.2 by Ingo_Igel (forum topic 35252; https://github.com/Ingo-Igel/Kryptonite_Dark, commit 8c40bf7; Unlicense). Read for its shapes; none of it is copied. It shows: the element `<variables>` with lines `name="r,g,b"`; the sections `<colours id="Default">`, `<settings>` (elements holding text, such as `true`) and `<images id="Default">`; `bg2` and `fg2`; and colours of four numbers with the alpha first: `gray_3="43,43,43"` beside `"128,43,43,43"`, the same grey half clear. |
| SK-… | A linuxBee decision. Which element colours which part of linuxBee is always one (inferred from the element's name and W-SE's description). |

Checked again on 2026-11-20. Each wiki page, the MusicBee version it says it
describes, and when it was last edited: "Skinning Elements" (2.3.5139;
2018-11-07); "Skinning Guide" (no version given; 2019-02-09).

## SK-READ: reading a skin

`readSkin(bytes)`:

- The bytes are UTF-8 XML, with or without a byte order mark. It is not
  ok, with a reason, when there is no root element `root` or it is never
  closed. (A compiled `.xmlc` skin is not text and so is not ok.)
- `dependsOn`: the root's attribute of that name, trimmed.
- **A colour** is three or four whole numbers from 0 to 255 separated by
  commas, spaces allowed around each: red, green and blue (the alpha
  255); or, of four, alpha, red, green and blue (C-KRY). (Until 0.9 the
  fourth number was read as the alpha: a skin's half-clear grey
  `128,43,43,43` came out red. No colour was ever stored by linuxBee,
  so nothing it saved is read differently.)
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

`skinColourText(colour)` is the colour as a skin writes it: `r,g,b`, or
`a,r,g,b` when the alpha is not 255. Read again, it is the same colour.

## SK-SHEET: the style sheet

`skinStyleSheet(colours)` gives these lines, each only when every name it
uses is there, in this order, joined by line feeds, with no line feed at
the end. A colour is written `rgb(r,g,b)`, or `rgba(r,g,b,a)` when its
alpha is not 255: Qt's order, alpha last, whatever order the skin
used.

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

## SC-LIST: the skins on offer

`SkinChoosing(window, actions, applier, folder, hooks)` in
`src/ui/skins/skinchoosing.h` is the place in the window to choose a
skin. `folder` is the skins folder.

`skinFiles()` is every file whose name ends in `.xml` (in any case)
directly in the skins folder or in a folder directly in it, as an
absolute path. Folders further down are not looked at (MusicBee's wiki,
"Skins": folders inside the sub-folders are not allowed). Names that
begin with a dot, files and folders alike, are left out, and so is a
folder named like a skin. Compiled skins (`.xmlc`) are not offered:
linuxBee cannot read them.

The order is by the file's name without its ending, case ignored; of
two with one name, the one directly in the skins folder first, then by
path.

Speed: listing reads no file. A file that is not a skin at all is
listed; it is refused when it is chosen. A skins folder that does not
exist gives an empty list and is not made.

## SC-CHOOSE

`choose()`:

- with no skin on offer it asks nothing, says
  `There are no skins yet. Add one with View > Skins > Add Skin…` and
  returns false;
- else it asks `chooseSkin(skinFiles(), applier->currentFile())` once. No
  answer: false, nothing changed, the message left as it was;
- the answer is given to `SkinApplier::apply`, once. Applied: true, and
  the message `Skin: NAME`. Refused: false, nothing changed (SK-APPLY),
  and the message `Could not use the skin NAME: REASON`, REASON being the
  applier's `lastError()`.

NAME is the file's name without its folder and its ending.

## SC-NONE

`none()` goes back to linuxBee's own look (`SkinApplier::clear`), says
`Skin: none` and returns true. With no skin in use it does nothing and
returns false.

## SC-ADD

`add()` takes a skin file from anywhere into the skins folder and uses
it. It asks `chooseFile()`; no answer: false, nothing changed, the
message left as it was. It refuses, with false, nothing copied, no
folder made, the look untouched and the message
`Could not add the skin NAME: REASON`:

| When | REASON |
|---|---|
| the name ends in `.xmlc` (any case) | `linuxBee cannot read compiled skins (.xmlc)` |
| the name does not end in `.xml` (any case) | `a skin file ends in .xml` |
| the file cannot be read, or is not a skin (SK-READ) | what the reading said |
| the skins folder, or a folder directly in it, has a file of that name (case ignored), the file itself included | `there is a skin of that name already` |
| the copy failed | `the file could not be copied` |

Otherwise the skins folder is made if it is missing and the file is
copied into it under its own name (copied: the original stays). When
the skin names a file it is laid over (`dependsOn`), and a file of that
name is beside the original, and the skins folder has none of that name,
that file is copied too, whether linuxBee can read it or not. Nothing
else is copied. Then the skin is used as SC-CHOOSE uses it, and the
message is `Added the skin NAME`.

## SC-MESSAGE

Every message goes to the window's status bar and is kept as
`lastMessage()`.

## SC-WIRING

Constructing the object implements the commands `view.skins.choose`
(`choose()`), `view.skins.add` (`add()`) and `view.skins.none`
(`none()`), which the catalogue lists under View > Skins as
`Choose Skin…`, `Add Skin…` and `No Skin`. Destroying it frees them.

Without hooks: `chooseSkin` is a choice among the names (NAME as above,
with the folder's name before it for a skin one folder down), the one in
use chosen to start with; `chooseFile` is the window's file dialog.

## SC-MAIN

`src/app/main.cpp` additionally creates a `SkinChoosing` over the window,
the registry and the `SkinApplier` of SK-MAIN, with the folder `skins` in
the data folder and the default hooks. Starting makes no folder.
(`--commands` then lists the three commands; `--parts` is unchanged.)

## Not in this version

- A list of skins in the View menu itself, a skin chooser in the
  preferences, `Random Skin` and `Skin Window Borders` (the owner's
  menu, seen): choosing is a question in a dialog (SC-CHOOSE).
- Compiled skins (`.xmlc`), and so the base of any skin laid over one
  (F-MOD): of those, only what a small `.xml` over it says is used.
- The skins' pictures and icons, and their numbers and switches
  (margins, gaps, which buttons are drawn): they are read (`text`) and
  not used.
- Gradients (`bg2`, `fg2`), borders, scroll bars, buttons' and inputs'
  own colours beyond the palette, and every element not in SK-COLOURS.
- A separate colour set per panel.

## Open parity questions

| Rule | Question |
|---|---|
| SC-LIST | Whether MusicBee's list shows a skin by its file's name or by a name inside the file, and how it orders folders. |
| SC-ADD | Whether MusicBee's `Add Skin...` copies the file, takes what it is laid over along, and switches to the skin at once (all three taken as yes). |
| SC-MAIN | Where the owner's skin files are to live; MusicBee's own `Skins` folder is never read or written by linuxBee. |

| Rule | Question for someone with MusicBee |
|---|---|
| SK-READ | Whether an element given twice in one skin adds to the first or replaces it whole. |
| SK-COLOURS | For each name, whether the element chosen is the one that colours that part in MusicBee: judged by eye, a complete community skin beside the same skin in linuxBee. |
| SK-APPLY | Whether a dependent skin's base may itself depend on another. |
