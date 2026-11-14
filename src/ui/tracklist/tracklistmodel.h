#pragma once
#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QStringList>

#include "library/library.h"
#include "search/search.h"
#include "sort/fieldsort.h"

class QSettings;

// The rows of a track list: which tracks, which columns, in which order,
// narrowed by a search. Contract: docs/spec/track-list.md (T-MODEL).
// It is given tracks; it does not know where they come from.
namespace linuxbee::ui {

class TrackListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Roles {
        TrackIdRole = Qt::UserRole, // qint64
        MissingRole, // bool: the file was not there at the last scan
    };

    explicit TrackListModel(QObject *parent = nullptr);
    ~TrackListModel() override;

    static QStringList defaultColumns();
    static QStringList defaultSort();

    QStringList columns() const;
    bool setColumns(const QStringList &fieldNames); // false: empty, or a name twice

    // Sort keys in order of weight. A leading "-" means descending.
    QStringList sortFields() const;
    void setSortFields(const QStringList &fields);

    // T-GIVEN: the tracks in the order they were given, not sorted.
    bool givenOrder() const;
    void setGivenOrder(bool on);

    void setTracks(const QList<LibraryTrack> &tracks);
    void upsertTracks(const QList<LibraryTrack> &tracks); // add, or replace by id
    void removeTracks(const QList<qint64> &ids);
    void setFilter(const SearchQuery &query);

    int totalCount() const; // before the filter
    qint64 trackId(int row) const; // 0 for a row that does not exist
    int rowOf(qint64 id) const; // -1 when not shown
    QString summary() const; // T-SUMMARY over the rows shown

    void save(QSettings &settings, const QString &group) const;
    void load(QSettings &settings, const QString &group);

    int rowCount(const QModelIndex &parent = { }) const override;
    int columnCount(const QModelIndex &parent = { }) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

signals:
    void summaryChanged();

private:
    struct Private;
    Private *d;
};

}
