#include "tagtools.h"

// Stub. Plan 09 replaces every body in this file.
namespace linuxbee {

QString validateInferPattern(const QString &pattern)
{
    Q_UNUSED(pattern);
    return { };
}

InferResult inferFromPath(const QString &path, const QString &pattern)
{
    Q_UNUSED(path);
    Q_UNUSED(pattern);
    return { };
}

TagChanges inferTags(
    const TrackTags &tags, const QString &path, const QString &pattern, bool blankFieldsOnly)
{
    Q_UNUSED(tags);
    Q_UNUSED(path);
    Q_UNUSED(pattern);
    Q_UNUSED(blankFieldsOnly);
    return { };
}

}
