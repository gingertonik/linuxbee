// LOCKED ACCEPTANCE TEST (the track list: many tracks at once reset nothing). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/track-list.md (T-ROWS). The rows a
// model must show after a change are those of a second model that is given
// the same tracks afresh.
#include <QAbstractItemModelTester>
#include <QItemSelectionModel>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <QTreeView>
#include <QSignalSpy>
#include <QtTest>

#include "library/library.h"
#include "search/search.h"
#include "sort/fieldsort.h"
#include "actions/actionregistry.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

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

// The spies a layout change is seen by.
struct Layout {
    QSignalSpy about;
    QSignalSpy done;
    explicit Layout(TrackListModel &model)
        : about(&model, &QAbstractItemModel::layoutAboutToBeChanged)
        , done(&model, &QAbstractItemModel::layoutChanged)
    {
    }
    void clear()
    {
        about.clear();
        done.clear();
    }
};

}

class TrackListManyTest : public QObject {
    Q_OBJECT
private slots:
    void manyChangedAndNew();
    void manyRemoved();
    void theFilterAndTheGivenOrder();
    void aScanInParts();
    void thePageKeepsItsPlace();
};

void TrackListManyTest::manyChangedAndNew()
{
    // T-ROWS: more than 50 tracks at once are one layout change, not a reset; what pointed at a track still does.
    Rig r;
    Layout layout(r.model);
    const QPersistentModelIndex moving(r.model.index(r.model.rowOf(150), 1)); // its album changes below
    const QPersistentModelIndex staying(r.model.index(r.model.rowOf(7), 0)); // not named
    QItemSelectionModel selection(&r.model);
    selection.select(r.model.index(r.model.rowOf(150), 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection.select(r.model.index(r.model.rowOf(7), 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    const auto selected = [&] {
        QList<qint64> ids;
        for (const QModelIndex &index : selection.selectedRows())
            ids.append(r.model.trackId(index.row()));
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    QCOMPARE(selected(), QList<qint64>({ 7, 150 }));

    QList<LibraryTrack> many;
    for (qint64 id = 101; id <= 220; ++id)
        many.append(with(r.track(id), u"Album", QStringLiteral("Moved %1").arg(id % 9)));
    for (qint64 id = 1000; id < 1030; ++id)
        many.append(made(id, int(id % 60), 7, QStringLiteral("Added %1").arg(id)));
    const int rowBefore = moving.row();
    r.mark();
    r.upsert(many);
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(layout.about.count(), 1);
    QCOMPARE(layout.done.count(), 1);
    QCOMPARE(r.inserted.count() + r.removed.count() + r.moved.count(), 0); // one announcement, not 150
    QCOMPARE(r.summary.count(), 1);
    QVERIFY2(r.prepared() > 0 && r.prepared() <= 150 * kKeys.size(), qPrintable(QString::number(r.prepared())));
    QCOMPARE(r.compared(), 0);
    QCOMPARE(r.model.totalCount(), 330);
    QCOMPARE(r.asAfresh(), QString());
    // The indexes kept by others follow their tracks.
    QVERIFY(moving.isValid() && staying.isValid());
    QCOMPARE(r.model.trackId(moving.row()), 150);
    QCOMPARE(moving.column(), 1);
    QVERIFY(moving.row() != rowBefore);
    QCOMPARE(r.model.trackId(staying.row()), 7);
    QCOMPARE(selected(), QList<qint64>({ 7, 150 }));

    // Fifty-one is many; fifty is not (the rows are announced one by one, as before).
    QList<LibraryTrack> some;
    for (qint64 id = 1; id <= 51; ++id)
        some.append(with(r.track(id), u"Title", QStringLiteral("T %1").arg(id)));
    r.mark();
    layout.clear();
    r.upsert(some);
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(layout.done.count(), 1);
    QCOMPARE(r.asAfresh(), QString());
    some.removeLast();
    r.mark();
    layout.clear();
    r.upsert(some);
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(layout.done.count(), 0);
    QCOMPARE(r.changed.count(), 50);
    QCOMPARE(selected(), QList<qint64>({ 7, 150 }));
}

void TrackListManyTest::manyRemoved()
{
    // T-ROWS: more than 50 removed at once: a layout change; an index of a track that went is no longer valid.
    Rig r;
    Layout layout(r.model);
    const QPersistentModelIndex going(r.model.index(r.model.rowOf(100), 0));
    const QPersistentModelIndex staying(r.model.index(r.model.rowOf(250), 2));
    QList<qint64> gone;
    for (qint64 id = 60; id < 160; ++id)
        gone.append(id);
    gone.append(99999); // not held: no matter
    r.mark();
    r.remove(gone);
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(layout.about.count(), 1);
    QCOMPARE(layout.done.count(), 1);
    QCOMPARE(r.removed.count(), 0);
    QCOMPARE(r.prepared(), 0);
    QCOMPARE(r.summary.count(), 1);
    QCOMPARE(r.model.totalCount(), 200);
    QCOMPARE(r.asAfresh(), QString());
    QVERIFY(!going.isValid());
    QVERIFY(staying.isValid());
    QCOMPARE(r.model.trackId(staying.row()), 250);
    QCOMPARE(staying.column(), 2);
    // A change of one track afterwards lands where it should.
    r.mark();
    r.upsert({ with(r.track(250), u"Album", s(u"Album 1")) });
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(r.asAfresh(), QString());
    QCOMPARE(r.model.trackId(staying.row()), 250);
}

void TrackListManyTest::theFilterAndTheGivenOrder()
{
    // T-ROWS: with a filter, an index of a track that is no longer shown is not valid; in the given order new tracks go to the end.
    Rig r;
    Layout layout(r.model);
    QList<LibraryTrack> marked;
    for (qint64 id = 1; id <= 60; ++id)
        marked.append(with(r.track(id), u"Title", QStringLiteral("Marked %1").arg(id)));
    r.upsert(marked);
    r.filter(s(u"marked"));
    QCOMPARE(r.model.rowCount(), 60);
    const qint64 out = r.model.trackId(10);
    const qint64 in = r.model.trackId(40);
    const QPersistentModelIndex leaving(r.model.index(10, 0));
    const QPersistentModelIndex kept(r.model.index(40, 0));
    QList<LibraryTrack> many;
    for (int row = 0; row < 55; ++row) {
        const qint64 id = r.model.trackId(row);
        many.append(id == in ? with(r.track(id), u"Album Artist", s(u"Zzz")) : id == out ? with(r.track(id), u"Title", s(u"Quiet")) : r.track(id));
    }
    r.mark();
    layout.clear();
    r.upsert(many);
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(layout.done.count(), 1);
    QCOMPARE(r.model.rowCount(), 59);
    QCOMPARE(r.asAfresh(), QString());
    QVERIFY(!leaving.isValid());
    QVERIFY(kept.isValid());
    QCOMPARE(r.model.trackId(kept.row()), in);
    QCOMPARE(kept.row(), 58); // the last of those shown

    r.filter(QString());
    r.model.setGivenOrder(true);
    QList<LibraryTrack> added;
    for (qint64 id = 2000; id < 2060; ++id)
        added.append(made(id, 0, 1, QStringLiteral("Aaa %1").arg(id)));
    r.mark();
    layout.clear();
    r.upsert(added);
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(layout.done.count(), 1);
    QCOMPARE(r.prepared(), 0);
    QCOMPARE(r.model.rowOf(2000), 300);
    QCOMPARE(r.model.rowOf(2059), 359);
    QCOMPARE(r.asAfresh(), QString());
}

void TrackListManyTest::aScanInParts()
{
    // T-ROWS: a library that arrives in parts of 200 never resets the list.
    Rig r;
    Layout layout(r.model);
    r.mark();
    qint64 id = 5000;
    for (int part = 0; part < 5; ++part) {
        QList<LibraryTrack> found;
        for (int i = 0; i < 200; ++i, ++id)
            found.append(made(id, int(id % 97), int(id % 12) + 1, QStringLiteral("Found %1").arg(id)));
        r.upsert(found);
        QCOMPARE(r.model.totalCount(), 300 + 200 * (part + 1));
    }
    QCOMPARE(r.reset.count(), 0);
    QCOMPARE(layout.done.count(), 5);
    QCOMPARE(r.summary.count(), 5);
    QVERIFY2(r.prepared() <= 1000 * kKeys.size(), qPrintable(QString::number(r.prepared())));
    QCOMPARE(r.asAfresh(), QString());
}

void TrackListManyTest::thePageKeepsItsPlace()
{
    // T-PAGE with T-ROWS: tracks that arrive in a part leave the list where the user scrolled it, and the selection as it was.
    TrackListModel model;
    model.setSortFields(kKeys);
    QList<LibraryTrack> held = crowd();
    model.setTracks(held);
    ActionRegistry actions;
    TrackListPage page(&model, &actions);
    page.resize(700, 400);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    auto *view = page.findChild<QTreeView *>(s(u"trackView"));
    QVERIFY(view);
    QScrollBar *bar = view->verticalScrollBar();
    QVERIFY(bar->maximum() > 100);
    bar->setValue(bar->maximum() / 2);
    QCoreApplication::processEvents();
    const int place = bar->value();
    QVERIFY(place > 0);
    page.selectTracks({ 12, 200 });
    QSignalSpy selection(&page, &TrackListPage::selectionChanged);
    QList<qint64> before = page.selectedTrackIds();
    std::sort(before.begin(), before.end());
    QCOMPARE(before, QList<qint64>({ 12, 200 }));
    bar->setValue(place); // selecting may have scrolled
    QCoreApplication::processEvents();

    // 200 tracks that all sort after everything shown: nothing above the place moves.
    QList<LibraryTrack> found;
    for (qint64 id = 7000; id < 7200; ++id) {
        LibraryTrack t = made(id, 3, 1, QStringLiteral("Late %1").arg(id));
        t.tags.fields.insert(s(u"Album Artist"), { s(u"Zzz Late") });
        found.append(t);
    }
    model.upsertTracks(found);
    QCoreApplication::processEvents();
    QCOMPARE(model.rowCount(), 500);
    QCOMPARE(bar->value(), place); // not back at the top
    QList<qint64> after = page.selectedTrackIds();
    std::sort(after.begin(), after.end());
    QCOMPARE(after, before);
    QCOMPARE(selection.count(), 0);
}

QTEST_MAIN(TrackListManyTest)
#include "tst_ui_tracklist_many.moc"
