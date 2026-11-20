# Building

How the tree is configured and built. Locked by `tests/locked/cli_build.sh`
and, for what is installed, `tests/locked/cli_install.sh`.

(From an outside review of the code, 2026-11-16: every build treated a
warning as an error, so a newer compiler on someone else's machine
could not build it; and a build needed Qt's test library even when no
test was wanted. The installer and the release build need neither.)

## B-PRESETS

`CMakePresets.json` has two presets, each for configuring and building:

| Preset | Build type | Folder | Tests | A warning is an error |
|---|---|---|---|---|
| `dev` | Debug | `build/dev` | built | yes |
| `release` | Release | `build/release` | not built | no |

`dev` is what `tools/check.sh` (the gate) uses, and what a builder
works in. `release` is what is installed.

## B-WERROR

The warnings (`-Wall -Wextra -Wpedantic`) are on in every build.
Whether a warning stops the build is the option `LINUXBEE_WERROR`,
which is off unless asked for. The `dev` preset asks for it.

## B-TESTS

Whether the tests are built is the option `LINUXBEE_BUILD_TESTS`,
which is off unless asked for. The `dev` preset asks for it. Only when
it is on does the build look for Qt's test library (`Qt6::Test`) and
add the `tests` folder. So a plain `cmake -S . -B build` builds the two
programs and needs no test library.

## B-DESKTOP

The desktop entry starts the program and offers it for nothing:
`Exec=linuxbee`, with no field code, and no `MimeType` line. Opening
files handed over by the desktop is not built
(background-shell.md, "Not in this version"), and an entry that claims
audio files makes the desktop offer a program that then does nothing
with them. Both come back with that feature.

## Not in this version

- A sanitizer preset (it comes with the CI package).
- Installing anywhere but through `cmake --install` (the installer
  script is its own package).

## Sources

None outside this project: this document is engineering only, and every
rule in it is a linuxBee decision. Nothing in it describes MusicBee.
