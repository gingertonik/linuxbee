#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <optional>

#include "library/library.h"

// Bringing a library over from another program through an iTunes-format
// library XML file, which MusicBee can export. Contract: docs/spec/import.md.
// Parsing and matching are pure; nothing here writes anything.
namespace linuxbee {

struct ImportedTrack {
    int trackId = 0; // the file's own number for the track
    QString location; // I-LOCATION: a path such as "C:/Music/a.mp3" or "/Volumes/x/a.mp3"
    QString name;
    QString artist;
    QString album;
    std::optional<double> rating; // stars, 0.5 to 5
    QString love; // I-PARSE: as written, without the space round it ("L", "B"); else empty
    int playCount = 0;
    int skipCount = 0;
    QDateTime dateAdded; // UTC; invalid when the file does not say
    QDateTime lastPlayed; // UTC; invalid when never played
};

struct ImportedPlaylist {
    QString name;
    QList<int> trackIds; // in order
};

struct ImportedLibrary {
    bool ok = false;
    QString error; // set when !ok
    QList<ImportedTrack> tracks; // in file order
    QList<ImportedPlaylist> playlists; // in file order
};

// I-PARSE
ImportedLibrary parseItunesLibrary(const QByteArray &xml);

// I-LOCATION: the path a "Location" URL names.
QString pathFromItunesLocation(const QString &location);

// I-MAP: old folder -> new folder. The first pair that fits is used.
using PathMapping = QList<QPair<QString, QString>>;
QString mapImportedPath(const QString &path, const PathMapping &mapping);

struct ImportMatch {
    qint64 libraryId = 0;
    QString libraryPath;
    TrackStatistics statistics; // what the library should hold afterwards
    std::optional<double> rating; // from the file, for the caller to check (I-CHECK)
    QString love; // from the file, for the caller to check (I-CHECK)
    bool changes = false; // false: the library says this already
};

struct ImportPlan {
    QList<ImportMatch> matches; // in the order of the imported tracks
    QStringList unmatched; // mapped paths of imported tracks the library does not have
    // Playlists as paths in the library; entries that matched nothing are left out.
    struct Playlist {
        QString name;
        QStringList paths;
        int missing = 0;
    };
    QList<Playlist> playlists;
};

// I-PLAN
ImportPlan planImport(const ImportedLibrary &imported, const QList<LibraryTrack> &libraryTracks,
    const PathMapping &mapping);

}
