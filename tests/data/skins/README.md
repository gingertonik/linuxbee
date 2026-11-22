# Fixtures for the skin tests

All four files are invented for linuxBee, in the shape the sources of
docs/spec/skins.md show (the wiki, the forum's examples, and the
community skin C-KRY). None is a copy of a published skin. A colour of
four numbers is written alpha first, as C-KRY writes it.

- `Plain Grey.xml`: a complete skin, with a variables block, the three
  sections, and some values that cannot be read.
- `Red Accent.xml`: a small skin laid over `Plain Grey.xml`.
- `Green Accent.xml`: the same laid over `Compiled Base.xmlc`.
- `Compiled Base.xmlc`: twenty bytes that are not a skin. It stands for
  a compiled skin, which linuxBee does not read.
