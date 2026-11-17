// LOCKED ACCEPTANCE TEST (a scan whose storing is refused partway stores nothing). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-PARTS, L-FAILED). The refusals are real: triggers planted
// through a second connection, one kind that fails the statement and one that ends the whole transaction, which
// is what a full disk does. Copies of fixtures in a temporary folder.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "library/library.h"

using namespace linuxbee;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

// Three tracks a, b, c. Then: a was missing and is back; fourteen files are new; b's file has changed; c's file
// is gone. One scan has all four kinds of thing to store, in that order.
struct Box {
    QTemporaryDir dir;
    QString database, music;
    std::unique_ptr<Library> library;
    qint64 a = 0, b = 0, c = 0;

    Box()
        : database(dir.path() + QStringLiteral("/lib/library.db"))
        , music(dir.path() + QStringLiteral("/music"))
    {
        QDir(dir.path()).mkpath(QStringLiteral("lib"));
        QDir(dir.path()).mkpath(QStringLiteral("music"));
        put("basic.flac", "a.flac");
        put("basic.mp3", "b.mp3");
        put("basic.ogg", "c.ogg");
        library = Library::open(database);
        if (!library || scan().added.size() != 3) {
            QTest::qFail("the first scan did not add the three files", __FILE__, __LINE__);
            return;
        }
        a = library->trackByPath(music + QStringLiteral("/a.flac"))->id;
        b = library->trackByPath(music + QStringLiteral("/b.mp3"))->id;
        c = library->trackByPath(music + QStringLiteral("/c.ogg"))->id;
        // a goes missing, and comes back.
        if (!QFile::remove(music + QStringLiteral("/a.flac")) || scan().nowMissing != QList<qint64> { a })
            QTest::qFail("a did not go missing", __FILE__, __LINE__);
        put("basic.flac", "a.flac");
        for (int i = 0; i < 14; ++i)
            put("basic.mp3", qPrintable(QStringLiteral("n%1.mp3").arg(i, 2, 10, QLatin1Char('0'))));
        QFile changed(music + QStringLiteral("/b.mp3"));
        if (!changed.open(QIODevice::ReadWrite) || !changed.setFileTime(kNow.addDays(-30), QFileDevice::FileModificationTime))
            QTest::qFail("could not change b's time", __FILE__, __LINE__);
        changed.close();
        if (!QFile::remove(music + QStringLiteral("/c.ogg")))
            QTest::qFail("could not remove c", __FILE__, __LINE__);
    }
    void put(const char *from, const char *to)
    {
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from), music + QLatin1Char('/') + QLatin1String(to)))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    }
    ScanReport scan()
    {
        ScanOptions options;
        options.category = QStringLiteral("Music");
        options.now = kNow;
        return library->scan({ music }, options);
    }
    // Through a connection of its own: a statement, or every row of the table as text.
    QStringList other(const QString &statement)
    {
        QStringList rows;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("other"));
            db.setDatabaseName(database);
            QSqlQuery query(db);
            if (!db.open() || !query.exec(statement))
                QTest::qFail(qPrintable(query.lastError().text()), __FILE__, __LINE__);
            while (query.next()) {
                QStringList row;
                for (int i = 0; i < query.record().count(); ++i)
                    row.append(query.value(i).toString());
                rows.append(row.join(QLatin1Char('|')));
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("other"));
        return rows;
    }
    QStringList rows() { return other(QStringLiteral("SELECT * FROM tracks ORDER BY id")); }
};

}

class LibraryScanRefusedTest : public QObject {
    Q_OBJECT
private slots:
    void refusedPartway_data();
    void refusedPartway();
    void allWell();
};

void LibraryScanRefusedTest::refusedPartway_data()
{
    QTest::addColumn<QString>("trigger");
    // A_ID is the track that was missing and is back (the first thing stored); B_ID the one whose file changed;
    // C_ID the one whose file is gone (the last). n07.mp3 is the eighth of the fourteen new files.
    QTest::newRow("a new file is refused") << QStringLiteral("BEFORE INSERT ON tracks WHEN new.path LIKE '%/n07.mp3' BEGIN SELECT RAISE(ABORT, 'refused by the test'); END");
    QTest::newRow("a new file ends the whole transaction") << QStringLiteral("BEFORE INSERT ON tracks WHEN new.path LIKE '%/n07.mp3' BEGIN SELECT RAISE(ROLLBACK, 'refused by the test'); END");
    QTest::newRow("the first thing stored is refused") << QStringLiteral("BEFORE UPDATE ON tracks WHEN old.id = A_ID BEGIN SELECT RAISE(ABORT, 'refused by the test'); END");
    QTest::newRow("the first thing stored ends the whole transaction") << QStringLiteral("BEFORE UPDATE ON tracks WHEN old.id = A_ID BEGIN SELECT RAISE(ROLLBACK, 'refused by the test'); END");
    QTest::newRow("a changed file is refused") << QStringLiteral("BEFORE UPDATE ON tracks WHEN old.id = B_ID BEGIN SELECT RAISE(ABORT, 'refused by the test'); END");
    QTest::newRow("a changed file ends the whole transaction") << QStringLiteral("BEFORE UPDATE ON tracks WHEN old.id = B_ID BEGIN SELECT RAISE(ROLLBACK, 'refused by the test'); END");
    QTest::newRow("the last thing stored is refused") << QStringLiteral("BEFORE UPDATE ON tracks WHEN old.id = C_ID BEGIN SELECT RAISE(ABORT, 'refused by the test'); END");
    QTest::newRow("the last thing stored ends the whole transaction") << QStringLiteral("BEFORE UPDATE ON tracks WHEN old.id = C_ID BEGIN SELECT RAISE(ROLLBACK, 'refused by the test'); END");
}

void LibraryScanRefusedTest::refusedPartway()
{
    // L-PARTS, L-FAILED: the storing of a scan is one change. A write the database refuses ends it: nothing of
    // the scan is stored, before the refusal or after it; the report names no track and says why; `failed`
    // goes out once; no change is announced. The same scan again, with the refusal gone, stores everything.
    QFETCH(QString, trigger);
    Box box;
    QVERIFY(box.a > 0 && box.b > 0 && box.c > 0);
    trigger.replace(QStringLiteral("A_ID"), QString::number(box.a));
    trigger.replace(QStringLiteral("B_ID"), QString::number(box.b));
    trigger.replace(QStringLiteral("C_ID"), QString::number(box.c));
    const QStringList before = box.rows();
    QCOMPARE(before.size(), 3);
    box.other(QStringLiteral("CREATE TRIGGER refuse_one ") + trigger);
    QSignalSpy failed(box.library.get(), &Library::failed);
    QSignalSpy added(box.library.get(), &Library::tracksAdded);
    QSignalSpy updated(box.library.get(), &Library::tracksUpdated);

    const ScanReport report = box.scan();
    QVERIFY(!report.refused.isEmpty());
    QVERIFY2(report.added.isEmpty() && report.updated.isEmpty() && report.found.isEmpty() && report.nowMissing.isEmpty(),
        qPrintable(QStringLiteral("added %1, updated %2, found %3, missing %4").arg(report.added.size()).arg(report.updated.size()).arg(report.found.size()).arg(report.nowMissing.size())));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toString(), report.refused);
    QCOMPARE(added.count(), 0);
    QCOMPARE(updated.count(), 0);
    // Not one row differs, as another program sees the file; and the library says the same.
    QCOMPARE(box.rows(), before);
    QCOMPARE(box.library->count(), 3);
    QVERIFY(box.library->track(box.a)->missing);
    QVERIFY(!box.library->track(box.c)->missing);
    // Nothing is left held: another program can write.
    box.other(QStringLiteral("DROP TRIGGER refuse_one"));

    const ScanReport again = box.scan();
    QVERIFY2(again.refused.isEmpty(), qPrintable(again.refused));
    QCOMPARE(again.added.size(), 14);
    QCOMPARE(again.found, QList<qint64> { box.a });
    QCOMPARE(again.updated, QList<qint64> { box.b });
    QCOMPARE(again.nowMissing, QList<qint64> { box.c });
    QCOMPARE(box.library->count(), 17);
    QCOMPARE(box.rows().size(), 17);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(added.count(), 1);
}

void LibraryScanRefusedTest::allWell()
{
    // The same scan with nothing refused, for comparison.
    Box box;
    QSignalSpy failed(box.library.get(), &Library::failed);
    const ScanReport report = box.scan();
    QVERIFY(report.refused.isEmpty());
    QCOMPARE(report.added.size(), 14);
    QCOMPARE(report.found, QList<qint64> { box.a });
    QCOMPARE(report.updated, QList<qint64> { box.b });
    QCOMPARE(report.nowMissing, QList<qint64> { box.c });
    QCOMPARE(failed.count(), 0);
}

QTEST_MAIN(LibraryScanRefusedTest)
#include "tst_library_scan_refused.moc"
