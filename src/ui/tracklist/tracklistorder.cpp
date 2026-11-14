#include "tracklist/tracklistmodel_p.h"

#include <algorithm>

// Sorting and filtering inside the model. Contract: docs/spec/track-list.md (T-MODEL).
namespace linuxbee::ui {

namespace {

struct SortKey {
    QString field;
    bool descending = false;
};

// One track, with each sort key already read and prepared for comparing
// (T-PREPARED).
struct Candidate {
    int position = 0;
    QList<PreparedValue> values;
    QString path;
};

}

void TrackListModel::Private::sortAll()
{
    // T-GIVEN: in the given order the keys decide nothing; `tracks` is already
    // in the order given, so the candidates below stay as they come.
    QList<SortKey> keys;
    if (!givenOrder) {
        for (const QString &name : std::as_const(sortFields)) {
            const bool descending = name.startsWith(QLatin1Char('-'));
            keys.append(SortKey { descending ? name.mid(1) : name, descending });
        }
    }

    QList<Candidate> candidates;
    for (int i = 0; i < tracks.size(); ++i) {
        const LibraryTrack &track = tracks.at(i);
        const LibraryFieldSource source(track);
        Candidate candidate;
        candidate.position = i;
        candidate.path = track.path;
        for (const SortKey &key : std::as_const(keys))
            candidate.values.append(
                prepareFieldValue(key.field, source.value(key.field).value_or(QString())));
        candidates.append(candidate);
    }

    // A blank value comes last in both directions. Tracks that tie on every
    // key are in order of path.
    if (!givenOrder)
        std::stable_sort(
            candidates.begin(), candidates.end(), [&keys](const Candidate &a, const Candidate &b) {
                for (int k = 0; k < keys.size(); ++k) {
                    const PreparedValue &valueA = a.values.at(k);
                    const PreparedValue &valueB = b.values.at(k);
                    const bool blankA = valueA.blank;
                    const bool blankB = valueB.blank;
                    if (blankA != blankB)
                        return blankB;
                    if (blankA)
                        continue;
                    const int order = comparePrepared(valueA, valueB);
                    if (order != 0)
                        return keys.at(k).descending ? order > 0 : order < 0;
                }
                return a.path < b.path;
            });

    order.clear();
    for (const Candidate &candidate : std::as_const(candidates))
        order.append(candidate.position);
}

void TrackListModel::Private::refilter()
{
    shown.clear();
    rowOf.clear();
    shownBytes = 0;
    shownMs = 0;
    for (const int position : std::as_const(order)) {
        const LibraryTrack &track = tracks.at(position);
        if (!filter.isEmpty() && !filter.matches(LibraryFieldSource(track), { }, &caches[position]))
            continue;
        rowOf.insert(track.id, int(shown.size()));
        shown.append(position);
        shownBytes += track.sizeBytes;
        shownMs += static_cast<qint64>(track.tags.audio.durationMs);
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
    d->sortFields = keys;
    d->reorder();
    endResetModel();
}

}
