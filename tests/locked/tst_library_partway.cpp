// LOCKED ACCEPTANCE TEST (the library: a call refused part-way changes nothing). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-MANY, L-FAILED). The refusal is real: a trigger, planted through a
// second connection, refuses one id in the middle of the list. The tracks are 2,000 names of one fixture file.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <unistd.h>

#include "library/library.h"

using namespace linuxbee;

namespace {

constexpr int kMany = 2000; // five pieces of 400
const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

struct Big {
    QTemporaryDir dir;
    QString database;
    std::unique_ptr<Library> library;
    QList<qint64> ids; // ascending

    Big()
        : database(dir.path() + QStringLiteral("/library.db"))
        , library(Library::open(database))
    {
        QDir(dir.path()).mkpath(QStringLiteral("music"));
        const QString first = dir.path() + QStringLiteral("/music/t0000.mp3");
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/basic.mp3"), first))
            QTest::qFail("could not copy the fixture", __FILE__, __LINE__);
        for (int i = 1; i < kMany; ++i) {
            const QString path = dir.path() + QStringLiteral("/music/t%1.mp3").arg(i, 4, 10, QLatin1Char('0'));
            if (::link(QFile::encodeName(first).constData(), QFile::encodeName(path).constData()) != 0)
                QTest::qFail("could not make a second name", __FILE__, __LINE__);
        }
        ScanOptions options;
        options.category = QStringLiteral("Music");
        options.now = kNow;
        ids = library->scan({ dir.path() + QStringLiteral("/music") }, options).added;
        std::sort(ids.begin(), ids.end());
        if (ids.size() != kMany)
            QTest::qFail("the scan did not add them all", __FILE__, __LINE__);
    }
    // Runs statements through a connection of the test's own, as another program would.
    void other(const QStringList &statements)
    {
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("other"));
            db.setDatabaseName(database);
            if (!db.open())
                QTest::qFail("could not open the second connection", __FILE__, __LINE__);
            for (const QString &statement : statements) {
                QSqlQuery query(db);
                if (!query.exec(statement))
                    QTest::qFail(qPrintable(query.lastError().text()), __FILE__, __LINE__);
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("other"));
    }
    int inCategory(const QString &category) const
    {
        int n = 0;
        for (const qint64 id : { ids.first(), ids.at(399), ids.at(400), ids.at(1199), ids.at(1500), ids.last() })
            n += library->track(id) && library->track(id)->category == category ? 1 : 0;
        return n;
    }
};

}

class LibraryPartwayTest : public QObject {
    Q_OBJECT
private slots:
    void aRemovalRefusedInTheMiddle();
    void aSendToRefusedInTheMiddle();
};

void LibraryPartwayTest::aRemovalRefusedInTheMiddle()
{
    // L-MANY: the id at place 1500 (the fourth piece of five) cannot be removed. None is.
    Big big;
    big.other({ QStringLiteral("CREATE TRIGGER refuse_one BEFORE DELETE ON tracks WHEN old.id = %1 "
                               "BEGIN SELECT RAISE(ABORT, 'refused by the test'); END")
                    .arg(big.ids.at(1500)) });
    QSignalSpy removed(big.library.get(), &Library::tracksRemoved);
    QSignalSpy failed(big.library.get(), &Library::failed);
    QCOMPARE(big.library->remove(big.ids), 0);
    QCOMPARE(big.library->count(), kMany); // the three pieces before it came back
    QVERIFY(big.library->track(big.ids.first()).has_value());
    QVERIFY(big.library->track(big.ids.at(1199)).has_value());
    QCOMPARE(removed.count(), 0);
    QCOMPARE(failed.count(), 1);
    // Another program sees the same: nothing was stored on the way.
    {
        auto second = Library::open(big.database);
        QVERIFY(second != nullptr);
        QCOMPARE(second->count(), kMany);
    }
    // A removal that does not name that id goes through, so nothing was left held.
    QCOMPARE(big.library->remove(big.ids.mid(0, 1000)), 1000);
    QCOMPARE(big.library->count(), kMany - 1000);
    // With the refusal gone, the rest goes.
    big.other({ QStringLiteral("DROP TRIGGER refuse_one") });
    QCOMPARE(big.library->remove(big.ids), kMany - 1000);
    QCOMPARE(big.library->count(), 0);
    QCOMPARE(failed.count(), 1);
}

void LibraryPartwayTest::aSendToRefusedInTheMiddle()
{
    // L-MANY: the same for setCategory.
    Big big;
    big.other({ QStringLiteral("CREATE TRIGGER refuse_one BEFORE UPDATE ON tracks WHEN old.id = %1 "
                               "BEGIN SELECT RAISE(ABORT, 'refused by the test'); END")
                    .arg(big.ids.at(1500)) });
    QSignalSpy updated(big.library.get(), &Library::tracksUpdated);
    QSignalSpy failed(big.library.get(), &Library::failed);
    QCOMPARE(big.library->setCategory(big.ids, QStringLiteral("Podcasts")), 0);
    QCOMPARE(big.inCategory(QStringLiteral("Music")), 6);
    QCOMPARE(big.inCategory(QStringLiteral("Podcasts")), 0);
    QCOMPARE(updated.count(), 0);
    QCOMPARE(failed.count(), 1);
    {
        auto second = Library::open(big.database);
        QVERIFY(second != nullptr);
        QCOMPARE(second->track(big.ids.first())->category, QStringLiteral("Music"));
    }
    big.other({ QStringLiteral("DROP TRIGGER refuse_one") });
    QCOMPARE(big.library->setCategory(big.ids, QStringLiteral("Podcasts")), kMany);
    QCOMPARE(big.inCategory(QStringLiteral("Podcasts")), 6);
}

QTEST_MAIN(LibraryPartwayTest)
#include "tst_library_partway.moc"
