#pragma once
#include <QByteArray>
#include <QList>
#include <QStringList>

#include "playlistformat.h"

// Not public API: playlistformat.h is. These are shared by the two readers and
// the two writers, so that one rule is written once. Defined in decode.cpp,
// m3u.cpp and pls.cpp.
namespace linuxbee::playlists {

// Y-DECODE: bytes to lines. A leading UTF-8 BOM is dropped, .m3u8 is UTF-8,
// .m3u and .pls are UTF-8 when the bytes are all valid UTF-8 and Latin-1
// otherwise. Lines end with "\r\n", "\n" or "\r"; every line is trimmed.
QStringList decodeLines(const QByteArray &data, PlaylistFormat format);

// Y-PATH: one location as it stands in the file, resolved against the folder
// the playlist file is in. Nothing on disk is looked at.
QString resolveLocation(const QString &raw, const QString &baseFolder);

// Y-WRITE: how one location is written: as it is, or relative to
// options.baseFolder when asked and when the location is a plain path.
QString writtenLocation(const QString &location, const PlaylistWriteOptions &options);

// Y-READ for one format. format is the format being read.
QList<PlaylistEntry> readM3u(
    const QByteArray &data, PlaylistFormat format, const QString &baseFolder);
QList<PlaylistEntry> readPls(const QByteArray &data, const QString &baseFolder);

// Y-WRITE for one format.
QByteArray writeM3u(const QList<PlaylistEntry> &entries, const PlaylistWriteOptions &options);
QByteArray writePls(const QList<PlaylistEntry> &entries, const PlaylistWriteOptions &options);

}
