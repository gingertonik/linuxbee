// LOCKED ACCEPTANCE TEST (values prepared once for sorting). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/track-list.md (T-PREPARED). The
// counters are for the whole process, so every check reads them before and after.
#include <QtTest>

#include "library/albumgroups.h"
#include "library/artistindex.h"
#include "library/library.h"
#include "sort/fieldsort.h"
#include "tracklist/tracklistmodel.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

int sign(int value)
{
    return value < 0 ? -1 : (value > 0 ? 1 : 0);
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
            t.tags.fields.insert(s(u"Title"), { QStringLiteral("Song %1 of %2").arg(n).arg(a) });
            t.tags.audio.durationMs = 1000 * (60 + n);
            t.tags.format = QStringLiteral("flac");
            out.append(t);
        }
    }
    return out;
}

}

class SortPreparedTest : public QObject {
    Q_OBJECT
private slots:
    void sameOrderAsComparing();
    void counters();
    void theAlbumOrderPrepares();
    void theBrowserPrepares();
    void theTrackListPrepares();
};

void SortPreparedTest::sameOrderAsComparing()
{
    // T-PREPARED: comparePrepared of two prepared values has the sign of compareFieldValues.
    const QStringList values = { QString(), s(u" "), s(u"a"), s(u"A"), s(u"b"), s(u"á"), s(u"The Beatles"), s(u"the  beatles"),
        s(u"Beatles"), s(u"The"), s(u"Theory"), s(u"A Tribe"), s(u"Tribe"), s(u"Track 2"), s(u"Track 10"), s(u"Track 02"),
        s(u"2"), s(u"10"), s(u"02"), s(u"3/12"), s(u"x3"), s(u"-1"), s(u"1.5"), s(u"3:05"), s(u"12:00"), s(u"1:02:03"),
        s(u"59"), s(u"7 MB"), s(u"1.2 GB"), s(u"900 KB"), s(u"12 MB"), s(u"1998"), s(u"1998-04-20"), s(u"Zoë"), s(u"zoe"),
        s(u"  padded  "), s(u"padded"), s(u"Ünder"), s(u"under 9"), s(u"under 10") };
    const QStringList fields = { s(u"Title"), s(u"Album Artist"), s(u"Track#"), s(u"Disc#"), s(u"Time"), s(u"Size"), s(u"Year"),
        s(u"Play Count"), s(u"TRACK#"), s(u"time") };
    SortOptions noWords;
    noWords.ignoreWords = { };
    SortOptions twoWords;
    twoWords.ignoreWords = { s(u"The"), s(u"A") };
    const QList<SortOptions> every = { SortOptions { }, noWords, twoWords };
    int checked = 0;
    for (const SortOptions &options : every) {
        for (const QString &field : fields) {
            QList<PreparedValue> prepared;
            for (const QString &value : values)
                prepared.append(prepareFieldValue(field, value, options));
            for (int i = 0; i < values.size(); ++i) {
                QCOMPARE(prepared.at(i).blank, values.at(i).trimmed().isEmpty());
                for (int j = 0; j < values.size(); ++j) {
                    const int want = sign(compareFieldValues(field, values.at(i), values.at(j), options));
                    const int got = sign(comparePrepared(prepared.at(i), prepared.at(j)));
                    if (want != got)
                        QFAIL(qPrintable(QStringLiteral("%1: [%2] and [%3]: want %4, got %5")
                                             .arg(field, values.at(i), values.at(j))
                                             .arg(want)
                                             .arg(got)));
                    ++checked;
                }
            }
        }
    }
    QCOMPARE(checked, 3 * 10 * 40 * 40);
}

void SortPreparedTest::counters()
{
    // T-PREPARED: two counters, for tests and for a person timing the program.
    const qint64 prepared = fieldValuesPrepared();
    const qint64 compared = fieldValueComparisons();
    const PreparedValue a = prepareFieldValue(s(u"Title"), s(u"a"));
    const PreparedValue b = prepareFieldValue(s(u"Title"), s(u"b"));
    QCOMPARE(fieldValuesPrepared(), prepared + 2);
    QCOMPARE(fieldValueComparisons(), compared);
    QVERIFY(comparePrepared(a, b) < 0);
    QVERIFY(comparePrepared(b, a) > 0);
    QCOMPARE(comparePrepared(a, a), 0);
    QCOMPARE(fieldValuesPrepared(), prepared + 2);
    QCOMPARE(fieldValueComparisons(), compared); // comparing prepared values counts as neither
    QVERIFY(compareFieldValues(s(u"Title"), s(u"a"), s(u"b")) < 0);
    QVERIFY(compareFieldValues(s(u"Track#"), s(u"10"), s(u"9")) > 0);
    QCOMPARE(fieldValueComparisons(), compared + 2);
    QCOMPARE(fieldValuesPrepared(), prepared + 2); // an unprepared comparison is not counted as preparing
}

void SortPreparedTest::theAlbumOrderPrepares()
{
    // AV-ORDER: each key of each track prepared once; no unprepared comparison.
    const QList<LibraryTrack> tracks = crowd(60, 5);
    QCOMPARE(tracks.size(), 300);
    const qint64 prepared = fieldValuesPrepared();
    const qint64 compared = fieldValueComparisons();
    const QList<LibraryTrack> sorted = sortedForAlbums(tracks);
    QCOMPARE(sorted.size(), 300);
    QCOMPARE(fieldValueComparisons(), compared);
    const qint64 made = fieldValuesPrepared() - prepared;
    QVERIFY2(made > 0 && made <= 300 * albumSortFields().size(), qPrintable(QString::number(made)));
    // and the order is still that of T-COMPARE, key by key
    const QStringList fields = albumSortFields();
    QVERIFY(fields.size() >= 3);
    for (int i = 1; i < sorted.size(); ++i) {
        const QString before = sorted.at(i - 1).tags.fields.value(s(u"Album Artist")).value(0);
        const QString after = sorted.at(i).tags.fields.value(s(u"Album Artist")).value(0);
        QVERIFY2(compareFieldValues(s(u"Album Artist"), before, after) <= 0, qPrintable(before + s(u" / ") + after));
    }
}

void SortPreparedTest::theBrowserPrepares()
{
    // AB-ARTIST: the artists in order, each name prepared once.
    const QList<LibraryTrack> tracks = crowd(60, 5);
    const qint64 prepared = fieldValuesPrepared();
    const qint64 compared = fieldValueComparisons();
    const QList<ArtistEntry> artists = artistIndex(tracks);
    QVERIFY(artists.size() >= 17);
    QCOMPARE(fieldValueComparisons(), compared);
    const qint64 made = fieldValuesPrepared() - prepared;
    QVERIFY2(made > 0 && made <= artists.size(), qPrintable(QString::number(made)));
    for (int i = 1; i < artists.size(); ++i)
        QVERIFY(compareFieldValues(s(u"Album Artist"), artists.at(i - 1).name, artists.at(i).name) <= 0);
}

void SortPreparedTest::theTrackListPrepares()
{
    // T-SORT: the model prepares each key of each track once for a sort.
    const QList<LibraryTrack> tracks = crowd(60, 5);
    TrackListModel model;
    model.setSortFields({ s(u"Album Artist"), s(u"Album"), s(u"Track#") });
    qint64 prepared = fieldValuesPrepared();
    qint64 compared = fieldValueComparisons();
    model.setTracks(tracks);
    QCOMPARE(model.rowCount(), 300);
    QCOMPARE(fieldValueComparisons(), compared);
    qint64 made = fieldValuesPrepared() - prepared;
    QVERIFY2(made > 0 && made <= 300 * 3, qPrintable(QString::number(made)));

    const int title = int(model.columns().indexOf(s(u"Title")));
    QVERIFY(title >= 0);
    prepared = fieldValuesPrepared();
    compared = fieldValueComparisons();
    model.sort(title, Qt::DescendingOrder);
    QCOMPARE(fieldValueComparisons(), compared);
    made = fieldValuesPrepared() - prepared;
    QVERIFY2(made > 0 && made <= 300 * model.sortFields().size(), qPrintable(QString::number(made)));
    // the rows are in the order T-COMPARE gives, downwards
    const QHash<qint64, QString> titleOf = [&tracks] {
        QHash<qint64, QString> out;
        for (const LibraryTrack &t : tracks)
            out.insert(t.id, t.tags.fields.value(QStringLiteral("Title")).value(0));
        return out;
    }();
    for (int row = 1; row < model.rowCount(); ++row)
        QVERIFY(compareFieldValues(s(u"Title"), titleOf.value(model.trackId(row - 1)), titleOf.value(model.trackId(row))) >= 0);
}

QTEST_MAIN(SortPreparedTest)
#include "tst_ui_sort_prepared.moc"
