#include "trackfields.h"

// Stub. Plan 04b replaces every body in this file.
namespace linuxbee {

TrackFieldSource::TrackFieldSource(
    const TrackTags &tags, const QString &filePath, const TrackFieldOptions &options)
    : m_tags(tags)
    , m_filePath(filePath)
    , m_options(options)
{
}

std::optional<QString> TrackFieldSource::value(const QString &fieldName) const
{
    Q_UNUSED(fieldName);
    Q_UNUSED(m_tags);
    return std::nullopt;
}

QStringList trackFieldNames()
{
    return { };
}

QString formatDuration(int milliseconds)
{
    Q_UNUSED(milliseconds);
    return { };
}

}
