#include "library/library.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

// L-FIELDS: the seven things only the library knows about a track, written out
// as fields a naming template or a rule can read. What the file's own tags and
// path answer comes from TrackFieldSource, which this file does not touch.
namespace linuxbee {
namespace {

// The names in the order of the spec's table. libraryFieldNames() hands out
// this same list, and the lookup answers from it, so there is one list.
const QStringList &fieldNames()
{
    static const QStringList names
        = { QStringLiteral("Date Added"), QStringLiteral("Date Modified"),
              QStringLiteral("Last Played"), QStringLiteral("Play Count"),
              QStringLiteral("Skip Count"), QStringLiteral("Category"), QStringLiteral("Size") };
    return names;
}

// L-FIELDS: the date fields are "yyyy-MM-ddTHH:mm:ss", in UTC. A time that
// never happened answers blank, which is a known field with no value: Last
// Played on a track that was never played is blank, not unknown.
QString formattedUtc(const QDateTime &when)
{
    if (!when.isValid())
        return QString();
    return when.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

// L-FIELDS: Size is megabytes of 1,048,576 bytes to one decimal and " MB".
QString formattedSize(qint64 bytes)
{
    constexpr double kMebibyte = 1048576.0;
    return QStringLiteral("%1 MB").arg(QString::number(double(bytes) / kMebibyte, 'f', 1));
}

// The answer for one of the seven, by its name as fieldNames() writes it.
std::optional<QString> libraryValue(const LibraryTrack &track, const QString &name)
{
    if (name == QLatin1String("Date Added"))
        return formattedUtc(track.dateAdded);
    if (name == QLatin1String("Date Modified"))
        return formattedUtc(track.modified);
    if (name == QLatin1String("Last Played"))
        return formattedUtc(track.lastPlayed);
    if (name == QLatin1String("Play Count"))
        return QString::number(track.playCount);
    if (name == QLatin1String("Skip Count"))
        return QString::number(track.skipCount);
    if (name == QLatin1String("Category"))
        return track.category;
    if (name == QLatin1String("Size"))
        return formattedSize(track.sizeBytes);
    return std::nullopt;
}

} // namespace

QStringList libraryFieldNames()
{
    return fieldNames();
}

LibraryFieldSource::LibraryFieldSource(const LibraryTrack &track, const TrackFieldOptions &options)
    : m_track(track)
    , m_fields(track.tags, track.path, options)
{
}

std::optional<QString> LibraryFieldSource::value(const QString &fieldName) const
{
    // L-FIELDS: names ignore case. The library's seven are answered first,
    // because they are what a track's own tags cannot give; after them every
    // name TrackFieldSource answers is there unchanged, and any other name is
    // unknown.
    for (const QString &name : fieldNames()) {
        if (name.compare(fieldName, Qt::CaseInsensitive) == 0)
            return libraryValue(m_track, name);
    }
    return m_fields.value(fieldName);
}

} // namespace linuxbee
