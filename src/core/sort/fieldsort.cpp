#include "sort/fieldsort.h"

// Stub. Plan 11 replaces every body in this file.
namespace linuxbee {

FieldKind fieldKind(const QString &fieldName)
{
    Q_UNUSED(fieldName);
    return FieldKind::Text;
}

int compareFieldValues(
    const QString &fieldName, const QString &a, const QString &b, const SortOptions &options)
{
    Q_UNUSED(fieldName);
    Q_UNUSED(a);
    Q_UNUSED(b);
    Q_UNUSED(options);
    return 0;
}

QString formatTrackSummary(int count, qint64 totalBytes, qint64 totalMs)
{
    Q_UNUSED(count);
    Q_UNUSED(totalBytes);
    Q_UNUSED(totalMs);
    return { };
}

}
