#pragma once
#include <QString>

// Helpers for the steps of cleanComponent (docs/spec/paths.md, section 4).
// cleanComponent itself is declared in pathbuilder.h.
namespace linuxbee::paths {

// P-CTRL: remove every character below U+0020, U+007F, and every UTF-16
// surrogate that is not part of a valid pair.
QString removeControlCharacters(const QString &text);

// P-TRIM: remove spaces and dots from both ends, repeatedly, until neither
// end is a space or a dot.
QString trimSpacesAndDots(const QString &text);

// P-LENGTH: keep the longest prefix within maxBytes of UTF-8 that splits no
// character (a surrogate pair counts as one character), then P-TRIM.
QString fitToBytes(const QString &text, int maxBytes);

// P-RESERVED: true when `base` (the text before the first dot) is a reserved
// Windows device name, ignoring case: CON, PRN, AUX, NUL, COM1 to COM9,
// LPT1 to LPT9.
bool isReservedName(const QString &base);

} // namespace linuxbee::paths
