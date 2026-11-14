#include "tracklist/tracklistmodel_p.h"

#include <QSettings>

// Contract: docs/spec/track-list.md (T-MODEL). Sorting and filtering are in tracklistorder.cpp.
namespace linuxbee::ui {

namespace {

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
    d->sortFields = defaultSort();
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
    d->sortFields = fields;
    d->reorder();
    endResetModel();
}

bool TrackListModel::givenOrder() const
{
    return d->givenOrder;
}

void TrackListModel::setGivenOrder(bool on)
{
    // T-GIVEN: `tracks` has kept the order given (put() replaces in place and
    // appends), so showing it again is a reorder. Not stored; load() leaves it.
    if (on == d->givenOrder)
        return;
    beginResetModel();
    d->givenOrder = on;
    d->reorder();
    endResetModel();
}

void TrackListModel::setTracks(const QList<LibraryTrack> &tracks)
{
    if (tracks.isEmpty() && d->tracks.isEmpty())
        return;
    beginResetModel();
    d->tracks.clear();
    d->positionOf.clear();
    for (const LibraryTrack &track : tracks)
        d->put(track);
    d->reorder();
    endResetModel();
    emit summaryChanged();
}

void TrackListModel::upsertTracks(const QList<LibraryTrack> &tracks)
{
    if (tracks.isEmpty())
        return;
    beginResetModel();
    for (const LibraryTrack &track : tracks)
        d->put(track);
    d->reorder();
    endResetModel();
    emit summaryChanged();
}

void TrackListModel::removeTracks(const QList<qint64> &ids)
{
    bool anyPresent = false;
    for (const qint64 id : ids) {
        if (d->positionOf.contains(id)) {
            anyPresent = true;
            break;
        }
    }
    if (!anyPresent)
        return;
    beginResetModel();
    d->removeIds(QSet<qint64>(ids.begin(), ids.end()));
    d->reorder();
    endResetModel();
    emit summaryChanged();
}

void TrackListModel::setFilter(const SearchQuery &query)
{
    if (query.canonical() == d->filter.canonical())
        return;
    beginResetModel();
    d->filter = query;
    d->reorder();
    endResetModel();
    emit summaryChanged();
}

int TrackListModel::totalCount() const
{
    return int(d->tracks.size());
}

qint64 TrackListModel::trackId(int row) const
{
    if (row < 0 || row >= d->shown.size())
        return 0;
    return d->tracks.at(d->shown.at(row)).id;
}

int TrackListModel::rowOf(qint64 id) const
{
    return d->rowOf.value(id, -1);
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
    const LibraryTrack &track = d->tracks.at(d->shown.at(index.row()));
    const QString &field = d->columns.at(index.column());
    switch (role) {
    case Qt::DisplayRole: {
        const LibraryFieldSource source(track);
        return source.value(field).value_or(QString());
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
