// fix-sr4a stub: the builder removes this file and makes these real.
#include "search.h"

namespace linuxbee {

QString foldForSearch(const QString &text, bool)
{
    return foldForSearch(text);
}

bool SearchQuery::operator==(const SearchQuery &other) const
{
    return canonical() == other.canonical();
}

}
