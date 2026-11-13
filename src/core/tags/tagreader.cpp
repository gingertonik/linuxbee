#include "tagreader.h"

// Stub. Plan 03 replaces every body in this file.
namespace linuxbee {

QString TrackTags::first(const QString &fieldName) const
{
    Q_UNUSED(fieldName);
    return { };
}

QString TrackTags::joined(const QString &fieldName) const
{
    Q_UNUSED(fieldName);
    return { };
}

ReadResult readTags(const QString &filePath, const ReadOptions &options)
{
    Q_UNUSED(filePath);
    Q_UNUSED(options);
    return { };
}

QString imageMimeType(const QByteArray &data)
{
    Q_UNUSED(data);
    return { };
}

TrackTagSource::TrackTagSource(const TrackTags &tags)
    : m_tags(tags)
{
}

std::optional<QString> TrackTagSource::value(const QString &fieldName) const
{
    Q_UNUSED(fieldName);
    Q_UNUSED(m_tags);
    return std::nullopt;
}

}
