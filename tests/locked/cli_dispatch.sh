#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_dispatch.sh /path/to/linuxbeectl
# What linuxbeectl itself does, apart from any one command.
set -u
bin="$1"
fails=0
ok() { echo "PASS $1"; }
bad() { echo "FAIL $1: $2"; fails=$((fails + 1)); }

out=$("$bin" --version 2>/dev/null); rc=$?
# expected: CMakeLists.txt, project(linuxbee VERSION X.Y.Z ...), read here so that a version commit
# (tools/bump-version.sh) changes no locked file. Three numbers, and what the program says.
want=$(sed -n 's/^project(linuxbee VERSION \([0-9]*\.[0-9]*\.[0-9]*\) .*/\1/p' "$(dirname "$0")/../../CMakeLists.txt")
if [ "$rc" = 0 ] && [ -n "$want" ] && [ "$out" = "linuxbeectl $want" ]; then ok version; else bad version "rc=$rc out=[$out] want=[$want]"; fi

out=$("$bin" --help 2>/dev/null); rc=$?
if [ "$rc" = 0 ]; then ok help-exit; else bad help-exit "rc=$rc"; fi
case "$out" in *"  template "*) ok help-lists-template ;; *) bad help-lists-template "[$out]" ;; esac

out=$("$bin" 2>/dev/null); rc=$?
if [ "$rc" = 2 ] && [ -z "$out" ]; then ok no-arguments; else bad no-arguments "rc=$rc out=[$out]"; fi

out=$("$bin" frobnicate 2>/dev/null); rc=$?
if [ "$rc" = 2 ] && [ -z "$out" ]; then ok unknown-command; else bad unknown-command "rc=$rc out=[$out]"; fi
err=$("$bin" frobnicate 2>&1 >/dev/null)
case "$err" in *frobnicate*) ok unknown-command-named ;; *) bad unknown-command-named "[$err]" ;; esac

echo "cli_dispatch: $fails failed"
[ "$fails" -eq 0 ]
