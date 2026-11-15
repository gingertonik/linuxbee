// LOCKED ACCEPTANCE TEST (the album view: a fill shows the top). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/album-view.md (AV-VIEW). Runs on the
// offscreen platform.
#include <QScrollBar>
#include <QTreeWidget>
#include <QtTest>

#include "albumview/albumtracksview.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

// `albums` albums of five tracks each, every album its own artist.
QList<LibraryTrack> albumsOf(int albums)
{
    QList<LibraryTrack> out;
    qint64 id = 1;
    for (int a = 0; a < albums; ++a) {
        for (int t = 1; t <= 5; ++t) {
            LibraryTrack track;
            track.id = id++;
            track.path = QStringLiteral("/music/%1/%2.flac").arg(a).arg(t);
            track.tags.fields.insert(QStringLiteral("Album Artist"), { QStringLiteral("Artist %1").arg(a, 3, 10, QLatin1Char('0')) });
            track.tags.fields.insert(QStringLiteral("Album"), { QStringLiteral("Album %1").arg(a, 3, 10, QLatin1Char('0')) });
            track.tags.fields.insert(QStringLiteral("Track#"), { QString::number(t) });
            track.tags.fields.insert(QStringLiteral("Title"), { QStringLiteral("Title %1").arg(t) });
            track.tags.audio.durationMs = 180000;
            track.tags.format = QStringLiteral("flac");
            out.append(track);
        }
    }
    return out;
}

}

class AlbumViewTopTest : public QObject {
    Q_OBJECT
private slots:
    void aFillShowsTheTop();
};

void AlbumViewTopTest::aFillShowsTheTop()
{
    // AV-VIEW: after a fill the first heading is the first row shown, wherever
    // the view was scrolled to before.
    AlbumTracksView v;
    v.resize(600, 400);
    v.show();
    QTreeWidget *tree = v.findChild<QTreeWidget *>(QStringLiteral("albumTree"));
    if (!tree)
        QFAIL("the album view has not built its tree");
    const QList<LibraryTrack> tracks = albumsOf(60); // 360 rows, far more than the view shows
    v.setTracks(tracks);
    QCoreApplication::processEvents();
    QScrollBar *bar = tree->verticalScrollBar();
    QVERIFY2(bar->maximum() > 100, qPrintable(QString::number(bar->maximum())));

    // Scrolled far down, then filled again with the same tracks.
    bar->setValue(bar->maximum());
    QCoreApplication::processEvents();
    v.setTracks(tracks);
    QCoreApplication::processEvents();
    QCOMPARE(bar->value(), 0);

    // Scrolled a little, then filled with fewer tracks.
    bar->setValue(20);
    QCoreApplication::processEvents();
    v.setTracks(tracks.mid(0, 200));
    QCoreApplication::processEvents();
    QCOMPARE(bar->value(), 0);

    // Scrolled to the middle, then filled with more.
    bar->setValue(bar->maximum() / 2);
    QCoreApplication::processEvents();
    v.setTracks(tracks);
    QCoreApplication::processEvents();
    QCOMPARE(bar->value(), 0);
    QCOMPARE(tree->topLevelItem(0), tree->itemAt(QPoint(2, 2)));
}

QTEST_MAIN(AlbumViewTopTest)
#include "tst_ui_albumview_top.moc"
