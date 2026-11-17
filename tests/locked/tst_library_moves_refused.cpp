// LOCKED ACCEPTANCE TEST (the library: a run of moves is recorded whole or not at all). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-MOVED, L-FAILED). The refusals are real: triggers planted through
// a second connection, one that fails the statement and one that ends the whole transaction (as a full disk does).
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "library/library.h"

using namespace linuxbee;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

// The review's case: e goes to E; c takes a's place (a is replaced); the file at a goes on to b.
struct Box {
    QTemporaryDir dir;
    QString database;
    std::unique_ptr<Library> library;
    QString a, c, e, b, bigE;
    qint64 ia = 0, ic = 0, ie = 0;

    Box()
        : database(dir.path() + QStringLiteral("/library.db"))
        , library(Library::open(database))
    {
        QDir(dir.path()).mkpath(QStringLiteral("music"));
        a = put("basic.flac", "music/a.flac");
        c = put("basic.mp3", "music/c.mp3");
        e = put("basic.ogg", "music/e.ogg");
        b = dir.path() + QStringLiteral("/music/b.mp3");
        bigE = dir.path() + QStringLiteral("/music/E.ogg");
        ia = library->addFile(a, QStringLiteral("Music"), kNow);
        ic = library->addFile(c, QStringLiteral("Music"), kNow);
        ie = library->addFile(e, QStringLiteral("Music"), kNow);
        if (ia == 0 || ic == 0 || ie == 0)
            QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        library->recordPlay(ic, kNow);
        library->recordPlay(ic, kNow.addSecs(60)); // c has been played twice
        // The files move as Organise moved them.
        if (!QFile::rename(e, bigE) || !QFile::remove(a) || !QFile::rename(c, a) || !QFile::rename(a, b))
            QTest::qFail("could not move the files", __FILE__, __LINE__);
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    QList<FileMove> moves() const { return { { e, bigE }, { c, a }, { a, b } }; }
    void other(const QString &statement)
    {
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("other"));
            db.setDatabaseName(database);
            QSqlQuery query(db);
            if (!db.open() || !query.exec(statement))
                QTest::qFail(qPrintable(query.lastError().text()), __FILE__, __LINE__);
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("other"));
    }
    // "<path> plays=<n>" for each of the three tracks, or "gone".
    QStringList state(Library *of = nullptr) const
    {
        QStringList out;
        for (const qint64 id : { ia, ic, ie }) {
            const auto t = (of ? of : library.get())->track(id);
            out.append(t ? QStringLiteral("%1 plays=%2").arg(QFileInfo(t->path).fileName()).arg(t->playCount) : QStringLiteral("gone"));
        }
        return out;
    }
};

const QStringList kBefore { QStringLiteral("a.flac plays=0"), QStringLiteral("c.mp3 plays=2"), QStringLiteral("e.ogg plays=0") };
const QStringList kAfter { QStringLiteral("gone"), QStringLiteral("b.mp3 plays=2"), QStringLiteral("E.ogg plays=0") };

}

class LibraryMovesRefusedTest : public QObject {
    Q_OBJECT
private slots:
    void refusedInTheMiddle_data();
    void refusedInTheMiddle();
    void allWell();
};

void LibraryMovesRefusedTest::refusedInTheMiddle_data()
{
    QTest::addColumn<QString>("trigger");
    // A_ID is a's track, the one that has to go; C_ID is c's, the one that has to move.
    QTest::newRow("the removal is refused") << QStringLiteral("BEFORE DELETE ON tracks WHEN old.id = A_ID BEGIN SELECT RAISE(ABORT, 'refused by the test'); END");
    QTest::newRow("the removal ends the whole transaction") << QStringLiteral("BEFORE DELETE ON tracks WHEN old.id = A_ID BEGIN SELECT RAISE(ROLLBACK, 'refused by the test'); END");
    QTest::newRow("the move is refused") << QStringLiteral("BEFORE UPDATE ON tracks WHEN old.id = C_ID BEGIN SELECT RAISE(ABORT, 'refused by the test'); END");
    QTest::newRow("the move ends the whole transaction") << QStringLiteral("BEFORE UPDATE ON tracks WHEN old.id = C_ID BEGIN SELECT RAISE(ROLLBACK, 'refused by the test'); END");
    QTest::newRow("the last move is refused") << QStringLiteral("BEFORE UPDATE ON tracks WHEN old.id = C_ID AND new.path LIKE '%/b.mp3' BEGIN SELECT RAISE(ABORT, 'refused by the test'); END");
}

void LibraryMovesRefusedTest::refusedInTheMiddle()
{
    // L-MOVED: a run of moves is one change. If any step of it is refused, the library is as it was before
    // the call: no track moved, none removed, no play count on another file. And it says so (L-FAILED).
    QFETCH(QString, trigger);
    Box box;
    QCOMPARE(box.state(), kBefore);
    trigger.replace(QStringLiteral("A_ID"), QString::number(box.ia));
    trigger.replace(QStringLiteral("C_ID"), QString::number(box.ic));
    box.other(QStringLiteral("CREATE TRIGGER refuse_one ") + trigger);
    QSignalSpy failed(box.library.get(), &Library::failed);
    QSignalSpy removed(box.library.get(), &Library::tracksRemoved);
    QSignalSpy updated(box.library.get(), &Library::tracksUpdated);
    QSignalSpy moved(box.library.get(), &Library::pathsMoved);

    QCOMPARE(box.library->recordMoves(box.moves()), 0);
    QCOMPARE(box.state(), kBefore);
    QCOMPARE(box.library->count(), 3);
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed.at(0).at(0).toString().isEmpty());
    QCOMPARE(removed.count(), 0);
    QCOMPARE(updated.count(), 0);
    QCOMPARE(moved.count(), 0);
    {
        // Another program sees the same: nothing was stored on the way, and nothing is left held.
        auto second = Library::open(box.database);
        QVERIFY(second != nullptr);
        QCOMPARE(box.state(second.get()), kBefore);
        QVERIFY(second->recordPlay(box.ie, kNow));
    }
    // The same run again, with the refusal gone, is recorded whole: nothing of the first try is in the way.
    box.other(QStringLiteral("DROP TRIGGER refuse_one"));
    QCOMPARE(box.library->recordMoves(box.moves()), 2);
    QStringList after = kAfter;
    after[2] = QStringLiteral("E.ogg plays=1"); // the play the other program counted
    QCOMPARE(box.state(), after);
    QVERIFY(!box.library->track(box.ic)->missing);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(moved.count(), 1);
}

void LibraryMovesRefusedTest::allWell()
{
    // The same run with nothing refused, for comparison: c's plays are with c's file, at b.
    Box box;
    QSignalSpy failed(box.library.get(), &Library::failed);
    QCOMPARE(box.library->recordMoves(box.moves()), 2);
    QCOMPARE(box.state(), kAfter);
    QCOMPARE(failed.count(), 0);
}

QTEST_MAIN(LibraryMovesRefusedTest)
#include "tst_library_moves_refused.moc"
