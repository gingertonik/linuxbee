// LOCKED ACCEPTANCE TEST (the track list: one track, one row). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/track-list.md (T-ROWS). The rows a
// model must show after a change are those of a second model that is given
// the same tracks afresh.
#include <QAbstractItemModelTester>
#include <QItemSelectionModel>
#include <QSignalSpy>
#include <QtTest>

#include "library/library.h"
#include "search/search.h"
#include "sort/fieldsort.h"
#include "tracklist/tracklistmodel.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

LibraryTrack made(qint64 id, int album, int number, const QString &title)
{
    LibraryTrack t;
    t.id = id;
    t.path = QStringLiteral("/music/%1/%2-%3.flac").arg(album).arg(number).arg(id);
    const QString artist = QStringLiteral("Artist %1").arg(album % 17);
    t.tags.fields.insert(s(u"Album Artist"), { artist });
    t.tags.fields.insert(s(u"Artist"), { artist });
    t.tags.fields.insert(s(u"Album"), { QStringLiteral("Album %1").arg(album) });
    t.tags.fields.insert(s(u"Track#"), { QString::number(number) });
    t.tags.fields.insert(s(u"Title"), { title });
    t.tags.audio.durationMs = 1000 * (60 + number);
    t.sizeBytes = 1000 + id;
    t.tags.format = QStringLiteral("flac");
    return t;
}

// 60 albums of 5 tracks, given out of order; ids 1 to 300.
QList<LibraryTrack> crowd()
{
    QList<LibraryTrack> out;
    qint64 id = 0;
    for (int n = 5; n >= 1; --n) {
        for (int a = 59; a >= 0; --a) {
            ++id;
            out.append(made(id, a, n, QStringLiteral("Song %1 of %2").arg(n).arg(a)));
        }
    }
    return out;
}

const QStringList kKeys = { QStringLiteral("Album Artist"), QStringLiteral("Album"), QStringLiteral("Track#") };

QList<qint64> rows(const TrackListModel &model)
{
    QList<qint64> out;
    for (int row = 0; row < model.rowCount(); ++row)
        out.append(model.trackId(row));
    return out;
}

// One model, what it is told, and what a model given the same tracks afresh would show.
struct Rig {
    TrackListModel model;
    QAbstractItemModelTester tester { &model, QAbstractItemModelTester::FailureReportingMode::QtTest };
    QList<LibraryTrack> held; // in the order given, replaced in place
    QString filterText;
    QSignalSpy reset { &model, &QAbstractItemModel::modelReset };
    QSignalSpy inserted { &model, &QAbstractItemModel::rowsInserted };
    QSignalSpy removed { &model, &QAbstractItemModel::rowsRemoved };
    QSignalSpy moved { &model, &QAbstractItemModel::rowsMoved };
    QSignalSpy changed { &model, &QAbstractItemModel::dataChanged };
    QSignalSpy summary { &model, &TrackListModel::summaryChanged };
    qint64 preparedAt = 0;
    qint64 comparedAt = 0;

    Rig()
    {
        model.setSortFields(kKeys);
        held = crowd();
        model.setTracks(held);
        mark();
    }
    void mark()
    {
        reset.clear();
        inserted.clear();
        removed.clear();
        moved.clear();
        changed.clear();
        summary.clear();
        preparedAt = fieldValuesPrepared();
        comparedAt = fieldValueComparisons();
    }
    qint64 prepared() const { return fieldValuesPrepared() - preparedAt; }
    qint64 compared() const { return fieldValueComparisons() - comparedAt; }
    LibraryTrack track(qint64 id) const
    {
        for (const LibraryTrack &t : held) {
            if (t.id == id)
                return t;
        }
        return { };
    }
    void upsert(const QList<LibraryTrack> &tracks)
    {
        for (const LibraryTrack &t : tracks) {
            bool known = false;
            for (LibraryTrack &h : held) {
                if (h.id == t.id) {
                    h = t;
                    known = true;
                }
            }
            if (!known)
                held.append(t);
        }
        model.upsertTracks(tracks);
    }
    void remove(const QList<qint64> &ids)
    {
        held.erase(std::remove_if(held.begin(), held.end(), [&ids](const LibraryTrack &t) { return ids.contains(t.id); }), held.end());
        model.removeTracks(ids);
    }
    void filter(const QString &text)
    {
        filterText = text;
        model.setFilter(SearchQuery::parse(text));
    }
    // "" when the model shows what a model given `held` afresh shows; else what differs.
    QString asAfresh() const
    {
        TrackListModel other;
        other.setSortFields(model.sortFields());
        other.setGivenOrder(model.givenOrder());
        other.setTracks(held);
        other.setFilter(SearchQuery::parse(filterText));
        if (rows(model) != rows(other))
            return QStringLiteral("the rows differ from those of a model given the same tracks afresh");
        if (model.summary() != other.summary())
            return QStringLiteral("summary: ") + model.summary() + QStringLiteral(" / ") + other.summary();
        if (model.totalCount() != other.totalCount())
            return QStringLiteral("totalCount differs");
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.rowOf(model.trackId(row)) != row)
                return QStringLiteral("rowOf is wrong at row %1").arg(row);
            for (int column = 0; column < model.columnCount(); ++column) {
                if (model.data(model.index(row, column)) != other.data(other.index(row, column)))
                    return QStringLiteral("a cell differs at row %1").arg(row);
            }
        }
        return QString();
    }
};

LibraryTrack with(LibraryTrack t, const char16_t *field, const QString &value)
{
    t.tags.fields.insert(s(field), { value });
    return t;
}

}

class TrackListRowsTest : public QObject {
    Q_OBJECT
private slots:
    void aChangedTrackIsOneRow();
    void aTrackArrivesAndLeaves();
    void theFilterDecides();
    void inTheGivenOrder();
    void manyAtOnce();
    void afterAnotherSort();
};

void TrackListRowsTest::aChangedTrackIsOneRow()
{
    // T-ROWS: a replaced track costs its own keys and one row's signal; the selection follows it.
    Rig r;
    QCOMPARE(r.model.rowCount(), 300);
    QItemSelectionModel selection(&r.model);
    const qint64 id = r.model.trackId(100);
    const qint64 other = r.model.trackId(7);
    selection.select(r.model.index(100, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection.select(r.model.index(7, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    const auto selected = [&] {
        QList<qint64> ids;
        for (const QModelIndex &index : selection.selectedRows())
            ids.append(r.model.trackId(index.row()));
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    QList<qint64> both { id, other };
    std::sort(both.begin(), both.end());

    // A change that is not in a sort key: the row stays where it is.
    r.mark();
    r.upsert({ with(r.track(id), u"Title", s(u"Another name")) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.inserted.count() + r.removed.count() + r.moved.count(), 0);
    QCOMPARE(r.changed.count(), 1);
    QCOMPARE(r.changed.at(0).at(0).toModelIndex().row(), 100);
    QCOMPARE(r.changed.at(0).at(0).toModelIndex().column(), 0);
    QCOMPARE(r.changed.at(0).at(1).toModelIndex().column(), r.model.columnCount() - 1);
    QCOMPARE(r.summary.count(), 1);
    QVERIFY2(r.prepared() <= kKeys.size(), qPrintable(QString::number(r.prepared())));
    QCOMPARE(r.compared(), 0);
    QCOMPARE(r.model.rowOf(id), 100);
    QCOMPARE(r.asAfresh(), QString());
    QCOMPARE(selected(), both);

    // A change in a sort key: the row moves, nothing is reset, the selection goes with it.
    r.mark();
    r.upsert({ with(r.track(id), u"Album", s(u"Album 9999")) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.inserted.count() + r.removed.count(), 0);
    QCOMPARE(r.moved.count(), 1);
    QVERIFY2(r.prepared() > 0 && r.prepared() <= kKeys.size(), qPrintable(QString::number(r.prepared())));
    QCOMPARE(r.compared(), 0);
    QVERIFY(r.model.rowOf(id) != 100);
    QCOMPARE(r.asAfresh(), QString());
    QCOMPARE(selected(), both);

    // Up as well as down, and to the first and the last row.
    for (const char16_t *artist : { u"Aaa first", u"Zzz last", u"Artist 5" }) {
        r.mark();
        r.upsert({ with(r.track(id), u"Album Artist", s(artist)) });
        QCOMPARE(r.reset.count(), 0);
        QCOMPARE(r.asAfresh(), QString());
        QCOMPARE(selected(), both);
    }
    QVERIFY(r.model.rowOf(id) > 0 && r.model.rowOf(id) < 299);

    // The same track again, as it is: one dataChanged, no move.
    r.mark();
    r.upsert({ r.track(id) });
    QCOMPARE(r.reset.count() + r.moved.count() + r.inserted.count() + r.removed.count(), 0);
    QCOMPARE(r.changed.count(), 1);

    // Three at once: three rows' worth, one summary.
    r.mark();
    r.upsert({ with(r.track(1), u"Track#", s(u"40")), with(r.track(2), u"Title", s(u"x")), with(r.track(3), u"Album", s(u"Album 0")) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.summary.count(), 1);
    QVERIFY2(r.prepared() <= 3 * kKeys.size(), qPrintable(QString::number(r.prepared())));
    QCOMPARE(r.asAfresh(), QString());
}

void TrackListRowsTest::aTrackArrivesAndLeaves()
{
    // T-ROWS: a new track is one inserted row; a removed one is one removed row and prepares nothing.
    Rig r;
    r.upsert({ made(900, 30, 3, s(u"New in the middle")) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.inserted.count(), 1);
    QCOMPARE(r.removed.count() + r.moved.count(), 0);
    QCOMPARE(r.inserted.at(0).at(1).toInt(), r.model.rowOf(900));
    QVERIFY2(r.prepared() > 0 && r.prepared() <= kKeys.size(), qPrintable(QString::number(r.prepared())));
    QCOMPARE(r.model.rowCount(), 301);
    QCOMPARE(r.asAfresh(), QString());

    r.mark();
    const int row = r.model.rowOf(150);
    r.remove({ 150 });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.removed.count(), 1);
    QCOMPARE(r.removed.at(0).at(1).toInt(), row);
    QCOMPARE(r.prepared(), 0);
    QCOMPARE(r.summary.count(), 1);
    QCOMPARE(r.model.rowOf(150), -1);
    QCOMPARE(r.asAfresh(), QString());

    // The first, the last, and several: still rows, and nothing prepared.
    r.mark();
    r.remove({ r.model.trackId(0), r.model.trackId(r.model.rowCount() - 1), 900, 12, 13 });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.removed.count(), 5);
    QCOMPARE(r.prepared(), 0);
    QCOMPARE(r.asAfresh(), QString());

    // An id the model does not hold: nothing at all.
    r.mark();
    r.remove({ 123456 });
    QCOMPARE(r.reset.count() + r.removed.count() + r.summary.count(), 0);

    // What was removed can come back.
    r.mark();
    r.upsert({ made(150, 2, 2, s(u"Back")) });
    QCOMPARE(r.inserted.count(), 1);
    QCOMPARE(r.asAfresh(), QString());
}

void TrackListRowsTest::theFilterDecides()
{
    // T-ROWS: with a filter, a changed track is shown only when the filter accepts it as it is now.
    Rig r;
    r.filter(s(u"zebra"));
    QCOMPARE(r.model.rowCount(), 0);
    QCOMPARE(r.model.totalCount(), 300);

    // Not shown, and now accepted: its row comes.
    r.mark();
    r.upsert({ with(r.track(10), u"Title", s(u"Zebra one")) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.inserted.count(), 1);
    QCOMPARE(r.model.rowCount(), 1);
    QCOMPARE(r.asAfresh(), QString());
    r.upsert({ with(r.track(200), u"Title", s(u"Zebra two")), with(r.track(90), u"Title", s(u"Zebra three")) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.model.rowCount(), 3);
    QCOMPARE(r.asAfresh(), QString());

    // Shown, changed, and still accepted: it stays, in the row its keys give.
    r.mark();
    r.upsert({ with(r.track(200), u"Album Artist", s(u"Aaa")) });
    QCOMPARE(r.reset.count() + r.inserted.count() + r.removed.count(), 0);
    QCOMPARE(r.model.rowOf(200), 0);
    QCOMPARE(r.asAfresh(), QString());

    // Shown, and no longer accepted: its row goes.
    r.mark();
    r.upsert({ with(r.track(10), u"Title", s(u"Quiet")) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.removed.count(), 1);
    QCOMPARE(r.inserted.count() + r.moved.count(), 0);
    QCOMPARE(r.model.rowCount(), 2);
    QCOMPARE(r.model.totalCount(), 300);
    QCOMPARE(r.asAfresh(), QString());

    // Not shown, and still not accepted: no signal about rows.
    r.mark();
    r.upsert({ with(r.track(11), u"Title", s(u"Still quiet")) });
    QCOMPARE(r.reset.count() + r.removed.count() + r.inserted.count() + r.moved.count() + r.changed.count(), 0);
    QCOMPARE(r.asAfresh(), QString());

    // A new track the filter refuses is held and not shown; one it accepts is shown.
    r.mark();
    r.upsert({ made(901, 4, 1, s(u"Hidden")) });
    QCOMPARE(r.inserted.count(), 0);
    QCOMPARE(r.model.totalCount(), 301);
    r.upsert({ made(902, 4, 2, s(u"A zebra arrives")) });
    QCOMPARE(r.inserted.count(), 1);
    QCOMPARE(r.model.rowCount(), 3);
    QCOMPARE(r.asAfresh(), QString());

    // A hidden track removed: no row signal, and it is gone when the filter is.
    r.mark();
    r.remove({ 11 });
    QCOMPARE(r.reset.count() + r.removed.count(), 0);
    QCOMPARE(r.model.totalCount(), 301);
    r.filter(QString());
    QCOMPARE(r.model.rowCount(), 301);
    QCOMPARE(r.model.rowOf(11), -1);
    QCOMPARE(r.asAfresh(), QString());
}

void TrackListRowsTest::inTheGivenOrder()
{
    // T-ROWS with T-GIVEN: a replaced track keeps its place, a new one goes to the end, nothing is prepared.
    Rig r;
    r.model.setGivenOrder(true);
    QCOMPARE(rows(r.model).first(), 1);
    r.mark();
    r.upsert({ with(r.track(40), u"Album Artist", s(u"Zzz")) });
    QCOMPARE(r.reset.count() + r.moved.count() + r.inserted.count() + r.removed.count(), 0);
    QCOMPARE(r.changed.count(), 1);
    QCOMPARE(r.model.rowOf(40), 39);
    QCOMPARE(r.prepared(), 0);
    r.mark();
    r.upsert({ made(902, 0, 1, s(u"Aaa")) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.inserted.count(), 1);
    QCOMPARE(r.model.rowOf(902), 300);
    QCOMPARE(r.prepared(), 0);
    r.mark();
    r.remove({ 1 });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.removed.count(), 1);
    QCOMPARE(rows(r.model).first(), 2);
    QCOMPARE(r.asAfresh(), QString());
    // Sorted again, the tracks that changed meanwhile fall where their values put them.
    r.model.setGivenOrder(false);
    QCOMPARE(r.asAfresh(), QString());
}

void TrackListRowsTest::manyAtOnce()
{
    // T-ROWS: up to 50 tracks at once reset nothing; more may reset once. Either way only the tracks named are prepared.
    Rig r;
    QList<LibraryTrack> fifty;
    for (qint64 id = 1; id <= 50; ++id)
        fifty.append(with(r.track(id), u"Album", QStringLiteral("Moved %1").arg(id % 7)));
    r.mark();
    r.upsert(fifty);
    QCOMPARE(r.reset.count(), 0);
    QVERIFY2(r.prepared() <= 50 * kKeys.size(), qPrintable(QString::number(r.prepared())));
    QCOMPARE(r.summary.count(), 1);
    QCOMPARE(r.asAfresh(), QString());

    QList<LibraryTrack> many;
    for (qint64 id = 101; id <= 220; ++id)
        many.append(with(r.track(id), u"Track#", QString::number(id % 9)));
    for (qint64 id = 1000; id < 1030; ++id)
        many.append(made(id, int(id % 60), 7, QStringLiteral("Added %1").arg(id)));
    r.mark();
    r.upsert(many);
    QVERIFY(r.reset.count() <= 1);
    QVERIFY2(r.prepared() > 0 && r.prepared() <= 150 * kKeys.size(), qPrintable(QString::number(r.prepared())));
    QCOMPARE(r.compared(), 0);
    QCOMPARE(r.summary.count(), 1);
    QCOMPARE(r.model.totalCount(), 330);
    QCOMPARE(r.asAfresh(), QString());

    QList<qint64> gone;
    for (qint64 id = 60; id < 160; ++id)
        gone.append(id);
    r.mark();
    r.remove(gone);
    QVERIFY(r.reset.count() <= 1);
    QCOMPARE(r.prepared(), 0);
    QCOMPARE(r.model.totalCount(), 230);
    QCOMPARE(r.asAfresh(), QString());
    // And one more change after a large one lands where it should.
    r.mark();
    r.upsert({ with(r.track(250), u"Album", s(u"Album 1")) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.asAfresh(), QString());
}

void TrackListRowsTest::afterAnotherSort()
{
    // T-ROWS: the keys kept are those of the sort in force.
    Rig r;
    r.model.setSortFields({ s(u"-Title") });
    r.mark();
    r.upsert({ with(r.track(77), u"Title", s(u"Zzz top")) });
    QCOMPARE(r.reset.count(), 0);
    QVERIFY2(r.prepared() > 0 && r.prepared() <= 1, qPrintable(QString::number(r.prepared())));
    QCOMPARE(r.model.rowOf(77), 0);
    QCOMPARE(r.asAfresh(), QString());
    // A header click sorts by that column and the default keys after it.
    r.model.sort(int(r.model.columns().indexOf(s(u"Album"))), Qt::AscendingOrder);
    r.mark();
    r.upsert({ with(r.track(77), u"Album", s(u"Aaa")) });
    QCOMPARE(r.reset.count(), 0);
    QVERIFY2(r.prepared() <= r.model.sortFields().size(), qPrintable(QString::number(r.prepared())));
    QCOMPARE(r.model.rowOf(77), 0);
    QCOMPARE(r.asAfresh(), QString());
    // A blank value goes last, in a row of its own.
    r.mark();
    r.upsert({ with(r.track(77), u"Album", QString()) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.model.rowOf(77), r.model.rowCount() - 1);
    QCOMPARE(r.asAfresh(), QString());
}

QTEST_MAIN(TrackListRowsTest)
#include "tst_ui_tracklist_rows.moc"
