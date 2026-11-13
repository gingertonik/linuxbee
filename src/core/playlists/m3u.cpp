#include "formatdetail.h"

#include <optional>

namespace linuxbee::playlists {

namespace {

struct ExtInf {
    int seconds = -1;
    QString title;
};

// Y-READ: the tag in any case is "#EXTINF:SECONDS,TITLE". SECONDS, trimmed, is
// rounded to the nearest second, and anything negative or unreadable is -1.
// TITLE is what follows the first comma, trimmed; with no comma there is no
// title. A line that starts with '#' and is not this tag is no concern here.
std::optional<ExtInf> parseExtInf(const QString &line)
{
    static const QString kTag = QStringLiteral("#extinf:");
    if (!line.startsWith(kTag, Qt::CaseInsensitive))
        return std::nullopt;

    const QString rest = line.mid(kTag.size());
    const int comma = rest.indexOf(QLatin1Char(','));
    ExtInf info;
    bool ok = false;
    const double seconds = rest.left(comma < 0 ? rest.size() : comma).trimmed().toDouble(&ok);
    if (ok && seconds >= 0.0)
        info.seconds = qRound(seconds);
    if (comma >= 0)
        info.title = rest.mid(comma + 1).trimmed();
    return info;
}

}

QList<PlaylistEntry> readM3u(
    const QByteArray &data, PlaylistFormat format, const QString &baseFolder)
{
    QList<PlaylistEntry> entries;
    std::optional<ExtInf> pending;
    for (const QString &line : decodeLines(data, format)) {
        if (line.isEmpty())
            continue; // Y-READ: an empty line is skipped
        if (line.startsWith(QLatin1Char('#'))) {
            if (const auto info = parseExtInf(line))
                pending = *info; // an #EXTINF with no location after it is dropped
            continue;
        }
        PlaylistEntry entry;
        entry.path = resolveLocation(line, baseFolder);
        if (pending) {
            entry.durationSeconds = pending->seconds;
            entry.title = pending->title;
            pending = std::nullopt;
        }
        entries.append(entry);
    }
    return entries;
}

QByteArray writeM3u(const QList<PlaylistEntry> &entries, const PlaylistWriteOptions &options)
{
    // Y-WRITE: UTF-8 with "\n" line ends, the same bytes for .m3u and .m3u8.
    QByteArray data;
    if (options.extended)
        data += QByteArrayLiteral("#EXTM3U\n");
    for (const PlaylistEntry &entry : entries) {
        const bool knownDuration = entry.durationSeconds >= 0;
        if (options.extended && (!entry.title.isEmpty() || knownDuration))
            data += QByteArrayLiteral("#EXTINF:") + QByteArray::number(entry.durationSeconds)
                + QByteArrayLiteral(",") + entry.title.toUtf8() + '\n';
        data += writtenLocation(entry.path, options).toUtf8() + '\n';
    }
    return data;
}

}
