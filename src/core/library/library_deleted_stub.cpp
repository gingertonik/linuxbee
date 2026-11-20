// A stub: the bodies belong in library.cpp. Remove this file when they are there.
#include "library.h"

namespace linuxbee {

int Library::markDeleted(const QList<qint64> &, const QDateTime &)
{
    return 0;
}

int Library::expireDeleted(const QDateTime &)
{
    return 0;
}

}
