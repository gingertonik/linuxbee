#include "trackfields.h"

#include "format.h"

#include <QSet>

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
    // A name written exactly as the list writes it is found by one lookup in a set made once.
    static const QSet<QString> exact = [] {
        const QStringList &names = computedFieldNames();
        return QSet<QString>(names.cbegin(), names.cend());
    }();
    if (exact.contains(fieldName))
        return fieldName;

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

// F-DISCTRACK: "01-11", "07", or blank when there is no track number.
QString discTrack(const TrackTags &tags, const TrackFieldOptions &options)
{
    const QString track = tags.first(QStringLiteral("Track#")).trimmed();
    if (track.isEmpty())
        return { };

    const QString paddedTrack = fieldformat::padWholeNumber(track, options.trackDigits);
    const QString disc = tags.first(QStringLiteral("Disc#")).trimmed();
    if (disc.isEmpty())
        return paddedTrack;
    return fieldformat::padWholeNumber(disc, options.discDigits) + QLatin1Char('-') + paddedTrack;
}

// The value of one computed field. Names are the canonical spellings from
// computedFieldNames().
QString computedValue(const TrackTags &tags, const QString &path, const QString &name,
    const TrackFieldOptions &options)
{
    const PathParts parts = splitPath(path);
    // F-AUDIO: tags that did not come from a file have blank audio fields.
    const bool fromFile = !tags.format.isEmpty();

    if (name == QLatin1String("Disc-Track#"))
        return discTrack(tags, options);

    // F-YEAR and F-DECADE.
    const QString year = fieldformat::fourDigitYear(tags.first(QStringLiteral("Year")));
    if (name == QLatin1String("Year (yyyy)"))
        return year;
    if (name == QLatin1String("Decade"))
        return year.isEmpty() ? QString() : year.left(3) + QStringLiteral("0s");

    // W-SF: "<Album> [yyyy]", just the album without a year, blank without an album.
    if (name == QLatin1String("Album [Year]")) {
        const QString album = tags.joined(QStringLiteral("Album"));
        if (album.isEmpty())
            return { };
        if (year.isEmpty())
            return album;
        return album + QStringLiteral(" [") + year + QLatin1Char(']');
    }

    // F-DISPLAY: until artist roles exist, the Artist values joined.
    if (name == QLatin1String("Display Artist"))
        return tags.joined(QStringLiteral("Artist"));

    // W-SF: "Artist - Title" when both are set, otherwise the filename.
    if (name == QLatin1String("Artist & Title") || name == QLatin1String("ArtistAndTitle")) {
        const QString artist = tags.joined(QStringLiteral("Artist"));
        const QString title = tags.joined(QStringLiteral("Title"));
        if (artist.isEmpty() || title.isEmpty())
            return parts.filename;
        return artist + QStringLiteral(" - ") + title;
    }

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
    return computedValue(m_tags, m_filePath, name, m_options);
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
