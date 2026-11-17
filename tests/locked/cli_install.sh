#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Installs the build into a throwaway prefix and checks what arrived.
# usage: cli_install.sh /path/to/linuxbeectl
set -u
CTL=${1:?usage: cli_install.sh /path/to/linuxbeectl}
BUILD=$(cd "$(dirname "$CTL")/../.." && pwd)
P=$(mktemp -d)
trap 'rm -rf "$P"' EXIT
fail=0
ID=io.github.gingertonik.linuxbee

no() { echo "FAIL: $1"; fail=$((fail + 1)); }

if ! cmake --install "$BUILD" --prefix "$P" > "$P/install.log" 2>&1; then
    no "cmake --install failed: $(tail -n 3 "$P/install.log" | tr '\n' ' ')"
fi
for f in bin/linuxbee bin/linuxbeectl \
    "share/applications/$ID.desktop" "share/metainfo/$ID.metainfo.xml" \
    "share/icons/hicolor/scalable/apps/$ID.svg" share/licenses/linuxbee/LICENSE; do
    [ -f "$P/$f" ] || no "not installed: $f"
done
[ -x "$P/bin/linuxbee" ] || no "bin/linuxbee is not executable"
[ -x "$P/bin/linuxbeectl" ] || no "bin/linuxbeectl is not executable"

# The installed programs run from where they landed.
got=$("$P/bin/linuxbeectl" --version 2> /dev/null)
case "$got" in linuxbeectl\ *) ;; *) no "installed linuxbeectl --version printed: $got" ;; esac
got=$(QT_QPA_PLATFORM=offscreen "$P/bin/linuxbee" --version 2> /dev/null)
case "$got" in linuxBee\ *) ;; *) no "installed linuxbee --version printed: $got" ;; esac

# Nothing from the build machine leaks into what is installed.
if grep -rIl -e "$BUILD" -e "$(dirname "$BUILD")" "$P/share" > /dev/null 2>&1; then
    no "an installed data file names the build directory"
fi

D="$P/share/applications/$ID.desktop"
if [ -f "$D" ]; then
    # B-DESKTOP: it starts the program and claims no files, until opening files is built.
    grep -qx 'Exec=linuxbee' "$D" || no "desktop entry: Exec"
    grep -q '%[fFuU]' "$D" && no "desktop entry: a field code"
    grep -qx "Icon=$ID" "$D" || no "desktop entry: Icon"
    grep -qx 'Name=linuxBee' "$D" || no "desktop entry: Name"
    grep -q '^MimeType=' "$D" && no "desktop entry: MimeType"
    grep -q '^Categories=.*AudioVideo;.*Audio;' "$D" || no "desktop entry: Categories"
    if command -v desktop-file-validate > /dev/null 2>&1; then
        out=$(desktop-file-validate "$D" 2>&1) || no "desktop-file-validate: $out"
    fi
fi
M="$P/share/metainfo/$ID.metainfo.xml"
if [ -f "$M" ]; then
    grep -q "<id>$ID</id>" "$M" || no "metainfo: id"
    grep -q '<project_license>GPL-3.0-or-later</project_license>' "$M" || no "metainfo: project licence"
    grep -q "<launchable type=\"desktop-id\">$ID.desktop</launchable>" "$M" || no "metainfo: launchable"
    if command -v appstreamcli > /dev/null 2>&1; then
        out=$(appstreamcli validate --no-net "$M" 2>&1) || no "appstreamcli validate: $(echo "$out" | head -n 6 | tr '\n' ' ')"
    fi
fi
I="$P/share/icons/hicolor/scalable/apps/$ID.svg"
if [ -f "$I" ]; then
    head -c 300 "$I" | grep -q '<svg' || no "the icon is not an SVG"
fi

echo "cli_install: $fail failed"
[ "$fail" -eq 0 ]
