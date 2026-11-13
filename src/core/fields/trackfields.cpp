#include "trackfields.h"

#include "format.h"

namespace linuxbee {

namespace {

// F-NAMES: the computed fields, in the order of the spec's table.
const QStringList &computedFieldNames()
{
    static const QStringList names = {
        QStringLiteral("Disc-Track#"),
        QStringLiteral("Year (yyyy)"),
        QStringLiteral("Decade"),
        QStringLiteral("Album [Year]"),
        QStringLiteral("Display Artist"),
        QStringLiteral("Artist & Title"),
        QStringLiteral("ArtistAndTitle"),
        QStringLiteral("Filename"),
        QStringLiteral(".Ext"),
        QStringLiteral("Path"),
        QStringLiteral("URL"),
        QStringLiteral("Kind"),
        QStringLiteral("Time"),
        QStringLiteral("Bitrate"),
        QStringLiteral("Sample Rate"),
        QStringLiteral("Channels"),
    };
    return names;
}

}

TrackFieldSource::TrackFieldSource(
    const TrackTags &tags, const QString &filePath, const TrackFieldOptions &options)
    : m_tags(tags)
    , m_filePath(filePath)
    , m_options(options)
{
}

std::optional<QString> TrackFieldSource::value(const QString &fieldName) const
{
    // F-TAGS: the catalogue fields come from the tag source (R-SOURCE).
    if (const std::optional<QString> tagValue = TrackTagSource(m_tags).value(fieldName))
        return tagValue;

    for (const QString &name : computedFieldNames()) {
        if (name.compare(fieldName, Qt::CaseInsensitive) == 0) {
            // Known but not computed yet: blank until its rule is implemented.
            return QString();
        }
    }
    return std::nullopt;
}

QStringList trackFieldNames()
{
    return tagFieldNames() + computedFieldNames();
}

QString formatDuration(int milliseconds)
{
    return fieldformat::duration(milliseconds);
}

}
