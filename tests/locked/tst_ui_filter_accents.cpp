// LOCKED ACCEPTANCE TEST (the track list: accents heeded on request, folded again in pieces).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/track-list.md (T-MODEL, "Accents") and
// docs/spec/search.md (S-CACHE). The counters are for the whole process, so every check reads
// them before and after.
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

// `count` tracks: one in two by "Ártist n", the others by "Artist n".
QList<LibraryTrack> crowd(int count)
{
    QList<LibraryTrack> out;
    for (int i = 0; i < count; ++i) {
        LibraryTrack t;
        t.id = i + 1;
        t.path = QStringLiteral("/music/%1.flac").arg(i);
        const QString artist
            = (i % 2 == 0 ? QStringLiteral("\u00c1rtist %1") : QStringLiteral("Artist %1"))
                  .arg(i % 7);
        t.tags.fields.insert(s(u"Artist"), { artist });
        t.tags.fields.insert(s(u"Title"), { QStringLiteral("Song %1").arg(i) });
        out.append(t);
    }
    return out;
}

qint64 allOf(int tracks)
{
    return qint64(tracks) * defaultSearchFields().size();
}

constexpr int kWait = 10000;

}

class FilterAccentsTest : public QObject {
    Q_OBJECT
private slots:
    void byDefaultIgnored();
    void heededInPieces();
    void aFilterTellsTheFormsApart();
    void theSameValueDoesNothing();
    void backAgain();
};

void FilterAccentsTest::byDefaultIgnored()
{
    TrackListModel model;
    QVERIFY(model.searchIgnoresAccents());
    model.setTracks(crowd(300));
    model.setFilter(SearchQuery::parse(s(u"\"artist 3\"")));
    QCOMPARE(model.rowCount(), 300 / 7 + 1); // both spellings: i % 7 == 3 (a phrase: one term)
}

void FilterAccentsTest::heededInPieces()
{
    // T-MODEL "Accents": a change of the way folds nothing inside the call and leaves the rows as
    // they were; every track is folded again in the pieces of "Prepared ahead"; when the last
    // piece is done the rows are worked out again, folding nothing more.
    TrackListModel model;
    model.setTracks(crowd(2000));
    model.setFilter(SearchQuery::parse(s(u"\"artist 3\"")));
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    const int both = model.rowCount();
    QCOMPARE(both, 2000 / 7 + 1);
    const qint64 folded = searchValuesFolded();
    model.setSearchIgnoresAccents(false);
    QVERIFY(!model.searchIgnoresAccents());
    QCOMPARE(searchValuesFolded(), folded); // not inside the call
    QCOMPARE(model.rowCount(), both); // the rows as they were, for now
    QVERIFY(!model.searchPrepared());
    QCoreApplication::processEvents();
    const qint64 one = searchValuesFolded() - folded;
    QVERIFY2(one <= allOf(512), qPrintable(QString::number(one))); // at most two pieces
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    QTRY_COMPARE_WITH_TIMEOUT(
        model.rowCount(), 143, kWait); // "Artist n" only: odd i with i % 7 == 3
    QCOMPARE(searchValuesFolded() - folded, allOf(2000)); // each value once, the new way
    const qint64 after = searchValuesFolded();
    for (const char16_t *text :
        { u"\"artist 1\"", u"\"\u00e1rtist 1\"", u"song 7", u"title:song" }) {
        model.setFilter(SearchQuery::parse(s(text)));
        QVERIFY2(model.rowCount() > 0, qPrintable(s(text)));
    }
    QCOMPARE(searchValuesFolded(), after); // keystrokes fold nothing
}

void FilterAccentsTest::aFilterTellsTheFormsApart()
{
    // The canonical texts of "artist" and "ártist" are alike; the filter is still changed.
    TrackListModel model;
    model.setTracks(crowd(70));
    model.setSearchIgnoresAccents(false);
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    model.setFilter(SearchQuery::parse(s(u"\"artist 3\"")));
    QCOMPARE(model.rowCount(), 5); // odd i < 70 with i % 7 == 3: 3, 17, 31, 45, 59
    model.setFilter(SearchQuery::parse(s(u"\"\u00e1rtist 3\"")));
    QCOMPARE(model.rowCount(), 5); // even: 10, 24, 38, 52, 66
    QVERIFY(model.trackId(0) % 2 == 1); // ids are i + 1: an even i is an odd id
    model.setFilter(SearchQuery::parse(s(u"\"artist 3\"")));
    QVERIFY(model.trackId(0) % 2 == 0);
}

void FilterAccentsTest::theSameValueDoesNothing()
{
    TrackListModel model;
    model.setTracks(crowd(300));
    model.setFilter(SearchQuery::parse(s(u"artist")));
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy layout(&model, &QAbstractItemModel::layoutChanged);
    const qint64 folded = searchValuesFolded();
    model.setSearchIgnoresAccents(true);
    QVERIFY(model.searchPrepared());
    QTest::qWait(50);
    QCOMPARE(searchValuesFolded(), folded);
    QCOMPARE(reset.count() + layout.count(), 0);
}

void FilterAccentsTest::backAgain()
{
    TrackListModel model;
    model.setTracks(crowd(300));
    model.setSearchIgnoresAccents(false);
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    model.setFilter(SearchQuery::parse(s(u"\"artist 3\"")));
    const int heeded = model.rowCount();
    const qint64 folded = searchValuesFolded();
    model.setSearchIgnoresAccents(true);
    QCOMPARE(model.rowCount(), heeded);
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 300 / 7 + 1, kWait);
    QCOMPARE(searchValuesFolded() - folded, allOf(300));
    // With no filter, a change still folds again in pieces, and there are no rows to work out.
    model.setFilter(SearchQuery());
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    model.setSearchIgnoresAccents(false);
    QTRY_VERIFY_WITH_TIMEOUT(model.searchPrepared(), kWait);
    QCOMPARE(model.rowCount(), 300);
    QCOMPARE(reset.count(), 0);
}

QTEST_MAIN(FilterAccentsTest)
#include "tst_ui_filter_accents.moc"
