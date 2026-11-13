#pragma once
#include <QString>

namespace linuxbee::fieldformat {

// F-TIME: rounds to the nearest second (halves up; a negative length counts
// as 0). Prints m:ss under an hour and h:mm:ss from an hour up.
QString duration(qint64 milliseconds);

// F-KIND: the Kind text for a format name such as "mp3". A format with no
// entry in the table gives its upper-case name then " audio file".
QString kind(const QString &format);

// F-AUDIO: kilohertz with no trailing zeros, then " kHz": 44100 gives
// "44.1 kHz", 48000 gives "48 kHz". The caller handles 0.
QString sampleRate(int hertz);

// F-DISCTRACK: a text made only of ASCII digits is read as a whole number
// (leading zeros dropped) and padded with zeros on the left to `width`; a
// longer number is not cut. Any other text is returned as written.
QString padWholeNumber(const QString &text, int width);

// F-YEAR: the first run of exactly four ASCII digits in `text`, or empty when
// there is none. A run of five or more digits does not count.
QString fourDigitYear(const QString &text);

}
