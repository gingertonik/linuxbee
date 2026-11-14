#include "sort/fieldsort.h"

// STUB (review-sort-prepared). The builder gives these real bodies in
// fieldsort.cpp and removes this file.
namespace linuxbee {

PreparedValue prepareFieldValue(const QString &, const QString &, const SortOptions &)
{
    return { };
}

int comparePrepared(const PreparedValue &, const PreparedValue &)
{
    return 0;
}

qint64 fieldValuesPrepared()
{
    return 0;
}

qint64 fieldValueComparisons()
{
    return 0;
}

}
