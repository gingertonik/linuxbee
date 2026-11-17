// LOCKED ACCEPTANCE TEST (the library file shared with another program, or with a second start). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-OPEN, L-WAIT, L-PARTS, L-FAILED). The other program is a second
// connection of the test's own, in this thread or in one of its own. Copies of fixtures in a temporary folder.
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <thread>

#include "library/library.h"

using namespace linuxbee;
using namespace std::chrono_literals;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
std::atomic<int> nextName { 0 };

struct Box {
    QTemporaryDir dir;
    QString database, music;
    std::unique_ptr<Library> library;
    QList<qint64> ids;

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
        if (!library)
            QTest::qFail("the library did not open", __FILE__, __LINE__);
        else
            ids = scan().added;
        if (ids.size() != 3)
            QTest::qFail("the first scan did not add the three files", __FILE__, __LINE__);
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
};

// Another program with the file open, in this thread. `hold` is what it does and keeps doing.
struct Other {
    QString name;
    Other(const QString &database, const char *hold)
        : name(QStringLiteral("other-%1").arg(++nextName))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(database);
        if (!db.open())
            QTest::qFail("the other program could not open the file", __FILE__, __LINE__);
        for (const QString &statement : QString::fromLatin1(hold).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            QSqlQuery query(db);
            if (!query.exec(statement))
                QTest::qFail(qPrintable(query.lastError().text()), __FILE__, __LINE__);
            while (query.next()) { }
        }
    }
    QString ask(const char *statement) const
    {
        QSqlQuery query(QSqlDatabase::database(name));
        return query.exec(QString::fromLatin1(statement)) && query.next() ? query.value(0).toString() : QStringLiteral("<no answer>");
    }
    ~Other()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(name);
            QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
};

// Another program that has the file for writing and lets it go after a while, in a thread of its own.
struct WriterForAWhile {
    std::atomic<bool> holding { false };
    std::atomic<bool> failed { false };
    std::thread thread;
    WriterForAWhile(const QString &database, std::chrono::milliseconds howLong)
    {
        const QString name = QStringLiteral("writer-%1").arg(++nextName);
        thread = std::thread([this, database, howLong, name] {
            {
                QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
                db.setDatabaseName(database);
                if (!db.open() || !QSqlQuery(db).exec(QStringLiteral("BEGIN IMMEDIATE")))
                    failed = true;
                holding = true;
                std::this_thread::sleep_for(howLong);
                QSqlQuery(db).exec(QStringLiteral("COMMIT"));
                db.close();
            }
            QSqlDatabase::removeDatabase(name);
        });
        while (!holding)
            std::this_thread::sleep_for(1ms);
    }
    ~WriterForAWhile() { thread.join(); }
};

}

class LibrarySharedTest : public QObject {
    Q_OBJECT
private slots:
    void aWriteWaitsForAnotherWriter();
    void aWriteThatHasWaitedLongEnough();
    void aRefusedScanStoresNothingLater();
    void aReaderDoesNotStopAWrite();
    void theFileItself();
    void twoFirstStarts();
};

void LibrarySharedTest::aWriteWaitsForAnotherWriter()
{
    // L-WAIT: another program is writing. A change waits for it and then goes through; nothing is refused.
    Box box;
    QSignalSpy failed(box.library.get(), &Library::failed);
    QElapsedTimer timer;
    {
        WriterForAWhile writer(box.database, 400ms);
        QVERIFY(!writer.failed);
        timer.start();
        QCOMPARE(box.library->setCategory(box.ids, QStringLiteral("Podcasts")), 3);
        QVERIFY2(timer.elapsed() >= 250, qPrintable(QString::number(timer.elapsed())));
    }
    {
        // A scan stores what it found when the other is done.
        box.put("basic.m4a", "d.m4a");
        WriterForAWhile writer(box.database, 400ms);
        const ScanReport report = box.scan();
        QCOMPARE(report.added.size(), 1);
        QVERIFY(report.refused.isEmpty());
    }
    {
        WriterForAWhile writer(box.database, 400ms);
        QCOMPARE(box.library->remove({ box.ids.at(0) }), 1);
    }
    QCOMPARE(box.library->count(), 3);
    QCOMPARE(failed.count(), 0);
}

void LibrarySharedTest::aWriteThatHasWaitedLongEnough()
{
    // L-WAIT, L-FAILED: the wait has an end (5 seconds; here 100 ms). Then the change is refused whole, and said.
    Box box;
    box.library->setWriteWait(100ms);
    QSignalSpy failed(box.library.get(), &Library::failed);
    QSignalSpy added(box.library.get(), &Library::tracksAdded);
    QSignalSpy updated(box.library.get(), &Library::tracksUpdated);
    box.put("basic.m4a", "d.m4a");
    QVERIFY(QFile::remove(box.music + QStringLiteral("/c.ogg"))); // a scan would mark it missing
    {
        Other writer(box.database, "BEGIN IMMEDIATE");
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(box.library->remove(box.ids), 0);
        QCOMPARE(failed.count(), 1);
        // L-PARTS: a scan that cannot store stores nothing, says so in its report and through `failed`,
        // and announces no change.
        const ScanReport report = box.scan();
        QVERIFY2(timer.elapsed() < 3000, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(!report.refused.isEmpty());
        QVERIFY(report.added.isEmpty() && report.updated.isEmpty() && report.nowMissing.isEmpty() && report.found.isEmpty());
        QCOMPARE(failed.count(), 2);
        QCOMPARE(failed.at(1).at(0).toString(), report.refused);
        QCOMPARE(added.count(), 0);
        QCOMPARE(updated.count(), 0);
    }
    QCOMPARE(box.library->count(), 3);
    QVERIFY(!box.library->track(box.ids.at(2))->missing);
    // The same scan again, with the file free: everything it found is stored now.
    const ScanReport report = box.scan();
    QVERIFY(report.refused.isEmpty());
    QCOMPARE(report.added.size(), 1);
    QCOMPARE(report.nowMissing.size(), 1);
    QCOMPARE(failed.count(), 2);
}

void LibrarySharedTest::aRefusedScanStoresNothingLater()
{
    // L-PARTS, L-FAILED: a scan that could not begin does not go on. The other program lets go while the
    // scan would still be storing (it holds for a second; the wait is 100 ms; there are fourteen new files):
    // nothing of that scan is stored, then or later.
    Box box;
    box.library->setWriteWait(100ms);
    for (int i = 0; i < 14; ++i)
        box.put("basic.mp3", qPrintable(QStringLiteral("n%1.mp3").arg(i, 2, 10, QLatin1Char('0'))));
    QSignalSpy added(box.library.get(), &Library::tracksAdded);
    {
        WriterForAWhile writer(box.database, 1000ms);
        QVERIFY(!writer.failed);
        const ScanReport report = box.scan();
        QVERIFY(!report.refused.isEmpty());
        QVERIFY(report.added.isEmpty());
    }
    QCOMPARE(box.library->count(), 3);
    QCOMPARE(added.count(), 0);
    QCOMPARE(box.scan().added.size(), 14);
}

void LibrarySharedTest::aReaderDoesNotStopAWrite()
{
    // L-OPEN: another program that is only reading, however long it takes over it, does not hold a change up.
    Box box;
    box.library->setWriteWait(100ms);
    QSignalSpy failed(box.library.get(), &Library::failed);
    Other reader(box.database, "BEGIN;SELECT count(*) FROM sqlite_master");
    QCOMPARE(box.library->setCategory(box.ids, QStringLiteral("Podcasts")), 3);
    QCOMPARE(box.library->remove({ box.ids.at(0) }), 1);
    box.put("basic.m4a", "d.m4a");
    QCOMPARE(box.scan().added.size(), 2); // d, and the file whose track was removed just now
    QVERIFY(box.library->recordPlay(box.ids.at(1), kNow));
    QCOMPARE(failed.count(), 0);
    // And the reader goes on seeing the file as it was when it began.
    QCOMPARE(reader.ask("SELECT count(*) FROM sqlite_master").isEmpty(), false);
}

void LibrarySharedTest::theFileItself()
{
    // L-OPEN: the journal is a write-ahead log. Closed, the library is one file again, with everything in it.
    Box box;
    {
        Other other(box.database, "");
        QCOMPARE(other.ask("PRAGMA journal_mode"), QStringLiteral("wal"));
    }
    box.library.reset();
    QCOMPARE(QDir(box.dir.path() + QStringLiteral("/lib")).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList({ QStringLiteral("library.db") }));
    box.library = Library::open(box.database);
    QVERIFY(box.library != nullptr);
    QCOMPARE(box.library->count(), 3);
    // A library in memory is as before.
    auto memory = Library::open(QStringLiteral(":memory:"));
    QVERIFY(memory != nullptr);
    QCOMPARE(memory->count(), 0);
}

void LibrarySharedTest::twoFirstStarts()
{
    // L-OPEN: two programs started at the same moment on a profile with no library yet. Both have a library.
    QTemporaryDir dir;
    QStringList said;
    for (int round = 0; round < 40; ++round) {
        const QString database = dir.path() + QStringLiteral("/library-%1.db").arg(round);
        std::atomic<bool> go { false };
        std::atomic<int> opened { 0 };
        QString errors[2];
        std::thread starts[2];
        for (int i = 0; i < 2; ++i) {
            starts[i] = std::thread([&, i] {
                while (!go) { }
                auto library = Library::open(database, &errors[i]);
                if (library && library->count() == 0)
                    ++opened;
            });
        }
        go = true;
        for (std::thread &start : starts)
            start.join();
        if (opened != 2)
            said.append(QStringLiteral("round %1: %2 | %3").arg(round).arg(errors[0], errors[1]).left(150));
        // What they made is one good library.
        auto library = Library::open(database);
        QVERIFY(library != nullptr);
        QCOMPARE(library->count(), 0);
    }
    QVERIFY2(said.isEmpty(), qPrintable(QStringLiteral("%1 of the rounds; the first: %2").arg(said.size()).arg(said.value(0))));
}

QTEST_MAIN(LibrarySharedTest)
#include "tst_library_shared.moc"
