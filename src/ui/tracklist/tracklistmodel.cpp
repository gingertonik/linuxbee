#include "tracklist/tracklistmodel.h"

// Stub. Plan 11 replaces every body in this file.
namespace linuxbee::ui {

struct TrackListModel::Private { };

TrackListModel::TrackListModel(QObject *parent)
    : QAbstractTableModel(parent)
    , d(nullptr)
{
}

TrackListModel::~TrackListModel()
{
    Q_UNUSED(d);
}

QStringList TrackListModel::defaultColumns()
{
    return { };
}

QStringList TrackListModel::defaultSort()
{
    return { };
}

QStringList TrackListModel::columns() const
{
    return { };
}

bool TrackListModel::setColumns(const QStringList &fieldNames)
{
    Q_UNUSED(fieldNames);
    return false;
}

QStringList TrackListModel::sortFields() const
{
    return { };
}

void TrackListModel::setSortFields(const QStringList &fields)
{
    Q_UNUSED(fields);
}

void TrackListModel::setTracks(const QList<LibraryTrack> &tracks)
{
    Q_UNUSED(tracks);
}

void TrackListModel::upsertTracks(const QList<LibraryTrack> &tracks)
{
    Q_UNUSED(tracks);
}

void TrackListModel::removeTracks(const QList<qint64> &ids)
{
    Q_UNUSED(ids);
}

void TrackListModel::setFilter(const SearchQuery &query)
{
    Q_UNUSED(query);
}

int TrackListModel::totalCount() const
{
    return 0;
}

qint64 TrackListModel::trackId(int row) const
{
    Q_UNUSED(row);
    return 0;
}

int TrackListModel::rowOf(qint64 id) const
{
    Q_UNUSED(id);
    return -1;
}

QString TrackListModel::summary() const
{
    return { };
}

void TrackListModel::save(QSettings &settings, const QString &group) const
{
    Q_UNUSED(settings);
    Q_UNUSED(group);
}

void TrackListModel::load(QSettings &settings, const QString &group)
{
    Q_UNUSED(settings);
    Q_UNUSED(group);
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 0;
}

int TrackListModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 0;
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    Q_UNUSED(index);
    Q_UNUSED(role);
    return { };
}

QVariant TrackListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(section);
    Q_UNUSED(orientation);
    Q_UNUSED(role);
    return { };
}

void TrackListModel::sort(int column, Qt::SortOrder order)
{
    Q_UNUSED(column);
    Q_UNUSED(order);
}

}
