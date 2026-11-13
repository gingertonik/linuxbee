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

}
