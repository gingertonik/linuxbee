// LOCKED ACCEPTANCE TEST (the track list folds ahead, so that a first keystroke folds nothing).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/track-list.md (T-MODEL, "Prepared ahead") and
// docs/spec/search.md (S-CACHE, prepareSearch). The counters are for the whole process, so every
// check reads them before and after.
#include <QtTest>

#include "library/library.h"
#include "search/search.h"
#include "tracklist/tracklistmodel.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// Tracks in `albums` albums of `perAlbum` tracks, given out of order.
QList<LibraryTrack> crowd(int albums, int perAlbum)
{
    QList<LibraryTrack> out;
    qint64 id = 0;
    for (int n = perAlbum; n >= 1; --n) {
        for (int a = albums - 1; a >= 0; --a) {
            LibraryTrack t;
            t.id = ++id;
            t.path = QStringLiteral("/music/%1/%2.flac").arg(a).arg(n);
            const QString artist
                = (a % 3 == 0 ? QStringLiteral("The Artist %1") : QStringLiteral("Ártist %1"))
                      .arg(a % 17);
            t.tags.fields.insert(s(u"Album Artist"), { artist });
            t.tags.fields.insert(s(u"Artist"), { artist });
            t.tags.fields.insert(s(u"Album"), { QStringLiteral("Album %1").arg(a) });
            t.tags.fields.insert(s(u"Year"), { QString::number(1960 + a % 60) });
            t.tags.fields.insert(s(u"Track#"), { QString::number(n) });
            t.tags.fields.insert(s(u"Title"), { QStringLiteral("Söng %1 of %2").arg(n).arg(a) });
            if (a % 4 == 0)
                t.tags.fields.insert(s(u"Genre"), { s(u"Rock") });
            t.tags.audio.durationMs = 1000 * (60 + n);
            t.tags.format = QStringLiteral("flac");
            out.append(t);
        }
    }
    return out;
}

// Every track of crowd() knows every default search field (S-FIELDS): the catalogue's fields
// are known with or without a value, and Filename is the file's name.
qint64 allOf(int tracks)
{
    return qint64(tracks) * defaultSearchFields().size();
}

constexpr int kWait = 10000;

}

class FilterAheadTest : public QObject {
    Q_OBJECT
private slots:
    void prepareSearchMakesTheValue();
    void nothingInsideTheCalls();
    void aheadThenAFirstKeystrokeFoldsNothing();
    void inPieces();
    void aFilterBeforeThePiecesAreDone();
    void aReplacedTrackIsPreparedAgain();
    void noRowChanges();
    void goneWhileWorking();
};

void FilterAheadTest::prepareSearchMakesTheValue()
{
    // S-CACHE: prepareSearch makes what a term with no field would make, and no more.
    const LibraryTrack track = crowd(1, 1).first();
    const LibraryFieldSource source(track);
    SearchCache ahead;
    qint64 folded = searchValuesFolded();
    prepareSearch(source, { }, &ahead);
    QCOMPARE(searchValuesFolded() - folded, allOf(1));
    QVERIFY(ahead.joinedMade);
    QCOMPARE(ahead.joinedFor, defaultSearchFields());
    SearchCache plain;
    QVERIFY(!SearchQuery::parse(s(u"zzzz")).matches(source, { }, &plain));
    QCOMPARE(ahead.joined, plain.joined);
    folded = searchValuesFolded();
    prepareSearch(source, { }, &ahead); // made already: nothing
    QVERIFY(SearchQuery::parse(s(u"s\u00d6ng")).matches(source, { }, &ahead));
    QVERIFY(SearchQuery::parse(s(u"title:song")).matches(source, { }, &ahead));
    QCOMPARE(searchValuesFolded(), folded);
    SearchOptions titleOnly;
    titleOnly.fields = { s(u"Title") };
    prepareSearch(source, titleOnly, &ahead); // another list: made again, from what is held
    QCOMPARE(ahead.joinedFor, titleOnly.fields);
    QCOMPARE(searchValuesFolded(), folded);
    prepareSearch(source, { }, nullptr); // no cache: nothing, and no crash
    QCOMPARE(searchValuesFolded(), folded);
}

void FilterAheadTest::nothingInsideTheCalls()
{
    // T-MODEL, "Prepared ahead": nothing is folded inside the calls that give or change tracks.
    const QList<LibraryTrack> tracks = crowd(60, 5);
    TrackListModel model;
    qint64 folded = searchValuesFolded();
    model.setTracks(tracks);
    QCOMPARE(searchValuesFolded(), folded);
    QVERIFY(!model.searchPrepared());
    LibraryTrack changed = tracks.at(3);
    changed.tags.fields.insert(s(u"Title"), { s(u"Other") });
    model.upsertTracks({ changed });
    model.removeTracks({ tracks.at(4).id });
    QCOMPARE(searchValuesFolded(), folded);
    QVERIFY(!model.searchPrepared());
}

void FilterAheadTest::aheadThenAFirstKeystrokeFoldsNothing()
{
    const QList<LibraryTrack> tracks = crowd(60, 5);
    TrackListModel model;
    const qint64 folded = searchValuesFolded();
    model.setTracks(tracks);
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    QCOMPARE(searchValuesFolded() - folded, allOf(300)); // each value once
    const qint64 after = searchValuesFolded();
    model.setFilter(SearchQuery::parse(s(u"zzzz")));
    QCOMPARE(model.rowCount(), 0);
    for (const char16_t *text : { u"s", u"so", u"s\u00f6ng 1", u"artist 3", u"\"of 1\"",
             u"genre:rock", u"title:song", u"-zzzz" }) {
        model.setFilter(SearchQuery::parse(s(text)));
        QVERIFY2(model.rowCount() > 0, qPrintable(s(text)));
    }
    QCOMPARE(searchValuesFolded(), after);
    QVERIFY(model.searchPrepared());
}

void FilterAheadTest::inPieces()
{
    // T-MODEL: in pieces of at most 256 tracks, one piece for each turn of the event loop, so
    // the window keeps answering. One turn folds at most two pieces' worth.
    const QList<LibraryTrack> tracks = crowd(400, 5);
    TrackListModel model;
    const qint64 folded = searchValuesFolded();
    model.setTracks(tracks);
    QCoreApplication::processEvents();
    const qint64 one = searchValuesFolded() - folded;
    QVERIFY2(one <= allOf(512), qPrintable(QString::number(one)));
    QVERIFY(!model.searchPrepared());
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    QCOMPARE(searchValuesFolded() - folded, allOf(2000));
}

void FilterAheadTest::aFilterBeforeThePiecesAreDone()
{
    // A filter set before the pieces are done answers as it would have; each value is still
    // folded once in all.
    const QList<LibraryTrack> tracks = crowd(60, 5);
    TrackListModel model;
    const qint64 folded = searchValuesFolded();
    model.setTracks(tracks);
    model.setFilter(SearchQuery::parse(s(u"\u00e1rtist 4")));
    const int rows = model.rowCount();
    QVERIFY(rows > 0 && rows < 300);
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    QCOMPARE(model.rowCount(), rows);
    QCOMPARE(searchValuesFolded() - folded, allOf(300));
    model.setFilter(SearchQuery::parse(s(u"zzzz")));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(searchValuesFolded() - folded, allOf(300));
}

void FilterAheadTest::aReplacedTrackIsPreparedAgain()
{
    QList<LibraryTrack> tracks = crowd(60, 5);
    TrackListModel model;
    model.setTracks(tracks);
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    LibraryTrack changed = tracks.at(7);
    changed.tags.fields.insert(s(u"Title"), { s(u"Zzzz now") });
    qint64 folded = searchValuesFolded();
    model.upsertTracks({ changed });
    QCOMPARE(searchValuesFolded(), folded); // not inside the call
    QVERIFY(!model.searchPrepared());
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    QCOMPARE(searchValuesFolded() - folded, allOf(1)); // that track only
    folded = searchValuesFolded();
    model.setFilter(SearchQuery::parse(s(u"zzzz")));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(searchValuesFolded(), folded);
    // More than 50 at once is the other path of upsertTracks; it is prepared again too.
    QList<LibraryTrack> many;
    for (int i = 0; i < 60; ++i) {
        LibraryTrack t = tracks.at(i);
        t.tags.fields.insert(s(u"Comment"), { s(u"zzzz") });
        many.append(t);
    }
    model.setFilter(SearchQuery());
    folded = searchValuesFolded();
    model.upsertTracks(many);
    QCOMPARE(searchValuesFolded(), folded);
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    QCOMPARE(searchValuesFolded() - folded, allOf(60));
    model.setFilter(SearchQuery::parse(s(u"zzzz")));
    QCOMPARE(model.rowCount(), 60); // the changed track is among the 60, its title as before
}

void FilterAheadTest::noRowChanges()
{
    // Folding ahead changes no row, so a view is told nothing.
    TrackListModel model;
    model.setTracks(crowd(60, 5));
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy layout(&model, &QAbstractItemModel::layoutChanged);
    QSignalSpy data(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy summary(&model, &TrackListModel::summaryChanged);
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    QCOMPARE(reset.count() + layout.count() + data.count() + inserted.count() + removed.count()
            + summary.count(),
        0);
}

void FilterAheadTest::goneWhileWorking()
{
    // A model thrown away in the middle of its pieces leaves nothing behind that runs.
    auto *model = new TrackListModel;
    model->setTracks(crowd(400, 5));
    QCoreApplication::processEvents();
    delete model;
    const qint64 folded = searchValuesFolded();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    QCOMPARE(searchValuesFolded(), folded);
}

QTEST_MAIN(FilterAheadTest)
#include "tst_ui_filter_ahead.moc"
