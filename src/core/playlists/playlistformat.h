#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

// Playlist files other programs can read: M3U, M3U8 and PLS.
// Contract: docs/spec/playlists.md. These functions turn bytes into entries
// and back; they open no file.
namespace linuxbee {

struct PlaylistEntry {
    QString path; // a clean absolute path, or a URL
    QString title; // may be empty
    int durationSeconds = -1; // -1: not known

    bool operator==(const PlaylistEntry &other) const = default;
};

enum class PlaylistFormat { M3u, M3u8, Pls };

// Y-FORMAT: by the file name's extension, case ignored.
std::optional<PlaylistFormat> playlistFormatForFile(const QString &fileName);

// Y-READ. baseFolder is the folder the playlist file is in; relative paths
// are resolved against it.
QList<PlaylistEntry> parsePlaylist(
    const QByteArray &data, PlaylistFormat format, const QString &baseFolder);

struct PlaylistWriteOptions {
    bool extended = true; // M3U: write #EXTM3U and #EXTINF lines
    bool relativePaths = false; // write paths relative to baseFolder
    QString baseFolder; // the folder the playlist file will be in
};

// Y-WRITE. Always UTF-8, lines end with "\n".
QByteArray writePlaylist(const QList<PlaylistEntry> &entries, PlaylistFormat format,
    const PlaylistWriteOptions &options = { });

}
