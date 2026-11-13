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

// The canonical spelling of a computed field name, or empty when it is not one.
QString canonicalComputedName(const QString &fieldName)
{
    for (const QString &name : computedFieldNames()) {
        if (name.compare(fieldName, Qt::CaseInsensitive) == 0)
            return name;
    }
    return { };
}

// F-FILE: the path split at its last '/'.
struct PathParts {
    QString filename;
    QString extension;
    QString folder;
};

PathParts splitPath(const QString &path)
{
    PathParts parts;
    if (path.isEmpty())
        return parts;

    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    parts.filename = path.mid(slash + 1);
    // A dot at index 0 is a leading dot (".hidden"), not an extension.
    const qsizetype dot = parts.filename.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
        parts.extension = parts.filename.mid(dot + 1);

    if (slash == 0)
        parts.folder = QStringLiteral("/");
    else if (slash > 0)
        parts.folder = path.left(slash);
    return parts;
}

// The value of one computed field. Fields whose rule is not yet implemented
// are blank.
QString computedValue(const TrackTags &tags, const QString &path, const QString &name)
{
    const PathParts parts = splitPath(path);
    // F-AUDIO: tags that did not come from a file have blank audio fields.
    const bool fromFile = !tags.format.isEmpty();

    if (name == QLatin1String("Filename"))
        return parts.filename;
    if (name == QLatin1String(".Ext"))
        return parts.extension;
    if (name == QLatin1String("Path"))
        return parts.folder;
    if (name == QLatin1String("URL"))
        return path;
    if (name == QLatin1String("Kind"))
        return fromFile ? fieldformat::kind(tags.format) : QString();
    if (name == QLatin1String("Time"))
        return fromFile ? fieldformat::duration(tags.audio.durationMs) : QString();
    if (name == QLatin1String("Bitrate")) {
        if (!fromFile || tags.audio.bitrateKbps == 0)
            return QString();
        return QString::number(tags.audio.bitrateKbps) + QLatin1Char('k');
    }
    if (name == QLatin1String("Sample Rate")) {
        if (!fromFile || tags.audio.sampleRateHz == 0)
            return QString();
        return fieldformat::sampleRate(tags.audio.sampleRateHz);
    }
    if (name == QLatin1String("Channels")) {
        if (!fromFile || tags.audio.channels == 0)
            return QString();
        if (tags.audio.channels == 1)
            return QStringLiteral("Mono");
        if (tags.audio.channels == 2)
            return QStringLiteral("Stereo");
        return QString::number(tags.audio.channels);
    }
    return QString();
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

    const QString name = canonicalComputedName(fieldName);
    if (name.isEmpty())
        return std::nullopt;
    return computedValue(m_tags, m_filePath, name);
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
