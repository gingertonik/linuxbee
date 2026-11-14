#pragma once

#include "tracklist/tracklistmodel.h"

#include <QHash>
#include <QSet>

namespace linuxbee::ui {

// The state behind TrackListModel, shared by tracklistmodel.cpp (changes and
// the Qt model overrides) and tracklistorder.cpp (sorting and filtering).
struct TrackListModel::Private {
    QList<LibraryTrack> tracks; // every track, in the order they were given
    QHash<qint64, int> positionOf; // track id -> index into tracks
    QStringList columns;
    QStringList sortFields;
    SearchQuery filter;
    QList<int> shown; // indexes into tracks, in the order shown
    QHash<qint64, int> rowOf; // track id -> row in shown
    qint64 shownBytes = 0;
    qint64 shownMs = 0;

    // Adds the track, or replaces the one with its id.
    void put(const LibraryTrack &track)
    {
        const auto found = positionOf.constFind(track.id);
        if (found != positionOf.constEnd()) {
            tracks[found.value()] = track;
            return;
        }
        positionOf.insert(track.id, int(tracks.size()));
        tracks.append(track);
    }

    void removeIds(const QSet<qint64> &ids)
    {
        QList<LibraryTrack> kept;
        for (const LibraryTrack &track : std::as_const(tracks)) {
            if (!ids.contains(track.id))
                kept.append(track);
        }
        tracks = kept;
        positionOf.clear();
        for (int i = 0; i < tracks.size(); ++i)
            positionOf.insert(tracks.at(i).id, i);
    }

    // Filters, sorts and totals the tracks into shown, rowOf, shownBytes and
    // shownMs. Defined in tracklistorder.cpp.
    void reorder();
};

}
