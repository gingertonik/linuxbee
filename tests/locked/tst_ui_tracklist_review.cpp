// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/track-list.md (T-PAGE: Commands,
// Selection). Select All and keeping the selection through a re-sort must
// cost about as much per track on a large list as on a small one: a list
// four times longer may take at most ten times as long (a cost per track
// that grows with the list shows as about sixteen).
#include <QElapsedTimer>
#include <QItemSelectionModel>
#include <QTreeView>
#include <QtTest>

#include "actions/actionregistry.h"
#include "sort/fieldsort.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using linuxbee::ui::ActionRegistry;
using linuxbee::ui::TrackListModel;
using linuxbee::ui::TrackListPage;

namespace {

QList<LibraryTrack> tracks(int count)
{
    QList<LibraryTrack> out;
    for (int i = 0; i < count; ++i) {
        LibraryTrack t;
        t.id = i + 1;
        t.path = QStringLiteral("/music/%1.flac").arg(i + 1, 6, 10, QLatin1Char('0'));
        t.tags.fields.insert(QStringLiteral("Artist"), { QStringLiteral("Artist %1").arg(i % 97) });
        t.tags.fields.insert(QStringLiteral("Title"), { QStringLiteral("Title %1").arg(i) });
        out << t;
    }
    return out;
}

// Milliseconds for Select All plus one re-sort with every row selected; the
// best of three runs.
qint64 selectAllCost(int count, int *selected)
{
    qint64 best = -1;
    for (int run = 0; run < 3; ++run) {
        TrackListModel model;
        ActionRegistry actions;
        TrackListPage page(&model, &actions);
        model.setColumns({ QStringLiteral("Artist"), QStringLiteral("Title") });
        model.setTracks(tracks(count));
        QElapsedTimer timer;
        timer.start();
        actions.action(QStringLiteral("edit.selectAll"))->trigger();
        model.sort(1, Qt::DescendingOrder);
        const qint64 spent = timer.elapsed();
        *selected = int(page.selectedTrackIds().size());
        if (best < 0 || spent < best)
            best = spent;
    }
    return best;
}

}

class TrackListReviewTest : public QObject {
    Q_OBJECT
private slots:
    void selectAllScales()
    {
        int small = 0;
        int large = 0;
        const qint64 smallMs = selectAllCost(1500, &small);
        const qint64 largeMs = selectAllCost(6000, &large);
        QCOMPARE(small, 1500);
        QCOMPARE(large, 6000);
        const double ratio = double(largeMs) / double(qMax<qint64>(smallMs, 5));
        qInfo() << "1500 tracks:" << smallMs << "ms, 6000 tracks:" << largeMs
                << "ms, ratio" << ratio;
        QVERIFY2(ratio < 10.0, qPrintable(QStringLiteral("ratio %1").arg(ratio)));
    }

    // The rows the view shows as selected are the selected tracks, no more.
    void viewShowsTheSelection()
    {
        TrackListModel model;
        ActionRegistry actions;
        TrackListPage page(&model, &actions);
        model.setTracks(tracks(20));
        auto *view = page.findChild<QTreeView *>(QStringLiteral("trackView"));
        QVERIFY(view);
        actions.action(QStringLiteral("edit.selectAll"))->trigger();
        QCOMPARE(view->selectionModel()->selectedRows().size(), 20);
        const QList<qint64> two = { model.trackId(3), model.trackId(7) };
        page.selectTracks(two);
        QCOMPARE(page.selectedTrackIds(), two);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
        model.sort(1, Qt::DescendingOrder);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
        QCOMPARE(page.selectedTrackIds().size(), 2);
    }

    // T-COMPARE, Duration: what is not m:ss or h:mm:ss (here 75 minutes in
    // the minutes place) is treated as a number, so "1:75:00" is 1.
    void durationFallsBackToNumber()
    {
        const QString time = QStringLiteral("Time");
        QVERIFY(compareFieldValues(time, QStringLiteral("1:75:00"), QStringLiteral("2:00")) < 0);
        QVERIFY(compareFieldValues(time, QStringLiteral("2:00"), QStringLiteral("1:75:00")) > 0);
        QVERIFY(compareFieldValues(time, QStringLiteral("1:75:00"), QStringLiteral("1")) == 0);
        QVERIFY(compareFieldValues(time, QStringLiteral("1:75"), QStringLiteral("1:75:00")) == 0);
        QVERIFY(compareFieldValues(time, QStringLiteral("1:75:00"), QStringLiteral("x")) < 0);
    }
};

QTEST_MAIN(TrackListReviewTest)
#include "tst_ui_tracklist_review.moc"
