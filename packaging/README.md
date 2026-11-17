# Packaging

`cmake --install <build dir> --prefix <prefix>` installs:

| File | Where (under the prefix) |
|---|---|
| `linuxbee`, `linuxbeectl` | `bin/` |
| the desktop entry | `share/applications/` |
| the AppStream metadata | `share/metainfo/` |
| the icon | `share/icons/hicolor/scalable/apps/` |
| the licence | `share/licenses/linuxbee/` |

The application id is `io.github.gingertonik.linuxbee`; the three data
files are named after it.

## The installer

`install.sh` puts the same files under `~/.local`: the two programs in
`~/.local/bin/`, and the desktop entry, the AppStream metadata, the icon and
the licence under `~/.local/share/`. `--dry-run` prints every path it would
write before it writes any of them. `--uninstall` removes the files the
installer wrote, from a manifest it keeps, and leaves settings, the library,
playlists and music files alone.

At run time linuxBee needs Qt 6 (Core, Sql, Widgets, Multimedia), TagLib 2
and the `ffmpeg` program. Converting to a format needs the encoder for it
in that `ffmpeg`.

A package for a distribution sets `CMAKE_INSTALL_PREFIX=/usr` and
`DESTDIR`; nothing else is special. Building needs CMake 3.25 or later,
Ninja and a C++20 compiler.

The icon is a placeholder drawn for this project.
