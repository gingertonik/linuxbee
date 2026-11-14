#pragma once

#include "search/search.h"
#include "tracklist/tracklistmodel.h"

#include <QHash>
#include <QSet>

namespace linuxbee::ui {

// The state behind TrackListModel, shared by tracklistmodel.cpp (changes and
// the Qt model overrides) and tracklistorder.cpp (sorting and filtering).
struct TrackListModel::Private {
    QList<LibraryTrack> tracks; // every track, in the order they were given
    QList<SearchCache> caches; // S-CACHE: one for each track, at the same index as in tracks
    QList<int> order; // indexes into tracks: every track, in the order the keys give
    QHash<qint64, int> positionOf; // track id -> index into tracks
    QStringList columns;
    QStringList sortFields;
    bool givenOrder = false; // T-GIVEN: show `tracks` as they are, the keys decide nothing
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
            caches[found.value()] = SearchCache(); // the track changed: what was folded goes
            return;
        }
        positionOf.insert(track.id, int(tracks.size()));
        tracks.append(track);
        caches.append(SearchCache());
    }

    void removeIds(const QSet<qint64> &ids)
    {
        QList<LibraryTrack> kept;
        QList<SearchCache> keptCaches;
        for (int i = 0; i < tracks.size(); ++i) {
            if (!ids.contains(tracks.at(i).id)) {
                kept.append(tracks.at(i));
                keptCaches.append(std::move(caches[i])); // carried along with its track
            }
        }
        tracks = kept;
        caches = keptCaches;
        positionOf.clear();
        for (int i = 0; i < tracks.size(); ++i)
            positionOf.insert(tracks.at(i).id, i);
    }

    // Sorts every track into order (the keys, or the order given). Defined in
    // tracklistorder.cpp.
    void sortAll();
    // Filters order into shown, rowOf, shownBytes and shownMs. Sorts nothing and
    // prepares no value; folds only what the caches do not have. Defined in
    // tracklistorder.cpp.
    void refilter();
    // Both, for every change but the filter's own.
    void reorder()
    {
        sortAll();
        refilter();
    }
};

}
