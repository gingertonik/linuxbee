#pragma once
#include <QString>

// Private to src/core/search/. Not part of the interface.
namespace linuxbee {

// S-FOLD without the U+001F rule: the fold of text that holds no U+001F of its own, or one
// whose U+001F the caller wants kept (the joined value of S-CACHE is cut at them). Equal to
// foldForSearch for any text without U+001F.
QString foldKeepingUnit(const QString &text);

}
