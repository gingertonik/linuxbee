// LOCKED ACCEPTANCE TEST (what the import command stands on: Love in the export, a drive that maps
// to the root, and the merge of what only a library knows, as one change). Written by the reviewer,
// not the builder. The builder makes these pass by changing src/, never by editing this file. A
// test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/import.md (I-PARSE, I-MAP, I-PLAN) and
// docs/spec/library.md (L-STATS mergeStatistics). The export text here is made up, in the shape
// MusicBee writes.
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "import/itunesxml.h"
#include "library/library.h"

using namespace linuxbee;
using namespace std::chrono_literals;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QDateTime at(int day, int hour = 12)
{
    return QDateTime(QDate(2026, 11, day), QTime(hour, 0), QTimeZone::UTC);
}

QByteArray exportOf(const QByteArray &tracks)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
           "\t<key>Application "
           "Version</key><string>3.7.0.0</string>\n\t<key>Tracks</key>\n\t<dict>\n"
        + tracks + "\t</dict>\n</dict>\n</plist>\n";
}

QByteArray track(int id, const QByteArray &location, const QByteArray &more)
{
    return "\t\t<key>" + QByteArray::number(id)
        + "</key>\n\t\t<dict>\n\t\t\t<key>Track ID</key><integer>" + QByteArray::number(id)
        + "</integer>\n" + more + "\t\t\t<key>Location</key><string>" + location
        + "</string>\n\t\t</dict>\n";
}

struct Lib {
    QTemporaryDir dir;
    std::unique_ptr<Library> library;
    QList<qint64> ids;
    Lib()
        : library(Library::open(dir.path() + QStringLiteral("/library.db")))
    {
        const char *names[] = { "basic.mp3", "basic.flac", "basic.m4a", "basic.ogg" };
        for (const char *name : names) {
            const QString to = dir.path() + QLatin1Char('/') + QLatin1String(name);
            if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/")
                        + QLatin1String(name),
                    to))
                QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
            ids.append(library->addFile(to, QStringLiteral("Music"), at(10)));
        }
        library->setWriteWait(50ms);
    }
    LibraryTrack of(int i) const { return *library->track(ids.at(i)); }
    bool sql(const QString &text) const
    {
        const QString name = QStringLiteral("sql-%1").arg(quintptr(this));
        bool ok = false;
        {
            QSqlDatabase other = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            other.setDatabaseName(dir.path() + QStringLiteral("/library.db"));
            ok = other.open() && QSqlQuery(other).exec(text);
            other.close();
        }
        QSqlDatabase::removeDatabase(name);
        return ok;
    }
};

struct Holder {
    QString name;
    explicit Holder(const Lib &f)
        : name(QStringLiteral("holder-%1").arg(quintptr(this)))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(f.dir.path() + QStringLiteral("/library.db"));
        if (!db.open() || !QSqlQuery(db).exec(QStringLiteral("BEGIN IMMEDIATE")))
            QTest::qFail("could not hold the database", __FILE__, __LINE__);
    }
    ~Holder()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(name);
            QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
};

}

class ImportMergeTest : public QObject {
    Q_OBJECT
private slots:
    void loveAndRatingAsMusicBeeWritesThem();
    void aValueThatIsNotWhatItsElementSays();
    void aDriveThatIsTheRoot();
    void thePlanCarriesLove();
    void aPlanOfTwentyThousand();
    void mergeTakesTheLargerAndTheLater();
    void mergeIsOneChange();
    void mergeIsAllOrNone();
    void mergeTwiceIsOnce();
};

void ImportMergeTest::loveAndRatingAsMusicBeeWritesThem()
{
    // I-PARSE: `Love` is a string, kept as written but for the space round it; a rating of 0 is no
    // rating; tens that are odd are half stars.
    const ImportedLibrary lib
        = parseItunesLibrary(exportOf(track(1, "file://localhost/Z:/mnt/music/a.mp3",
                                          "\t\t\t<key>Love</key><string>L</"
                                          "string>\n\t\t\t<key>Rating</key><integer>0</integer>\n")
            + track(2, "file://localhost/Z:/mnt/music/b.mp3",
                "\t\t\t<key>Love</key><string> B "
                "</string>\n\t\t\t<key>Rating</key><integer>70</integer>\n")
            + track(3, "file://localhost/Z:/mnt/music/c.mp3",
                "\t\t\t<key>Rating</key><integer>100</integer>\n")
            + track(4, "file://localhost/Z:/mnt/music/d.mp3",
                "\t\t\t<key>Love</key><integer>1</integer>\n")));
    QVERIFY2(lib.ok, qPrintable(lib.error));
    QCOMPARE(lib.tracks.size(), 4);
    QCOMPARE(lib.tracks.at(0).love, s(u"L"));
    QVERIFY(!lib.tracks.at(0).rating.has_value());
    QCOMPARE(lib.tracks.at(1).love, s(u"B"));
    QCOMPARE(lib.tracks.at(1).rating.value_or(0), 3.5);
    QCOMPARE(lib.tracks.at(2).love, QString());
    QCOMPARE(lib.tracks.at(2).rating.value_or(0), 5.0);
    QCOMPARE(lib.tracks.at(3).love, QString()); // not a string: not a Love this program knows
    QCOMPARE(lib.tracks.at(0).location, s(u"Z:/mnt/music/a.mp3"));
}

void ImportMergeTest::aValueThatIsNotWhatItsElementSays()
{
    // I-PARSE, found on a real export: MusicBee writes a tempo with a fraction as an <integer>
    // (<integer>94.034</integer>, 14 times in 16,797 tracks), and the whole file was refused for
    // it. A value that is not what its element says is no value of that kind; the file is read.
    const ImportedLibrary lib = parseItunesLibrary(
        exportOf(track(1, "file://localhost/Z:/m/a.mp3",
                     "\t\t\t<key>BPM</key><integer>94.034</integer>\n\t\t\t<key>Play "
                     "Count</key><integer>7</integer>\n")
            + track(2, "file://localhost/Z:/m/b.mp3",
                "\t\t\t<key>Play Count</key><integer>3.7</integer>\n\t\t\t<key>Date "
                "Added</key><date>some day</date>\n"
                "\t\t\t<key>Volume "
                "Adjustment</key><real>loud</real>\n\t\t\t<key>Rating</key><integer></integer>\n")
            + track(3, "file://localhost/Z:/m/c.mp3",
                "\t\t\t<key>Play Count</key><integer> 4 </integer>\n")));
    QVERIFY2(lib.ok, qPrintable(lib.error));
    QCOMPARE(lib.tracks.size(), 3);
    QCOMPARE(lib.tracks.at(0).playCount, 7);
    QCOMPARE(lib.tracks.at(1).playCount, 0); // not a count
    QVERIFY(!lib.tracks.at(1).dateAdded.isValid());
    QVERIFY(!lib.tracks.at(1).rating.has_value());
    QCOMPARE(lib.tracks.at(2).playCount, 4);
    // A track's own number that is no number still leaves the track out, and only that track.
    const ImportedLibrary odd = parseItunesLibrary(exportOf(
        "\t\t<key>9</key>\n\t\t<dict>\n\t\t\t<key>Track ID</key><integer>nine</integer>\n"
        "\t\t\t<key>Location</key><string>file://localhost/Z:/m/x.mp3</string>\n\t\t</dict>\n"
        + track(3, "file://localhost/Z:/m/c.mp3", "")));
    QVERIFY(odd.ok);
    QCOMPARE(odd.tracks.size(), 1);
}

void ImportMergeTest::aDriveThatIsTheRoot()
{
    // I-MAP: Wine shows the whole machine as one drive. The drive alone, with or without its slash,
    // maps to the root, and to a folder.
    for (const QString &drive : { s(u"Z:"), s(u"Z:/"), s(u"z:") }) {
        QCOMPARE(mapImportedPath(s(u"Z:/mnt/music/a b.mp3"), { { drive, s(u"/") } }),
            s(u"/mnt/music/a b.mp3"));
        QCOMPARE(mapImportedPath(s(u"Z:/mnt/music/a b.mp3"), { { drive, s(u"/data") } }),
            s(u"/data/mnt/music/a b.mp3"));
    }
    QCOMPARE(mapImportedPath(s(u"Z:/a.mp3"), { { s(u"Z:"), s(u"/") } }), s(u"/a.mp3"));
    QCOMPARE(mapImportedPath(s(u"Y:/mnt/a.mp3"), { { s(u"Z:"), s(u"/") } }),
        s(u"Y:/mnt/a.mp3")); // another drive: untouched
}

void ImportMergeTest::thePlanCarriesLove()
{
    Lib f;
    const QByteArray where
        = "file://localhost/Z:" + QByteArray(f.of(0).path.toUtf8()).replace(' ', "%20");
    const ImportedLibrary lib = parseItunesLibrary(exportOf(track(1, where,
        "\t\t\t<key>Love</key><string>L</string>\n\t\t\t<key>Rating</key><integer>90</"
        "integer>\n")));
    QVERIFY(lib.ok);
    const ImportPlan plan = planImport(lib, f.library->tracks(), { { s(u"Z:"), s(u"/") } });
    QCOMPARE(plan.matches.size(), 1);
    QCOMPARE(plan.matches.at(0).libraryId, f.ids.at(0));
    QCOMPARE(plan.matches.at(0).love, s(u"L"));
    QCOMPARE(plan.matches.at(0).rating.value_or(0), 4.5);
}

void ImportMergeTest::aPlanOfTwentyThousand()
{
    // I-PLAN at the size of a real library. (Found when the command was designed: every imported
    // track was looked for by walking the whole library, twice: four hundred million comparisons
    // for twenty thousand tracks.) What is checked is that the plan is right at that size; the time
    // is printed for whoever reads the output, and is not a condition.
    QList<LibraryTrack> library;
    ImportedLibrary imported;
    imported.ok = true;
    for (int i = 0; i < 20000; ++i) {
        LibraryTrack t;
        t.id = i + 1;
        t.path = QStringLiteral("/music/Artist %1/Album/%2 Title.mp3").arg(i / 12).arg(i);
        t.dateAdded = at(10);
        library.append(t);
        ImportedTrack in;
        in.trackId = i + 1;
        // Every third as Windows wrote its case, every seventh not in the library.
        const QString path = i % 7 == 0 ? QStringLiteral("/gone/%1.mp3").arg(i)
                                        : (i % 3 == 0 ? t.path.toUpper() : t.path);
        in.location = QStringLiteral("Z:") + path;
        in.dateAdded = at(1);
        in.playCount = i % 5;
        imported.tracks.append(in);
    }
    QElapsedTimer timer;
    timer.start();
    const ImportPlan plan = planImport(imported, library, { { s(u"Z:"), s(u"/") } });
    qInfo("a plan for 20000 tracks: %lld ms", static_cast<long long>(timer.elapsed()));
    QCOMPARE(plan.unmatched.size(), 2858); // every seventh
    QCOMPARE(plan.matches.size(), 20000 - 2858);
    QCOMPARE(plan.matches.at(0).libraryId,
        2); // track 0 is not in the library; track 1 is the first match
    QCOMPARE(plan.matches.at(1).libraryId, 3);
    QCOMPARE(plan.matches.at(2).libraryId, 4); // the fourth track, found though its case differs
    QVERIFY(plan.matches.at(2).changes);
    // Two library tracks that differ only by case: a path that fits neither exactly fits neither at
    // all.
    LibraryTrack twin = library.at(1);
    twin.id = 99999;
    twin.path = library.at(1).path.toLower();
    library.append(twin);
    ImportedLibrary one;
    one.ok = true;
    ImportedTrack in;
    in.trackId = 1;
    in.location = QStringLiteral("Z:") + library.at(1).path.toUpper();
    one.tracks.append(in);
    QCOMPARE(planImport(one, library, { { s(u"Z:"), s(u"/") } }).unmatched.size(), 1);
    one.tracks[0].location = QStringLiteral("Z:") + twin.path;
    QCOMPARE(planImport(one, library, { { s(u"Z:"), s(u"/") } }).matches.value(0).libraryId, 99999);
}

void ImportMergeTest::mergeTakesTheLargerAndTheLater()
{
    // L-STATS mergeStatistics: the date added given; the larger of each count; the later play.
    Lib f;
    QVERIFY(f.library->setStatistics(f.ids.at(0), { at(10), 7, 1, at(12) }));
    QVERIFY(f.library->setStatistics(f.ids.at(1), { at(10), 0, 0, QDateTime() }));
    QSignalSpy updated(f.library.get(), &Library::tracksUpdated);
    QSignalSpy failed(f.library.get(), &Library::failed);
    const int changed = f.library->mergeStatistics({
        { f.ids.at(0), { at(1), 3, 4, at(11) } }, // fewer plays, more skips, an earlier play
        { f.ids.at(1), { at(2), 9, 0, at(13) } },
        { f.ids.at(2),
            { QDateTime(), 0, 0, QDateTime() } }, // nothing to bring: the date added stays
        { 424242, { at(3), 5, 5, at(14) } }, // no such track: passed over
        { f.ids.at(3),
            { QDateTime(), 2, 0, QDateTime() } }, // plays, and no date added: the one held stays
    });
    QCOMPARE(changed, 3);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(updated.size(), 1);
    QList<qint64> ids = updated.at(0).at(0).value<QList<qint64>>();
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids, (QList<qint64> { f.ids.at(0), f.ids.at(1), f.ids.at(3) }));
    LibraryTrack t = f.of(0);
    QCOMPARE(t.dateAdded, at(1));
    QCOMPARE(t.playCount, 7);
    QCOMPARE(t.skipCount, 4);
    QCOMPARE(t.lastPlayed, at(12));
    t = f.of(1);
    QCOMPARE(t.dateAdded, at(2));
    QCOMPARE(t.playCount, 9);
    QCOMPARE(t.lastPlayed, at(13));
    t = f.of(2);
    QCOMPARE(t.dateAdded, at(10));
    QCOMPARE(t.playCount, 0);
    QVERIFY(!t.lastPlayed.isValid());
    t = f.of(3);
    QCOMPARE(t.dateAdded, at(10));
    QCOMPARE(t.playCount, 2);
    // A negative count is no count.
    QCOMPARE(
        f.library->mergeStatistics({ { f.ids.at(2), { QDateTime(), -5, -1, QDateTime() } } }), 0);
    QCOMPARE(f.of(2).playCount, 0);
}

void ImportMergeTest::mergeIsOneChange()
{
    // The merge is made by the database inside the one change, from what the library holds then: a
    // play counted after the caller looked and before the merge is not lost.
    Lib f;
    QVERIFY(f.library->setStatistics(f.ids.at(0), { at(10), 5, 0, at(11) }));
    const TrackStatistics planned { at(1), 5, 0, at(11) }; // planned when the library said 5
    QVERIFY(f.library->recordPlay(f.ids.at(0), at(14))); // and then a track ended
    QCOMPARE(f.library->mergeStatistics({ { f.ids.at(0), planned } }), 1);
    QCOMPARE(f.of(0).playCount, 6);
    QCOMPARE(f.of(0).lastPlayed, at(14));
    QCOMPARE(f.of(0).dateAdded, at(1));
    // Nothing given: nothing asked of the library, held or not.
    QSignalSpy failed(f.library.get(), &Library::failed);
    Holder holder(f);
    QCOMPARE(f.library->mergeStatistics({ }), 0);
    QCOMPARE(failed.size(), 0);
}

void ImportMergeTest::mergeIsAllOrNone()
{
    Lib f;
    QSignalSpy updated(f.library.get(), &Library::tracksUpdated);
    QSignalSpy failed(f.library.get(), &Library::failed);
    const QList<StatisticsMerge> four { { f.ids.at(0), { at(1), 1, 0, at(11) } },
        { f.ids.at(1), { at(1), 2, 0, at(11) } }, { f.ids.at(2), { at(1), 3, 0, at(11) } },
        { f.ids.at(3), { at(1), 4, 0, at(11) } } };
    {
        // Held by another program: refused whole, said once.
        Holder holder(f);
        QCOMPARE(f.library->mergeStatistics(four), -1);
        QCOMPARE(failed.size(), 1);
    }
    // Refused at the third of four (as when the disk fills): the first two are not left changed.
    QVERIFY(f.sql(QStringLiteral(
        "CREATE TRIGGER full BEFORE UPDATE ON tracks WHEN NEW.path LIKE '%/basic.m4a' "
        "BEGIN SELECT RAISE(ABORT, 'database or disk is full'); END")));
    QCOMPARE(f.library->mergeStatistics(four), -1);
    QCOMPARE(failed.size(), 2);
    QVERIFY(failed.at(1).at(0).toString().contains(s(u"full")));
    QCOMPARE(updated.size(), 0);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(f.of(i).playCount, 0);
        QCOMPARE(f.of(i).dateAdded, at(10));
    }
    QVERIFY(f.sql(QStringLiteral("DROP TRIGGER full")));
    QCOMPARE(f.library->mergeStatistics(four), 4);
    QCOMPARE(updated.size(), 1);
    QCOMPARE(f.of(3).playCount, 4);
}

void ImportMergeTest::mergeTwiceIsOnce()
{
    Lib f;
    const QList<StatisticsMerge> two { { f.ids.at(0), { at(1), 1, 2, at(11) } },
        { f.ids.at(1), { at(2), 0, 0, QDateTime() } } };
    QCOMPARE(f.library->mergeStatistics(two), 2);
    QSignalSpy updated(f.library.get(), &Library::tracksUpdated);
    QCOMPARE(f.library->mergeStatistics(two), 0); // the library says all of it already
    QCOMPARE(updated.size(), 0); // and nobody is told of a change that was none
    QCOMPARE(f.of(0).skipCount, 2);
}

QTEST_MAIN(ImportMergeTest)
#include "tst_import_merge.moc"
