#include "tracklist/tracklistmodel_p.h"

#include <QSettings>
#include <QTimer>
#include <QScopeGuard>

// Contract: docs/spec/track-list.md (T-MODEL). Sorting and filtering are in tracklistorder.cpp.
namespace linuxbee::ui {

namespace {

bool madeForDefault(const SearchCache &cache, const QStringList &fields)
{
    return cache.joinedMade && cache.joinedFor == fields;
}

// Numbers, durations and sizes line up on the right; text on the left. Both
// are vertically centred.
int alignmentFor(const QString &field)
{
    const FieldKind kind = fieldKind(field);
    Qt::Alignment alignment = kind == FieldKind::Text ? Qt::AlignLeft : Qt::AlignRight;
    alignment |= Qt::AlignVCenter;
    return alignment.toInt();
}

}

TrackListModel::TrackListModel(QObject *parent)
    : QAbstractTableModel(parent)
    , d(new Private)
{
    d->columns = defaultColumns();
    d->setSortFields(defaultSort());
    d->timer = new QTimer(this);
    d->timer->setInterval(0);
    connect(d->timer, &QTimer::timeout, this, [this]() { d->prepareSome(); });
}

void TrackListModel::Private::enqueue(int slot)
{
    todo.append(slot);
    if (!timer->isActive())
        timer->start();
}

void TrackListModel::Private::prepareSome()
{
    const QStringList fields = defaultSearchFields();
    int done = 0;
    while (todoAt < todo.size() && done < kPiece) {
        const int slot = todo.at(todoAt++);
        if (slot >= held.size() || !held.at(slot).live)
            continue;
        Entry &entry = held[slot];
        if (madeForDefault(entry.cache, fields))
            continue;
        prepareSearch(LibraryFieldSource(entry.track), { }, &entry.cache);
        ++done;
    }
    if (todoAt >= todo.size()) {
        todo.clear();
        todoAt = 0;
        timer->stop();
    }
}

bool TrackListModel::Private::allPrepared() const
{
    const QStringList fields = defaultSearchFields();
    for (qsizetype i = todoAt; i < todo.size(); ++i) {
        const int slot = todo.at(i);
        if (slot < held.size() && held.at(slot).live
            && !madeForDefault(held.at(slot).cache, fields))
            return false;
    }
    return true;
}

bool TrackListModel::searchPrepared() const
{
    return d->allPrepared();
}

TrackListModel::~TrackListModel()
{
    delete d;
}

QStringList TrackListModel::defaultColumns()
{
    return { QStringLiteral("Track#"), QStringLiteral("Title"), QStringLiteral("Artist"),
        QStringLiteral("Album"), QStringLiteral("Year"), QStringLiteral("Genre"),
        QStringLiteral("Rating"), QStringLiteral("Time") };
}

QStringList TrackListModel::defaultSort()
{
    return { QStringLiteral("Album Artist"), QStringLiteral("Album"), QStringLiteral("Disc#"),
        QStringLiteral("Track#"), QStringLiteral("Title") };
}

QStringList TrackListModel::columns() const
{
    return d->columns;
}

bool TrackListModel::setColumns(const QStringList &fieldNames)
{
    if (fieldNames.isEmpty())
        return false;
    QStringList cleaned;
    for (const QString &name : fieldNames) {
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty())
            return false;
        for (const QString &seen : std::as_const(cleaned)) {
            if (seen.compare(trimmed, Qt::CaseInsensitive) == 0)
                return false;
        }
        cleaned.append(trimmed);
    }
    beginResetModel();
    d->columns = cleaned;
    endResetModel();
    return true;
}

QStringList TrackListModel::sortFields() const
{
    return d->sortFields;
}

void TrackListModel::setSortFields(const QStringList &fields)
{
    if (fields == d->sortFields)
        return;
    beginResetModel();
    d->setSortFields(fields);
    d->reorder();
    endResetModel();
}

bool TrackListModel::givenOrder() const
{
    return d->givenOrder;
}

void TrackListModel::setGivenOrder(bool on)
{
    // T-GIVEN: every track has kept its place in the order given (put()
    // replaces in place and appends), so showing it again is a reorder. Not
    // stored; load() leaves it.
    if (on == d->givenOrder)
        return;
    beginResetModel();
    d->givenOrder = on;
    d->reorder();
    endResetModel();
}

void TrackListModel::setTracks(const QList<LibraryTrack> &tracks)
{
    if (tracks.isEmpty() && d->slotOf.isEmpty())
        return;
    beginResetModel();
    d->held.clear();
    d->freeSlots.clear();
    d->slotOf.clear();
    d->order.clear();
    d->shown.clear();
    d->nextSeq = 0;
    d->todo.clear();
    d->todoAt = 0;
    d->timer->stop();
    for (const LibraryTrack &track : tracks)
        d->put(track, false);
    d->reorder();
    endResetModel();
    emit summaryChanged();
    // After the call's own work: each track's search value is made later, in pieces.
    d->todo.reserve(d->held.size());
    for (int slot = 0; slot < d->held.size(); ++slot) {
        if (d->held.at(slot).live)
            d->todo.append(slot);
    }
    if (!d->todo.isEmpty())
        d->timer->start();
}

void TrackListModel::upsertTracks(const QList<LibraryTrack> &tracks)
{
    if (tracks.isEmpty())
        return;

    // T-ROWS: more than 50 at once is one change of layout, not a reset. Still
    // only the tracks named are prepared; they are sorted among themselves
    // and merged into the order kept, and every persistent index follows its
    // track to the row it has afterwards.
    if (tracks.size() > 50) {
        emit layoutAboutToBeChanged();
        const QModelIndexList from = persistentIndexList();
        QList<Private::Pinned> pinned;
        pinned.reserve(from.size());
        for (const QModelIndex &index : from)
            pinned.append(d->pin(index.row(), index.column()));

        QList<int> touched;
        touched.reserve(tracks.size());
        QList<char> seen;
        for (const LibraryTrack &track : tracks) {
            const int slot = d->put(track, true);
            if (seen.size() <= slot)
                seen.resize(d->held.size(), 0);
            if (!seen.at(slot)) {
                seen[slot] = 1;
                touched.append(slot);
            }
        }
        for (const int slot : std::as_const(touched)) {
            Private::Entry &entry = d->held[slot];
            entry.shown = d->accepts(entry.track, &entry.cache);
        }
        d->mergeIn(touched);
        d->rebuildShown();
        for (const int slot : std::as_const(touched))
            d->enqueue(slot);

        QModelIndexList to;
        to.reserve(from.size());
        for (const Private::Pinned &pin : std::as_const(pinned)) {
            const int row = d->rowOfId(pin.id);
            to.append(row >= 0 ? index(row, pin.column) : QModelIndex());
        }
        changePersistentIndexList(from, to);
        emit layoutChanged();
        emit summaryChanged();
        return;
    }

    const int lastColumn = int(d->columns.size()) - 1;
    for (const LibraryTrack &track : tracks) {
        const auto queue = qScopeGuard([&]() {
            const auto at = d->slotOf.constFind(track.id);
            if (at != d->slotOf.constEnd())
                d->enqueue(at.value());
        });
        // Everything is worked out before the first signal, from the keys the
        // rows have now and the keys of this track as it is now.
        const QList<PreparedValue> keys = d->prepareKeys(track);
        SearchCache cache;
        const bool accepted = d->accepts(track, &cache);
        const auto found = d->slotOf.constFind(track.id);

        if (found == d->slotOf.constEnd()) {
            const qint64 seq = d->nextSeq;
            const Private::Place place { &keys, &track.path, seq };
            const int orderAt = d->lowerBound(d->order, place, -1);
            const int row = accepted ? d->lowerBound(d->shown, place, -1) : -1;
            const int slot = d->allocate();
            Private::Entry &entry = d->held[slot];
            entry.track = track;
            entry.cache = std::move(cache);
            entry.keys = keys;
            entry.seq = seq;
            entry.live = true;
            ++d->nextSeq;
            d->slotOf.insert(track.id, slot);
            d->order.insert(orderAt, slot);
            if (row >= 0) {
                beginInsertRows({ }, row, row);
                d->shown.insert(row, slot);
                entry.shown = true;
                d->shownBytes += track.sizeBytes;
                d->shownMs += static_cast<qint64>(track.tags.audio.durationMs);
                endInsertRows();
            }
            continue;
        }

        const int slot = found.value();
        const int oldRow = d->rowFor(slot);
        const int oldOrder = d->orderIndexOf(slot);
        const Private::Place place { &keys, &track.path, d->held.at(slot).seq };
        const int newOrder = d->lowerBound(d->order, place, oldOrder);
        const int newRow = accepted ? d->lowerBound(d->shown, place, oldRow) : -1;

        // The track takes the place of the old one in the data; the rows are
        // moved, inserted or removed around it.
        const auto replace = [&]() {
            Private::Entry &entry = d->held[slot];
            if (entry.shown) {
                d->shownBytes -= entry.track.sizeBytes;
                d->shownMs -= static_cast<qint64>(entry.track.tags.audio.durationMs);
            }
            entry.track = track;
            entry.cache = std::move(cache);
            entry.keys = keys;
            entry.shown = accepted;
            if (accepted) {
                d->shownBytes += track.sizeBytes;
                d->shownMs += static_cast<qint64>(track.tags.audio.durationMs);
            }
            if (newOrder != oldOrder)
                d->order.move(oldOrder, newOrder);
        };

        if (oldRow >= 0 && accepted) {
            if (newRow == oldRow) {
                replace();
            } else {
                // Moving down, the destination is the row after the new one.
                beginMoveRows({ }, oldRow, oldRow, { }, newRow > oldRow ? newRow + 1 : newRow);
                replace();
                d->shown.move(oldRow, newRow);
                endMoveRows();
            }
            const int row = newRow;
            emit dataChanged(index(row, 0), index(row, lastColumn));
        } else if (oldRow >= 0) {
            beginRemoveRows({ }, oldRow, oldRow);
            replace();
            d->shown.removeAt(oldRow);
            endRemoveRows();
        } else if (accepted) {
            beginInsertRows({ }, newRow, newRow);
            replace();
            d->shown.insert(newRow, slot);
            endInsertRows();
        } else {
            replace();
        }
    }
    emit summaryChanged();
}

void TrackListModel::removeTracks(const QList<qint64> &ids)
{
    bool anyPresent = false;
    for (const qint64 id : ids) {
        if (d->slotOf.contains(id)) {
            anyPresent = true;
            break;
        }
    }
    if (!anyPresent)
        return;

    // T-ROWS: more than 50 at once is one change of layout, not a reset.
    // Nothing is prepared; an index of a track that went is made invalid.
    if (ids.size() > 50) {
        emit layoutAboutToBeChanged();
        const QModelIndexList from = persistentIndexList();
        QList<Private::Pinned> pinned;
        pinned.reserve(from.size());
        for (const QModelIndex &index : from)
            pinned.append(d->pin(index.row(), index.column()));

        for (const qint64 id : ids) {
            const auto found = d->slotOf.constFind(id);
            if (found != d->slotOf.constEnd())
                d->release(found.value());
        }
        d->order.removeIf([this](int slot) { return !d->held.at(slot).live; });
        d->rebuildShown();

        QModelIndexList to;
        to.reserve(from.size());
        for (const Private::Pinned &pin : std::as_const(pinned)) {
            const int row = d->rowOfId(pin.id);
            to.append(row >= 0 ? index(row, pin.column) : QModelIndex());
        }
        changePersistentIndexList(from, to);
        emit layoutChanged();
        emit summaryChanged();
        return;
    }

    for (const qint64 id : ids) {
        const auto found = d->slotOf.constFind(id);
        if (found == d->slotOf.constEnd())
            continue;
        const int slot = found.value();
        const int row = d->rowFor(slot);
        const int orderAt = d->orderIndexOf(slot);
        if (row >= 0)
            beginRemoveRows({ }, row, row);
        if (row >= 0) {
            d->shownBytes -= d->trackAt(slot).sizeBytes;
            d->shownMs -= static_cast<qint64>(d->trackAt(slot).tags.audio.durationMs);
            d->shown.removeAt(row);
        }
        d->order.removeAt(orderAt);
        d->release(slot);
        if (row >= 0)
            endRemoveRows();
    }
    emit summaryChanged();
}

void TrackListModel::setFilter(const SearchQuery &query)
{
    if (query.canonical() == d->filter.canonical())
        return;
    beginResetModel();
    d->filter = query;
    d->refilter(); // the kept order stays: a keystroke sorts nothing
    endResetModel();
    emit summaryChanged();
}

int TrackListModel::totalCount() const
{
    return int(d->slotOf.size());
}

qint64 TrackListModel::trackId(int row) const
{
    if (row < 0 || row >= d->shown.size())
        return 0;
    return d->trackAt(d->shown.at(row)).id;
}

int TrackListModel::rowOf(qint64 id) const
{
    const auto found = d->slotOf.constFind(id);
    return found == d->slotOf.constEnd() ? -1 : d->rowFor(found.value());
}

QString TrackListModel::summary() const
{
    return formatTrackSummary(int(d->shown.size()), d->shownBytes, d->shownMs);
}

void TrackListModel::save(QSettings &settings, const QString &group) const
{
    settings.setValue(group + QStringLiteral("/columns"), d->columns);
    settings.setValue(group + QStringLiteral("/sort"), d->sortFields);
}

void TrackListModel::load(QSettings &settings, const QString &group)
{
    const QStringList storedColumns
        = settings.value(group + QStringLiteral("/columns")).toStringList();
    if (!setColumns(storedColumns))
        setColumns(defaultColumns());
    const QStringList storedSort = settings.value(group + QStringLiteral("/sort")).toStringList();
    setSortFields(storedSort.isEmpty() ? defaultSort() : storedSort);
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(d->shown.size());
}

int TrackListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(d->columns.size());
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= d->shown.size() || index.column() >= d->columns.size())
        return { };
    const LibraryTrack &track = d->trackAt(d->shown.at(index.row()));
    const QString &field = d->columns.at(index.column());
    switch (role) {
    case Qt::DisplayRole: {
        const LibraryFieldSource source(track);
        const QString value = source.value(field).value_or(QString());
        // T-MODEL: a Love column shows a sign for its value; sorting and search keep the letter.
        if (field.compare(QLatin1String("Love"), Qt::CaseInsensitive) == 0) {
            if (value == QLatin1String("L"))
                return QStringLiteral(u"\u2665");
            if (value == QLatin1String("B"))
                return QStringLiteral(u"\u2298");
        }
        return value;
    }
    case Qt::ToolTipRole:
        return track.path;
    case Qt::TextAlignmentRole:
        return alignmentFor(field);
    case TrackIdRole:
        return track.id;
    case MissingRole:
        return track.missing;
    default:
        return { };
    }
}

QVariant TrackListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section >= d->columns.size())
        return { };
    const QString &field = d->columns.at(section);
    switch (role) {
    case Qt::DisplayRole:
        return field;
    case Qt::TextAlignmentRole:
        return alignmentFor(field);
    default:
        return { };
    }
}

}
