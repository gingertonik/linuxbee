#include "playlistformat.h"

#include "formatdetail.h"

// Plan 15 replaces every stub body in this file.
namespace linuxbee {

std::optional<PlaylistFormat> playlistFormatForFile(const QString &fileName)
{
    // Y-FORMAT: by the end of the file name, case ignored.
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return std::nullopt;
    const QString extension = fileName.mid(dot + 1).toLower();
    if (extension == QLatin1String("m3u"))
        return PlaylistFormat::M3u;
    if (extension == QLatin1String("m3u8"))
        return PlaylistFormat::M3u8;
    if (extension == QLatin1String("pls"))
        return PlaylistFormat::Pls;
    return std::nullopt;
}

QList<PlaylistEntry> parsePlaylist(
    const QByteArray &data, PlaylistFormat format, const QString &baseFolder)
{
    if (format == PlaylistFormat::Pls)
        return playlists::readPls(data, baseFolder);
    return playlists::readM3u(data, format, baseFolder);
}

QByteArray writePlaylist(
    const QList<PlaylistEntry> &entries, PlaylistFormat format, const PlaylistWriteOptions &options)
{
    Q_UNUSED(entries);
    Q_UNUSED(format);
    Q_UNUSED(options);
    return { };
}

}
