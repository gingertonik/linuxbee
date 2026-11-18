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

// I-PLAN: the library's paths, looked up by hash and not searched for by walking the list.
struct PathIndex {
    struct Folded {
        const LibraryTrack *track = nullptr;
        int fits = 0; // how many library tracks fold to this path
    };
    QHash<QString, const LibraryTrack *> exact; // the first track at each exact path
    QHash<QString, Folded> folded; // by the path with its case folded

    explicit PathIndex(const QList<LibraryTrack> &libraryTracks)
    {
        exact.reserve(libraryTracks.size());
        folded.reserve(libraryTracks.size());
        for (const LibraryTrack &track : libraryTracks) {
            if (!exact.contains(track.path))
                exact.insert(track.path, &track); // the first at a path is the one that counts
            Folded &entry = folded[track.path.toCaseFolded()];
            if (entry.fits++ == 0)
                entry.track = &track;
        }
    }

    // An exact path first; else the only track that fits when case is ignored; else nothing.
    const LibraryTrack *find(const QString &path) const
    {
        if (const auto hit = exact.constFind(path); hit != exact.constEnd())
            return hit.value();
        const auto fold = folded.constFind(path.toCaseFolded());
        return fold != folded.constEnd() && fold->fits == 1 ? fold->track : nullptr;
    }
};

// What the library should hold after the import of one track.
ImportMatch matchOf(const ImportedTrack &track, const LibraryTrack &library)
{
    ImportMatch match;
    match.libraryId = library.id;
    match.libraryPath = library.path;
    match.rating = track.rating;
    match.love = track.love;

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

    const PathIndex index(libraryTracks);
    QHash<int, QString> libraryPathOf; // every imported track found, duplicates included
    QSet<qint64> matched; // library tracks already given a match
    for (const ImportedTrack &track : imported.tracks) {
        const QString mapped = mapImportedPath(track.location, mapping);
        const LibraryTrack *library = index.find(mapped);
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
