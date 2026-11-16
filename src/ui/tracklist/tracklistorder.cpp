#include "tracklist/tracklistmodel_p.h"

#include <algorithm>

// Sorting and filtering inside the model. Contract: docs/spec/track-list.md (T-MODEL).
namespace linuxbee::ui {

QList<PreparedValue> TrackListModel::Private::prepareKeys(const LibraryTrack &track) const
{
    // T-GIVEN: in the given order the keys decide nothing, so none is prepared.
    QList<PreparedValue> keys;
    if (givenOrder)
        return keys;
    const LibraryFieldSource source(track);
    for (const SortKey &key : sortKeys)
        keys.append(prepareFieldValue(key.field, source.value(key.field).value_or(QString())));
    return keys;
}

bool TrackListModel::Private::before(const Place &a, const Place &b) const
{
    // A blank value comes last in both directions. Tracks that tie on every
    // key are in order of path, and then in the order given.
    if (!givenOrder) {
        for (int k = 0; k < sortKeys.size(); ++k) {
            const PreparedValue &valueA = a.keys->at(k);
            const PreparedValue &valueB = b.keys->at(k);
            if (valueA.blank != valueB.blank)
                return valueB.blank;
            if (valueA.blank)
                continue;
            const int result = comparePrepared(valueA, valueB);
            if (result != 0)
                return sortKeys.at(k).descending ? result > 0 : result < 0;
        }
        if (*a.path != *b.path)
            return *a.path < *b.path;
    }
    return a.seq < b.seq;
}

int TrackListModel::Private::lowerBound(const QList<int> &list, const Place &place, int skip) const
{
    int low = 0;
    int high = int(list.size()) - (skip >= 0 ? 1 : 0);
    while (low < high) {
        const int middle = low + (high - low) / 2;
        const int index = skip >= 0 && middle >= skip ? middle + 1 : middle;
        if (before(placeOf(list.at(index)), place))
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

bool TrackListModel::Private::accepts(const LibraryTrack &track, SearchCache *cache) const
{
    return filter.isEmpty() || filter.matches(LibraryFieldSource(track), { }, cache);
}

void TrackListModel::Private::prepareAll()
{
    for (Entry &entry : held) {
        if (entry.live)
            entry.keys = prepareKeys(entry.track);
    }
}

void TrackListModel::Private::sortKept()
{
    order.clear();
    for (int slot = 0; slot < held.size(); ++slot) {
        if (held.at(slot).live)
            order.append(slot);
    }
    // The order is total (the order given breaks every tie), so no stable sort is needed.
    std::sort(order.begin(), order.end(),
        [this](int a, int b) { return before(placeOf(a), placeOf(b)); });
}

void TrackListModel::Private::refilter()
{
    shown.clear();
    shownBytes = 0;
    shownMs = 0;
    for (const int slot : std::as_const(order)) {
        Entry &entry = held[slot];
        entry.shown = accepts(entry.track, &entry.cache);
        if (!entry.shown)
            continue;
        shown.append(slot);
        shownBytes += entry.track.sizeBytes;
        shownMs += static_cast<qint64>(entry.track.tags.audio.durationMs);
    }
}

void TrackListModel::Private::mergeIn(QList<int> moved)
{
    QList<char> moving(held.size(), 0);
    for (const int slot : std::as_const(moved))
        moving[slot] = 1;
    QList<int> kept;
    kept.reserve(order.size());
    for (const int slot : std::as_const(order)) {
        if (!moving.at(slot))
            kept.append(slot);
    }
    // The order is total, so the same comparison sorts, merges and decides.
    const auto less = [this](int a, int b) { return before(placeOf(a), placeOf(b)); };
    std::sort(moved.begin(), moved.end(), less);
    QList<int> merged(kept.size() + moved.size());
    std::merge(kept.cbegin(), kept.cend(), moved.cbegin(), moved.cend(), merged.begin(), less);
    order = std::move(merged);
}

void TrackListModel::Private::rebuildShown()
{
    shown.clear();
    shownBytes = 0;
    shownMs = 0;
    for (const int slot : std::as_const(order)) {
        const Entry &entry = held.at(slot);
        if (!entry.shown)
            continue;
        shown.append(slot);
        shownBytes += entry.track.sizeBytes;
        shownMs += static_cast<qint64>(entry.track.tags.audio.durationMs);
    }
}

void TrackListModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= d->columns.size())
        return;
    const QString field = d->columns.at(column);
    QStringList keys = { (order == Qt::DescendingOrder ? QStringLiteral("-") : QString()) + field };
    for (const QString &name : defaultSort()) {
        if (name.compare(field, Qt::CaseInsensitive) != 0)
            keys.append(name);
    }
    if (!d->givenOrder) {
        setSortFields(keys);
        return;
    }
    // T-GIVEN: a click sorts. The given order goes off and the rows are sorted
    // by these keys, also when they are the keys the model already has
    // (setSortFields would stop there). One reset.
    beginResetModel();
    d->givenOrder = false;
    d->setSortFields(keys);
    d->reorder();
    endResetModel();
}

}
