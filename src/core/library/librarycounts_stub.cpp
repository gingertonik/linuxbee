#include "library/library.h"

// STUB (review-counts). The builder gives these two real bodies in the
// library's own files and removes this file.
namespace linuxbee {

QMap<QString, int> Library::categoryCounts() const
{
    return { };
}

qint64 Library::tracksRead() const
{
    return 0;
}

}
