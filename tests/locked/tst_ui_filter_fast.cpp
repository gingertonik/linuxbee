// LOCKED ACCEPTANCE TEST (a keystroke in the search box neither sorts nor folds). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/search.md (S-CACHE) and
// docs/spec/track-list.md (T-MODEL, "Kept between changes"). The counters are
// for the whole process, so every check reads them before and after.
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
            const QString artist = (a % 3 == 0 ? QStringLiteral("The Artist %1") : QStringLiteral("Ártist %1")).arg(a % 17);
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

QList<qint64> rowsOf(const TrackListModel &model)
{
    QList<qint64> ids;
    for (int row = 0; row < model.rowCount(); ++row)
        ids.append(model.trackId(row));
    return ids;
}

}

class FilterFastTest : public QObject {
    Q_OBJECT
private slots:
    void aCacheAnswersTheSame();
    void aCacheFoldsOnce();
    void aKeystrokeNeitherSortsNorFolds();
    void aChangedTrackIsFoldedAgain();
};

void FilterFastTest::aCacheAnswersTheSame()
{
    // S-CACHE: with a cache the answer is that of S-MATCH, whatever the cache has seen before.
    const QList<LibraryTrack> tracks = crowd(12, 3);
    const QStringList queries = { s(u"song"), s(u"SÖNG"), s(u"song 1"), s(u"artist"), s(u"the"), s(u"genre:rock"), s(u"genre:"),
        s(u"-genre:"), s(u"-genre:rock song"), s(u"year:196"), s(u"nosuchfield:x"), s(u"-nosuchfield:x"), s(u"1 OR 2"),
        s(u"\"of 1\""), s(u"artist:\"artist 3\""), s(u"zzzz"), s(u"-zzzz"), s(u"flac"), s(u"filename:2"), QString() };
    int matched = 0;
    for (const LibraryTrack &track : tracks) {
        const LibraryFieldSource source(track);
        SearchCache shared; // one cache for this track through every query
        for (const QString &text : queries) {
            const SearchQuery query = SearchQuery::parse(text);
            const bool want = query.matches(source);
            SearchCache fresh;
            if (query.matches(source, { }, &fresh) != want || query.matches(source, { }, &shared) != want
                || query.matches(source, { }, &shared) != want)
                QFAIL(qPrintable(QStringLiteral("[%1] on track %2").arg(text).arg(track.id)));
            matched += want ? 1 : 0;
        }
    }
    QVERIFY(matched > 36 && matched < 36 * 20); // the queries tell tracks apart
    // Other search fields than the default: the cache is asked for those.
    SearchOptions titleOnly;
    titleOnly.fields = { s(u"Title") };
    const LibraryFieldSource first(tracks.first());
    SearchCache cache;
    QVERIFY(SearchQuery::parse(s(u"artist")).matches(first, { }, &cache));
    QVERIFY(!SearchQuery::parse(s(u"artist")).matches(first, titleOnly, &cache));
    QCOMPARE(SearchQuery::parse(s(u"artist")).matches(first, titleOnly), false);
}

void FilterFastTest::aCacheFoldsOnce()
{
    // S-CACHE: a value is folded when the cache does not have it, and only then.
    const LibraryTrack track = crowd(1, 1).first(); // album 0: it has a Genre
    const LibraryFieldSource source(track);
    SearchCache cache;
    qint64 folded = searchValuesFolded();
    QVERIFY(!SearchQuery::parse(s(u"zzzz")).matches(source, { }, &cache)); // every search field is looked at
    const qint64 first = searchValuesFolded() - folded;
    QVERIFY2(first >= 5 && first <= defaultSearchFields().size(), qPrintable(QString::number(first)));
    folded = searchValuesFolded();
    QVERIFY(!SearchQuery::parse(s(u"qqqq")).matches(source, { }, &cache));
    QVERIFY(SearchQuery::parse(s(u"song")).matches(source, { }, &cache));
    QVERIFY(SearchQuery::parse(s(u"genre:rock")).matches(source, { }, &cache));
    QVERIFY(!SearchQuery::parse(s(u"nosuchfield:x")).matches(source, { }, &cache));
    QVERIFY(!SearchQuery::parse(s(u"nosuchfield:y")).matches(source, { }, &cache));
    QCOMPARE(searchValuesFolded(), folded); // all of it was in the cache
    QVERIFY(SearchQuery::parse(s(u"track#:1")).matches(source, { }, &cache)); // a field not asked for before
    QCOMPARE(searchValuesFolded(), folded + 1);
    QVERIFY(SearchQuery::parse(s(u"track#:1")).matches(source, { }, &cache));
    QCOMPARE(searchValuesFolded(), folded + 1);
    // Without a cache every match folds again, and is counted.
    folded = searchValuesFolded();
    QVERIFY(!SearchQuery::parse(s(u"zzzz")).matches(source));
    QCOMPARE(searchValuesFolded() - folded, first);
    // An emptied cache starts over.
    cache = SearchCache();
    folded = searchValuesFolded();
    QVERIFY(!SearchQuery::parse(s(u"zzzz")).matches(source, { }, &cache));
    QCOMPARE(searchValuesFolded() - folded, first);
}

void FilterFastTest::aKeystrokeNeitherSortsNorFolds()
{
    // T-MODEL, "Kept between changes": a change of the filter keeps the order
    // and folds only what was not folded before.
    const QList<LibraryTrack> tracks = crowd(60, 5);
    TrackListModel model;
    model.setSortFields({ s(u"Album Artist"), s(u"Album"), s(u"Track#") });
    model.setTracks(tracks);
    const QList<qint64> all = rowsOf(model);
    QCOMPARE(all.size(), 300);

    qint64 prepared = fieldValuesPrepared();
    const qint64 compared = fieldValueComparisons();
    qint64 folded = searchValuesFolded();
    model.setFilter(SearchQuery::parse(s(u"zzzz"))); // looks at every search field of every track
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(fieldValuesPrepared(), prepared); // no sort
    const qint64 first = searchValuesFolded() - folded;
    QVERIFY2(first >= 300 * 5 && first <= 300 * defaultSearchFields().size(), qPrintable(QString::number(first)));

    // The keystrokes after it: nothing is folded, nothing is prepared, and the order is the sort's.
    folded = searchValuesFolded();
    for (const char16_t *text : { u"s", u"so", u"son", u"song", u"song 1", u"song 1 of", u"artist 3", u"genre:rock" }) {
        model.setFilter(SearchQuery::parse(s(text)));
        QVERIFY2(model.rowCount() > 0 && model.rowCount() <= 300, qPrintable(s(text)));
        const QList<qint64> shown = rowsOf(model);
        qsizetype at = 0;
        for (const qint64 id : shown) { // a subsequence of the unfiltered order
            at = all.indexOf(id, at);
            QVERIFY2(at >= 0, qPrintable(s(text)));
            ++at;
        }
    }
    QCOMPARE(searchValuesFolded(), folded);
    model.setFilter(SearchQuery());
    QCOMPARE(rowsOf(model), all);
    QCOMPARE(searchValuesFolded(), folded);
    QCOMPARE(fieldValuesPrepared(), prepared);
    QCOMPARE(fieldValueComparisons(), compared);

    // A sort does not fold; it keeps the filter.
    model.setFilter(SearchQuery::parse(s(u"song 1")));
    const int rows = model.rowCount();
    folded = searchValuesFolded();
    const int title = int(model.columns().indexOf(s(u"Title")));
    QVERIFY(title >= 0);
    model.sort(title, Qt::DescendingOrder);
    QCOMPARE(model.rowCount(), rows);
    QCOMPARE(searchValuesFolded(), folded);
}

void FilterFastTest::aChangedTrackIsFoldedAgain()
{
    // T-MODEL, "Kept between changes": what is kept for a track goes when the track is replaced.
    QList<LibraryTrack> tracks = crowd(60, 5);
    TrackListModel model;
    model.setTracks(tracks);
    model.setFilter(SearchQuery::parse(s(u"zzzz")));
    QCOMPARE(model.rowCount(), 0);

    LibraryTrack changed = tracks.at(7);
    changed.tags.fields.insert(s(u"Title"), { s(u"Zzzz now") });
    qint64 folded = searchValuesFolded();
    model.upsertTracks({ changed });
    QCOMPARE(rowsOf(model), QList<qint64>({ changed.id })); // the filter sees the new title
    qint64 made = searchValuesFolded() - folded;
    QVERIFY2(made >= 1 && made <= defaultSearchFields().size(), qPrintable(QString::number(made))); // that track only

    LibraryTrack added = tracks.at(8);
    added.id = 9001;
    added.path = s(u"/music/new.flac");
    added.tags.fields.insert(s(u"Album"), { s(u"zzzz") });
    folded = searchValuesFolded();
    model.upsertTracks({ added });
    QCOMPARE(model.rowCount(), 2);
    made = searchValuesFolded() - folded;
    QVERIFY2(made >= 1 && made <= defaultSearchFields().size(), qPrintable(QString::number(made)));

    folded = searchValuesFolded();
    model.removeTracks({ changed.id });
    QCOMPARE(rowsOf(model), QList<qint64>({ added.id }));
    QCOMPARE(searchValuesFolded(), folded); // removing folds nothing

    // New tracks altogether: everything is folded afresh.
    folded = searchValuesFolded();
    model.setTracks(tracks);
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(searchValuesFolded() - folded >= 300 * 5);
}

QTEST_MAIN(FilterFastTest)
#include "tst_ui_filter_fast.moc"
