#include "playlistformat.h"

// Stub. Plan 15 replaces every body in this file.
namespace linuxbee {

std::optional<PlaylistFormat> playlistFormatForFile(const QString &fileName)
{
    Q_UNUSED(fileName);
    return std::nullopt;
}

QList<PlaylistEntry> parsePlaylist(
    const QByteArray &data, PlaylistFormat format, const QString &baseFolder)
{
    Q_UNUSED(data);
    Q_UNUSED(format);
    Q_UNUSED(baseFolder);
    return { };
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
