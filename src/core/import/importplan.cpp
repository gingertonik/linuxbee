#include "itunesxml.h"

#include <QHash>
#include <QSet>

#include <algorithm>

// I-PLAN. Decides what an import would do; changes nothing.
namespace linuxbee {

namespace {

// Two times are the same when both are missing, or both are present and equal.
bool sameTime(const QDateTime &a, const QDateTime &b)
{
    if (!a.isValid() || !b.isValid())
        return a.isValid() == b.isValid();
    return a == b;
}

// The library track the path lands on: an exact path first; else the only
// track that fits when case is ignored; else nothing.
const LibraryTrack *findTrack(const QString &path, const QList<LibraryTrack> &libraryTracks)
{
    for (const LibraryTrack &track : libraryTracks) {
        if (track.path == path)
            return &track;
    }
    const LibraryTrack *only = nullptr;
    int fits = 0;
    for (const LibraryTrack &track : libraryTracks) {
        if (track.path.compare(path, Qt::CaseInsensitive) == 0) {
            only = &track;
            ++fits;
        }
    }
    return fits == 1 ? only : nullptr;
}

// What the library should hold after the import of one track.
ImportMatch matchOf(const ImportedTrack &track, const LibraryTrack &library)
{
    ImportMatch match;
    match.libraryId = library.id;
    match.libraryPath = library.path;
    match.rating = track.rating;

    TrackStatistics &next = match.statistics;
    next.dateAdded = track.dateAdded.isValid() ? track.dateAdded : library.dateAdded;
    next.playCount = std::max(track.playCount, library.playCount);
    next.skipCount = std::max(track.skipCount, library.skipCount);
    if (!track.lastPlayed.isValid())
        next.lastPlayed = library.lastPlayed;
    else if (!library.lastPlayed.isValid() || track.lastPlayed > library.lastPlayed)
        next.lastPlayed = track.lastPlayed;
    else
        next.lastPlayed = library.lastPlayed;

    match.changes = !sameTime(next.dateAdded, library.dateAdded)
        || next.playCount != library.playCount || next.skipCount != library.skipCount
        || !sameTime(next.lastPlayed, library.lastPlayed);
    return match;
}

}

ImportPlan planImport(const ImportedLibrary &imported, const QList<LibraryTrack> &libraryTracks,
    const PathMapping &mapping)
{
    ImportPlan plan;
    if (!imported.ok)
        return plan;

    QHash<int, QString> libraryPathOf; // every imported track found, duplicates included
    QSet<qint64> matched; // library tracks already given a match
    for (const ImportedTrack &track : imported.tracks) {
        const QString mapped = mapImportedPath(track.location, mapping);
        const LibraryTrack *library = findTrack(mapped, libraryTracks);
        if (!library) {
            plan.unmatched.append(mapped);
            continue;
        }
        libraryPathOf.insert(track.trackId, library->path);
        if (matched.contains(library->id))
            continue; // the first imported track for this file gets the match
        matched.insert(library->id);
        plan.matches.append(matchOf(track, *library));
    }

    for (const ImportedPlaylist &playlist : imported.playlists) {
        ImportPlan::Playlist out;
        out.name = playlist.name;
        for (const int trackId : playlist.trackIds) {
            const auto found = libraryPathOf.constFind(trackId);
            if (found == libraryPathOf.constEnd())
                ++out.missing;
            else
                out.paths.append(found.value());
        }
        plan.playlists.append(out);
    }
    return plan;
}

}
