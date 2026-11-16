#pragma once

#include "search/search.h"
#include "sort/fieldsort.h"
#include "tracklist/tracklistmodel.h"

#include <QHash>
#include <QSet>

namespace linuxbee::ui {

// The state behind TrackListModel, shared by tracklistmodel.cpp (changes and
// the Qt model overrides) and tracklistorder.cpp (sorting and filtering).
struct TrackListModel::Private {
    struct SortKey {
        QString field;
        bool descending = false;
    };

    // One track, with what is kept beside it.
    struct Entry {
        LibraryTrack track;
        SearchCache cache; // S-CACHE: emptied when the track is replaced
        QList<PreparedValue>
            keys; // T-ROWS: the sort keys in force, prepared; none in the given order
        qint64 seq = 0; // the place in the order given: a replaced track keeps it
        bool live = false; // false for a slot that is free
        bool shown = false; // has a row
    };

    // What a comparison of two tracks reads. The pointers outlive the call.
    struct Place {
        const QList<PreparedValue> *keys;
        const QString *path;
        qint64 seq;
    };

    // Tracks live in `held` at numbers (slots) that do not change while the
    // track is held, so a removal moves no other track's slot and rebuilds
    // no index.
    QList<Entry> held;
    QList<int> freeSlots;
    QHash<qint64, int> slotOf; // track id -> slot
    qint64 nextSeq = 0;
    QStringList columns;
    QStringList sortFields;
    QList<SortKey> sortKeys; // sortFields, taken apart
    bool givenOrder = false; // T-GIVEN: show the tracks as given, the keys decide nothing
    SearchQuery filter;
    QList<int> order; // slots: every track, in the order the keys (or the order given) give
    QList<int> shown; // slots, in the order shown: the rows
    qint64 shownBytes = 0;
    qint64 shownMs = 0;

    const LibraryTrack &trackAt(int slot) const { return held.at(slot).track; }

    void setSortFields(const QStringList &fields)
    {
        sortFields = fields;
        sortKeys.clear();
        for (const QString &name : std::as_const(fields)) {
            const bool descending = name.startsWith(QLatin1Char('-'));
            sortKeys.append(SortKey { descending ? name.mid(1) : name, descending });
        }
    }

    int allocate()
    {
        if (!freeSlots.isEmpty())
            return freeSlots.takeLast();
        held.append(Entry());
        return int(held.size()) - 1;
    }

    void release(int slot)
    {
        slotOf.remove(held.at(slot).track.id);
        held[slot] = Entry();
        freeSlots.append(slot);
    }

    // The sort keys of one track, prepared. None in the given order.
    QList<PreparedValue> prepareKeys(const LibraryTrack &track) const;

    // Adds the track, or replaces the one with its id (keeping its place in
    // the order given). Touches neither `order` nor `shown`. Returns the slot.
    int put(const LibraryTrack &track, bool prepare)
    {
        const auto found = slotOf.constFind(track.id);
        int slot = 0;
        if (found != slotOf.constEnd()) {
            slot = found.value();
        } else {
            slot = allocate();
            slotOf.insert(track.id, slot);
            held[slot].seq = nextSeq++;
            held[slot].live = true;
        }
        Entry &entry = held[slot];
        entry.track = track;
        entry.cache = SearchCache(); // the track changed: what was folded goes
        entry.keys = prepare ? prepareKeys(track) : QList<PreparedValue>();
        return slot;
    }

    Place placeOf(int slot) const
    {
        const Entry &entry = held.at(slot);
        return Place { &entry.keys, &entry.track.path, entry.seq };
    }

    // True when `a` comes before `b`: the keys, then the path, then the order given.
    bool before(const Place &a, const Place &b) const;

    // Where `place` goes in `list` (`order` or `shown`, both in order), by
    // binary search over the kept keys. With `skip` >= 0 the entry at that
    // index is left out, and the answer is an index in the list without it.
    int lowerBound(const QList<int> &list, const Place &place, int skip) const;

    // The index of a slot in `order`, and its row (-1 when not shown).
    int orderIndexOf(int slot) const { return lowerBound(order, placeOf(slot), -1); }
    int rowFor(int slot) const
    {
        return held.at(slot).shown ? lowerBound(shown, placeOf(slot), -1) : -1;
    }

    // Whether the filter accepts the track as it is; folds into `cache`.
    bool accepts(const LibraryTrack &track, SearchCache *cache) const;

    // Prepares the keys of every track (nothing in the given order). Defined
    // in tracklistorder.cpp.
    void prepareAll();
    // Puts every track in order by the keys kept: prepares nothing, compares
    // prepared values only. Defined in tracklistorder.cpp.
    void sortKept();
    // Filters order into shown, the rows' flags, shownBytes and shownMs. Sorts
    // nothing and prepares no value; folds only what the caches do not have.
    // Defined in tracklistorder.cpp.
    void refilter();
    // T-ROWS, more than 50: takes the slots given (each once) out of `order` and
    // puts them back where their keys say, by sorting them among themselves
    // and merging them into the order kept. The rest is not sorted again.
    void mergeIn(QList<int> moved);
    // T-ROWS, more than 50: builds `shown`, shownBytes and shownMs again from
    // the rows' flags, in `order`. Filters nothing and prepares nothing.
    void rebuildShown();
    // T-ROWS, more than 50: a persistent index, taken apart before the change
    // and put back after it.
    struct Pinned {
        qint64 id = 0; // -1 for an index that pointed at no row
        int column = 0;
    };
    Pinned pin(int row, int column) const
    {
        return Pinned { row >= 0 && row < shown.size() ? trackAt(shown.at(row)).id : -1, column };
    }
    // The row the track has now, -1 when it is gone or not shown.
    int rowOfId(qint64 id) const
    {
        const auto found = slotOf.constFind(id);
        return found == slotOf.constEnd() ? -1 : rowFor(found.value());
    }
    // All three, for a change of the keys or of every track.
    void reorder()
    {
        prepareAll();
        sortKept();
        refilter();
    }
};

}
