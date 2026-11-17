// LOCKED ACCEPTANCE TEST (the library: a call with very many ids). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-MANY, L-EDIT). 20,000 is above what the database takes
// in one expression (10,000 terms with the system's SQLite; 1,000 with a default build), and above the owner's
// library. The tracks are 20,000 names of one fixture file, in a temporary folder.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <unistd.h>

#include "library/library.h"

using namespace linuxbee;

namespace {

constexpr int kMany = 20000;
const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

struct Big {
    QTemporaryDir dir;
    std::unique_ptr<Library> library;
    QList<qint64> ids;

    explicit Big(const QString &database = QStringLiteral(":memory:"))
        : library(Library::open(database == QStringLiteral(":memory:") ? database : dir.path() + QLatin1Char('/') + database))
    {
        const QString first = dir.path() + QStringLiteral("/t00000.mp3");
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/basic.mp3"), first))
            QTest::qFail("could not copy the fixture", __FILE__, __LINE__);
        QStringList paths { first };
        for (int i = 1; i < kMany; ++i) {
            const QString path = dir.path() + QStringLiteral("/t%1.mp3").arg(i, 5, 10, QLatin1Char('0'));
            if (::link(QFile::encodeName(first).constData(), QFile::encodeName(path).constData()) != 0)
                QTest::qFail("could not make a second name", __FILE__, __LINE__);
            paths.append(path);
        }
        // One scan stores them in parts; adding one by one would be slow on disk.
        ScanOptions options;
        options.category = QStringLiteral("Music");
        options.now = kNow;
        const ScanReport report = library->scan({ dir.path() }, options);
        ids = report.added;
        if (ids.size() != kMany)
            QTest::qFail("the scan did not add them all", __FILE__, __LINE__);
    }
};

}

class LibraryManyTest : public QObject {
    Q_OBJECT
private slots:
    void sendToAndRemove();
    void partOfAVeryLongList();
    void allOrNone();
};

void LibraryManyTest::sendToAndRemove()
{
    // L-MANY: the number of ids in a call has no limit.
    Big big;
    QCOMPARE(big.library->count(), kMany);
    QSignalSpy updated(big.library.get(), &Library::tracksUpdated);
    QSignalSpy removed(big.library.get(), &Library::tracksRemoved);

    QCOMPARE(big.library->setCategory(big.ids, QStringLiteral("Audiobooks")), kMany);
    QCOMPARE(updated.count(), 1); // L-SIGNALS: one signal for the call
    QCOMPARE(updated.at(0).at(0).value<QList<qint64>>().size(), kMany);
    QCOMPARE(big.library->track(big.ids.first())->category, QStringLiteral("Audiobooks"));
    QCOMPARE(big.library->track(big.ids.at(kMany / 2))->category, QStringLiteral("Audiobooks"));
    QCOMPARE(big.library->track(big.ids.last())->category, QStringLiteral("Audiobooks"));
    QCOMPARE(big.library->setCategory(big.ids, QStringLiteral("Audiobooks")), 0); // already as asked

    QCOMPARE(big.library->remove(big.ids), kMany);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(0).value<QList<qint64>>().size(), kMany);
    QCOMPARE(big.library->count(), 0);
    QVERIFY(QFile::exists(big.dir.path() + QStringLiteral("/t19999.mp3"))); // L-EDIT: no file is touched
}

void LibraryManyTest::partOfAVeryLongList()
{
    // L-MANY with L-EDIT: unknown ids and ids given twice among very many still do not count.
    Big big;
    QList<qint64> asked;
    for (int i = 0; i < kMany; i += 2)
        asked.append(big.ids.at(i));
    for (int i = 0; i < 3000; ++i)
        asked.append(900000000 + i); // unknown
    asked.append(asked.mid(0, 500)); // some twice
    QCOMPARE(big.library->setCategory(asked, QStringLiteral("Podcasts")), kMany / 2);
    QCOMPARE(big.library->track(big.ids.at(1))->category, QStringLiteral("Music"));
    QSignalSpy removed(big.library.get(), &Library::tracksRemoved);
    QCOMPARE(big.library->remove(asked), kMany / 2);
    QList<qint64> told = removed.at(0).at(0).value<QList<qint64>>();
    std::sort(told.begin(), told.end());
    QCOMPARE(told.size(), kMany / 2);
    QVERIFY(std::adjacent_find(told.cbegin(), told.cend()) == told.cend()); // each id once
    QCOMPARE(big.library->count(), kMany / 2);
    QVERIFY(big.library->track(big.ids.at(1)).has_value());
    QVERIFY(!big.library->track(big.ids.at(0)).has_value());
}

void LibraryManyTest::allOrNone()
{
    // L-MANY, L-FAILED: a call the database refuses changes nothing, returns 0, and says `failed`.
    // Another holder has the database for writing, so the library's call cannot go through.
    Big big(QStringLiteral("library.db"));
    QSignalSpy removed(big.library.get(), &Library::tracksRemoved);
    QSignalSpy updated(big.library.get(), &Library::tracksUpdated);
    QSignalSpy failed(big.library.get(), &Library::failed);
    big.library->setWriteWait(std::chrono::milliseconds(50)); // L-WAIT: not the five seconds
    {
        QSqlDatabase holder = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("holder"));
        holder.setDatabaseName(big.dir.path() + QStringLiteral("/library.db"));
        QVERIFY(holder.open());
        QSqlQuery hold(holder);
        QVERIFY(hold.exec(QStringLiteral("BEGIN IMMEDIATE")));
        QCOMPARE(big.library->remove(big.ids), 0);
        QCOMPARE(big.library->setCategory(big.ids, QStringLiteral("Podcasts")), 0);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(updated.count(), 0);
        QCOMPARE(failed.count(), 2); // L-FAILED: once for each call
        QVERIFY(!failed.at(0).at(0).toString().isEmpty());
        QVERIFY(hold.exec(QStringLiteral("ROLLBACK")));
        holder.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("holder"));
    QCOMPARE(big.library->count(), kMany);
    QCOMPARE(big.library->track(big.ids.last())->category, QStringLiteral("Music"));
    // And afterwards it works, whole.
    QCOMPARE(big.library->remove(big.ids), kMany);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(failed.count(), 2); // no more
}

QTEST_MAIN(LibraryManyTest)
#include "tst_library_many.moc"
