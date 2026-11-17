#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Every command in the menus either does something or is on the list of those that do not yet.
# Usage: cli_catalogue.sh /path/to/linuxbeectl     (LINUXBEE_APP is the application)
# Rules: docs/spec/main-window.md (H-NOTYET). The catalogue is read from its own (locked) source file; what is
# implemented is asked of the program itself, in folders of this test's own.
set -u
app="${LINUXBEE_APP:?}"
SRC=$(cd "$(dirname "$0")/../.." && pwd)
LIST="$SRC/tests/locked/catalogue-not-yet.tsv"
fails=0
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
work=$(mktemp -d) || exit 2
trap 'rm -rf "$work"' EXIT
tab=$(printf '\t')

sed -n 's/^ *{ QStringLiteral("\([^"]*\)").*/\1/p' "$SRC/src/ui/actions/actioncatalogue.cpp" > "$work/catalogue"
[ "$(wc -l < "$work/catalogue")" -ge 60 ] || fail "the catalogue could not be read from its source ($(wc -l < "$work/catalogue") ids)"
[ "$(grep -c 'QStringLiteral("[a-z][A-Za-z0-9.]*"), QStringLiteral' "$SRC/src/ui/actions/actioncatalogue.cpp")" = "$(wc -l < "$work/catalogue")" ] \
    || fail "an entry of the catalogue was not read (its line is not in the usual form)"
XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache" QT_QPA_PLATFORM=offscreen \
    timeout 30 "$app" --commands > "$work/implemented" 2> /dev/null || fail "linuxbee --commands failed"
[ -s "$work/implemented" ] || fail "linuxbee --commands printed nothing"
grep -v '^#' "$LIST" | grep -v '^$' > "$work/lines"
cut -f1 "$work/lines" > "$work/notyet"

# The list is well made: an id, a tab, a reason; each id once; in the catalogue's order.
grep -vnE "^[a-z][A-Za-z0-9.]*${tab}[^${tab}]{8,}\$" "$work/lines" | head -n 3 | while IFS= read -r l; do echo "FAIL not 'id<tab>reason': $l"; done
[ -z "$(grep -vE "^[a-z][A-Za-z0-9.]*${tab}[^${tab}]{8,}\$" "$work/lines")" ] || fails=$((fails + 1))
[ -z "$(sort "$work/notyet" | uniq -d)" ] || fail "listed twice: $(sort "$work/notyet" | uniq -d | tr '\n' ' ')"
grep -Fxf "$work/notyet" "$work/catalogue" | cmp -s - "$work/notyet" || fail "the list is not in the catalogue's order (or names a command the catalogue has not)"

sort "$work/catalogue" > "$work/c"; sort "$work/implemented" > "$work/i"; sort "$work/notyet" > "$work/n"
x=$(comm -13 "$work/c" "$work/n" | tr '\n' ' ');   [ -z "$x" ] || fail "on the list and not in the catalogue: $x"
x=$(comm -13 "$work/c" "$work/i" | tr '\n' ' ');   [ -z "$x" ] || fail "implemented and not in the catalogue: $x"
x=$(comm -12 "$work/i" "$work/n" | tr '\n' ' ');   [ -z "$x" ] || fail "does something now, and is still on the list of those that do not (take its line out): $x"
sort -u "$work/i" "$work/n" > "$work/u"
x=$(comm -23 "$work/c" "$work/u" | tr '\n' ' ');   [ -z "$x" ] || fail "in the menus, does nothing, and is not on the list: $x"

echo "cli_catalogue: $fails failed ($(wc -l < "$work/implemented") of $(wc -l < "$work/catalogue") commands do something; $(wc -l < "$work/notyet") are listed as not yet)"
[ "$fails" -eq 0 ]
