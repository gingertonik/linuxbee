# linuxBee

A music manager and player for Linux, built for people who are moving from
MusicBee on Windows and want their habits to keep working: the same kind of
tag editor, the same file-naming templates, the same organise and convert
tools, and their library data carried across.

linuxBee is an independent project. It is not affiliated with or endorsed by
MusicBee or its author, and it contains no MusicBee code or artwork. How
that is kept true is written down in [docs/CLEAN-ROOM.md](docs/CLEAN-ROOM.md).

Status: early. The headless core is being built first, one tested piece at a
time. There is no window yet.

## Build

Needs CMake 3.25+, Ninja, a C++20 compiler, Qt 6.5+ and clang-format.

```sh
sh tools/check.sh
```

The last line it prints is `rc=0` when the tree lints, builds and passes
every test.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
