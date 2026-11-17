#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# linuxbeectl says why it did not save the tags of a file that has another name, and changes neither.
# Usage: cli_tags_linked.sh /path/to/linuxbeectl
# Rules: docs/spec/tag-safe-save.md (WS-LINK, Command line); docs/decisions/hard-linked-files.md.
set -u
ctl=$1
data="${LINUXBEE_TEST_DATA:?}/tags"
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
work=$(mktemp -d "${TMPDIR:-/tmp}/lb-linked.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
mkdir "$work/library" "$work/seeding"
for name in basic.mp3 basic.flac; do
    f="$work/library/$name"; cp "$data/$name" "$f"; chmod u+w "$f"; ln "$f" "$work/seeding/$name"
    before=$(sha256sum < "$f")
    out=$("$ctl" tags write --set 'Title=Linked' "$f" 2> "$work/err"); rc=$?
    [ "$rc" = 3 ] && pass "$name-status" || fail "$name-status: exit $rc"
    [ -z "$out" ] && pass "$name-says-nothing-on-stdout" || fail "$name-says-nothing-on-stdout: [$out]"
    [ "$(wc -l < "$work/err")" = 1 ] && grep -q "^not saved: $f: .*2 names.*hard link.*cp -p FILE FILE.new && mv FILE.new FILE" "$work/err" \
        && pass "$name-says-why" || fail "$name-says-why: [$(head -n 3 "$work/err" | tr '\n' ' ')]"
    [ "$(sha256sum < "$f")" = "$before" ] && [ "$(sha256sum < "$work/seeding/$name")" = "$before" ] && [ "$(stat -c %h "$f")" = 2 ] \
        && pass "$name-both-untouched" || fail "$name-both-untouched"
    [ "$(ls -A "$work/library" | grep -c lbsave)" = 0 ] && pass "$name-nothing-left" || fail "$name-nothing-left"
    # what the line says to do, done
    cp -p "$f" "$f.new" && mv "$f.new" "$f"
    out=$("$ctl" tags write --set 'Title=Linked' "$f" 2>&1); rc=$?
    [ "$rc" = 0 ] && [ "$out" = saved ] && [ "$(sha256sum < "$work/seeding/$name")" = "$before" ] && pass "$name-then-saved" || fail "$name-then-saved: rc=$rc [$out]"
done
echo "cli_tags_linked: $fails failed"
[ "$fails" -eq 0 ]
