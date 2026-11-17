// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// Every change the library is asked for and the database refuses: nothing changes, and it is said.
// Expected values: docs/spec/library.md (L-FAILED, L-EDIT relinkPaths, L-OPEN). The refusals are
// real: another connection holding the write lock, and triggers planted through another connection,
// one kind that fails the statement and one that ends the transaction (what a full disk does).
// Copies of fixtures in a temporary folder.
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

#include <chrono>

#include "library/library.h"

using namespace linuxbee;
using namespace std::chrono_literals;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

enum class Call { Refresh, SetPath, Relink, RecordPlay, RecordSkip, SetStatistics, AddFile };
enum class Refusal { Held, Fail, Rollback };

// Another connection to the same file, open for as long as this lives.
struct Other {
    QString name;
    explicit Other(const QString &database)
        : name(QStringLiteral("other-%1").arg(quintptr(this)))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(database);
        if (!db.open())
            QTest::qFail("the other connection did not open", __FILE__, __LINE__);
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
    QStringList run(const QString &statement)
    {
        QStringList rows;
        QSqlQuery query(QSqlDatabase::database(name));
        if (!query.exec(statement))
            QTest::qFail(qPrintable(statement + QStringLiteral(": ") + query.lastError().text()),
                __FILE__, __LINE__);
        while (query.next()) {
            QStringList row;
            for (int i = 0; i < query.record().count(); ++i)
                row.append(query.value(i).toString());
            rows.append(row.join(QLatin1Char('|')));
        }
        return rows;
    }
};

// Three tracks a, b, c, in that order of path.
struct Box {
    QTemporaryDir dir;
    QString database, music, elsewhere;
    std::unique_ptr<Library> library;
    qint64 a = 0, b = 0, c = 0;

    Box()
        : database(dir.path() + QStringLiteral("/lib/library.db"))
        , music(dir.path() + QStringLiteral("/music"))
        , elsewhere(dir.path() + QStringLiteral("/elsewhere"))
    {
        QDir(dir.path()).mkpath(QStringLiteral("lib"));
        QDir(dir.path()).mkpath(QStringLiteral("music"));
        put("basic.flac", "a.flac");
        put("basic.mp3", "b.mp3");
        put("basic.ogg", "c.ogg");
        library = Library::open(database);
        ScanOptions options;
        options.now = kNow;
        if (!library || library->scan({ music }, options).added.size() != 3) {
            QTest::qFail("the first scan did not add the three files", __FILE__, __LINE__);
            return;
        }
        a = library->trackByPath(music + QStringLiteral("/a.flac"))->id;
        b = library->trackByPath(music + QStringLiteral("/b.mp3"))->id;
        c = library->trackByPath(music + QStringLiteral("/c.ogg"))->id;
    }
    void put(const char *from, const char *to)
    {
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(from),
                music + QLatin1Char('/') + QLatin1String(to)))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    }
    QStringList rows()
    {
        Other other(database);
        return other.run(QStringLiteral("SELECT * FROM tracks ORDER BY id"));
    }
    // Makes sure the call has something to do.
    void prepare(Call call)
    {
        if (call == Call::Refresh) {
            QFile changed(music + QStringLiteral("/b.mp3"));
            if (!changed.open(QIODevice::ReadWrite)
                || !changed.setFileTime(kNow.addDays(-30), QFileDevice::FileModificationTime))
                QTest::qFail("could not change b's time", __FILE__, __LINE__);
        }
        if (call == Call::AddFile)
            put("basic.m4a", "d.m4a");
    }
    bool run(Call call)
    {
        switch (call) {
        case Call::Refresh:
            return library->refresh(b);
        case Call::SetPath:
            return library->setPath(b, music + QStringLiteral("/moved/b.mp3"));
        case Call::Relink:
            return library->relinkPaths(music, elsewhere) == 3;
        case Call::RecordPlay:
            return library->recordPlay(b, kNow);
        case Call::RecordSkip:
            return library->recordSkip(b, kNow);
        case Call::SetStatistics:
            return library->setStatistics(b, TrackStatistics { kNow.addDays(-1), 7, 2, kNow });
        case Call::AddFile:
            return library->addFile(music + QStringLiteral("/d.m4a"), QStringLiteral("Music"), kNow)
                > 0;
        }
        return false;
    }
};

struct Spies {
    QSignalSpy failed, added, updated, removed, moved;
    explicit Spies(Library *library)
        : failed(library, &Library::failed)
        , added(library, &Library::tracksAdded)
        , updated(library, &Library::tracksUpdated)
        , removed(library, &Library::tracksRemoved)
        , moved(library, &Library::pathsMoved)
    {
    }
    int changes() const
    {
        return int(added.size() + updated.size() + removed.size() + moved.size());
    }
};

}

class LibraryRefusedCallsTest : public QObject {
    Q_OBJECT
private slots:
    void aRefusedCallChangesNothingAndSaysSo_data();
    void aRefusedCallChangesNothingAndSaysSo();
    void nothingToDoIsNotARefusal();
    void nothingToDoWhileAnotherHoldsTheLibrary();
    void aRefusalIsNotRememberedForTheNextCall_data();
    void aRefusalIsNotRememberedForTheNextCall();
    void aRelinkIsOneChange_data();
    void aRelinkIsOneChange();
    void aLibraryThatCannotBeWrittenSaysSo();
};

void LibraryRefusedCallsTest::aRefusedCallChangesNothingAndSaysSo_data()
{
    QTest::addColumn<int>("call");
    QTest::addColumn<int>("refusal");
    const QList<QPair<const char *, Call>> calls { { "refresh", Call::Refresh },
        { "setPath", Call::SetPath }, { "relinkPaths", Call::Relink },
        { "recordPlay", Call::RecordPlay }, { "recordSkip", Call::RecordSkip },
        { "setStatistics", Call::SetStatistics }, { "addFile", Call::AddFile } };
    const QList<QPair<const char *, Refusal>> refusals {
        { "held by another program", Refusal::Held }, { "the statement fails", Refusal::Fail },
        { "the transaction is ended", Refusal::Rollback }
    };
    for (const auto &call : calls) {
        for (const auto &refusal : refusals)
            QTest::addRow("%s, %s", call.first, refusal.first)
                << int(call.second) << int(refusal.second);
    }
}

void LibraryRefusedCallsTest::aRefusedCallChangesNothingAndSaysSo()
{
    QFETCH(int, call);
    QFETCH(int, refusal);
    const Call what = Call(call);
    Box box;
    QVERIFY(box.library);
    box.prepare(what);
    const QStringList before = box.rows();
    QCOMPARE(before.size(), 3);
    box.library->setWriteWait(100ms);
    Spies spies(box.library.get());

    {
        Other other(box.database);
        if (Refusal(refusal) == Refusal::Held) {
            other.run(QStringLiteral("BEGIN IMMEDIATE"));
        } else {
            const QString raise = Refusal(refusal) == Refusal::Fail ? QStringLiteral("FAIL")
                                                                    : QStringLiteral("ROLLBACK");
            const QString when = what == Call::AddFile
                ? QStringLiteral("BEFORE INSERT ON tracks")
                : QStringLiteral("BEFORE UPDATE ON tracks WHEN OLD.id = %1").arg(box.b);
            other.run(QStringLiteral(
                "CREATE TRIGGER refuse_one %1 BEGIN SELECT RAISE(%2, 'refused for the test'); END")
                    .arg(when, raise));
        }
        QVERIFY2(!box.run(what), "the call said it had done what the database refused");
        QCOMPARE(spies.failed.size(), 1);
        QVERIFY(!spies.failed.at(0).at(0).toString().isEmpty());
        if (Refusal(refusal) != Refusal::Held)
            QVERIFY2(spies.failed.at(0).at(0).toString().contains(
                         QStringLiteral("refused for the test")),
                qPrintable(spies.failed.at(0).at(0).toString()));
        QCOMPARE(spies.changes(), 0);
        if (Refusal(refusal) != Refusal::Held)
            other.run(QStringLiteral("DROP TRIGGER refuse_one"));
    }
    QCOMPARE(box.rows(), before); // not a row differs
    // What the library hands out is as before too.
    QCOMPARE(box.library->count(), 3);
    QCOMPARE(box.library->track(box.b)->path, box.music + QStringLiteral("/b.mp3"));
    QCOMPARE(box.library->track(box.b)->playCount, 0);

    // The same call again, with nothing in its way.
    QVERIFY(box.run(what));
    QCOMPARE(spies.failed.size(), 1);
    QVERIFY(spies.changes() >= 1);
    QVERIFY(box.rows() != before);
}

void LibraryRefusedCallsTest::nothingToDoIsNotARefusal()
{
    Box box;
    QVERIFY(box.library);
    const QStringList before = box.rows();
    Spies spies(box.library.get());
    const qint64 nobody = box.c + 1000;
    QVERIFY(!box.library->refresh(nobody));
    QVERIFY(QFile::remove(box.music + QStringLiteral("/c.ogg")));
    QVERIFY(!box.library->refresh(box.c)); // the file cannot be read
    QVERIFY(!box.library->setPath(nobody, box.music + QStringLiteral("/x.mp3")));
    QVERIFY(
        !box.library->setPath(box.a, box.music + QStringLiteral("/b.mp3"))); // another track's path
    QVERIFY(
        box.library->setPath(box.a, box.music + QStringLiteral("/a.flac"))); // where it is already
    QCOMPARE(
        box.library->relinkPaths(box.dir.path() + QStringLiteral("/nowhere"), box.elsewhere), 0);
    QVERIFY(!box.library->recordPlay(nobody, kNow));
    QVERIFY(!box.library->recordPlay(box.a, QDateTime()));
    QVERIFY(!box.library->recordSkip(nobody, kNow));
    QVERIFY(!box.library->setStatistics(nobody, TrackStatistics { kNow, 1, 1, kNow }));
    QCOMPARE(box.library->addFile(
                 box.music + QStringLiteral("/not-there.mp3"), QStringLiteral("Music"), kNow),
        0);
    QCOMPARE(
        box.library->addFile(box.music + QStringLiteral("/a.flac"), QStringLiteral("Music"), kNow),
        box.a);
    QCOMPARE(box.library->addFile(
                 box.music + QStringLiteral("/a.flac"), QStringLiteral("No such"), kNow),
        0);
    QCOMPARE(spies.failed.size(), 0);
    QCOMPARE(spies.changes(), 0);
    QCOMPARE(box.rows(), before);
}

// L-FAILED: nothing to do is found out before the lock is asked for. (Found by the review of
// fix-70a: a play for an unknown id waited out another program's lock and then said "failed".)
// Every call here has nothing to do, and another program holds the library throughout.
void LibraryRefusedCallsTest::nothingToDoWhileAnotherHoldsTheLibrary()
{
    Box box;
    QVERIFY(box.library);
    QVERIFY(QFile::remove(box.music + QStringLiteral("/c.ogg")));
    const QStringList before = box.rows();
    const QString other = Library::categories().value(1);
    QVERIFY(!other.isEmpty() && other != QLatin1String("Music"));
    box.library->setWriteWait(100ms);
    Spies spies(box.library.get());
    const qint64 nobody = box.c + 1000;
    const QString nowhere = box.dir.path() + QStringLiteral("/nowhere");
    // The lists are named, so that nothing below is built from a temporary inside a longer
    // expression.
    const QList<qint64> none;
    const QList<qint64> unknown { nobody, nobody + 1 };
    const QList<qint64> oneUnknown { nobody };
    const QList<qint64> aAndB { box.a, box.b };
    const QList<qint64> justA { box.a };
    const QList<qint64> unknownAndA { nobody, box.a };
    const QList<FileMove> noMoves;
    const QList<FileMove> notOurs { FileMove {
        nowhere + QStringLiteral("/a"), nowhere + QStringLiteral("/b") } };
    const QList<FileMove> toItself { FileMove {
        box.music + QStringLiteral("/a.flac"), box.music + QStringLiteral("/a.flac") } };
    const TrackStatistics counted { kNow, 1, 1, kNow };
    const TrackStatistics undated { QDateTime(), 1, 1, kNow };
    {
        Other holder(box.database);
        holder.run(QStringLiteral("BEGIN IMMEDIATE"));
        const auto quiet = [&spies](const char *what) {
            if (spies.failed.isEmpty())
                return true;
            QTest::qFail(qPrintable(QStringLiteral("%1 said failed: %2")
                                 .arg(QLatin1String(what), spies.failed.at(0).at(0).toString())),
                __FILE__, __LINE__);
            return false;
        };
        QVERIFY(!box.library->refresh(nobody) && quiet("refresh, unknown"));
        QVERIFY(!box.library->refresh(box.c) && quiet("refresh, the file is gone"));
        QVERIFY(!box.library->setPath(nobody, box.music + QStringLiteral("/x.mp3"))
            && quiet("setPath, unknown"));
        QVERIFY(!box.library->setPath(box.a, box.music + QStringLiteral("/b.mp3"))
            && quiet("setPath, another's"));
        QVERIFY(box.library->setPath(box.a, box.music + QStringLiteral("/a.flac"))
            && quiet("setPath, the same"));
        QVERIFY(box.library->relinkPaths(nowhere, box.elsewhere) == 0
            && quiet("relinkPaths, nothing under it"));
        QVERIFY(!box.library->recordPlay(nobody, kNow) && quiet("recordPlay, unknown"));
        QVERIFY(!box.library->recordPlay(box.a, QDateTime()) && quiet("recordPlay, no time"));
        QVERIFY(!box.library->recordSkip(nobody, kNow) && quiet("recordSkip, unknown"));
        QVERIFY(!box.library->recordSkip(box.a, QDateTime()) && quiet("recordSkip, no time"));
        QVERIFY(!box.library->setStatistics(nobody, counted) && quiet("setStatistics, unknown"));
        QVERIFY(
            !box.library->setStatistics(box.a, undated) && quiet("setStatistics, no date added"));
        QVERIFY(
            box.library->addFile(nowhere + QStringLiteral("/n.mp3"), QStringLiteral("Music"), kNow)
                == 0
            && quiet("addFile, not there"));
        QVERIFY(box.library->addFile(
                    box.music + QStringLiteral("/a.flac"), QStringLiteral("Music"), kNow)
                == box.a
            && quiet("addFile, known"));
        QVERIFY(box.library->remove(none) == 0 && quiet("remove, none"));
        QVERIFY(box.library->remove(unknown) == 0 && quiet("remove, unknown"));
        QVERIFY(box.library->setCategory(oneUnknown, other) == 0 && quiet("setCategory, unknown"));
        QVERIFY(box.library->setCategory(aAndB, QStringLiteral("Music")) == 0
            && quiet("setCategory, there already"));
        QVERIFY(box.library->setCategory(justA, QStringLiteral("No such")) == 0
            && quiet("setCategory, no such section"));
        QVERIFY(box.library->recordMoves(noMoves) == 0 && quiet("recordMoves, none"));
        QVERIFY(box.library->recordMoves(notOurs) == 0 && quiet("recordMoves, no track of ours"));
        QVERIFY(box.library->recordMoves(toItself) == 0 && quiet("recordMoves, to where it is"));
        QCOMPARE(spies.changes(), 0);

        // And a call that has something to do, among them, is still refused and said.
        QCOMPARE(box.library->remove(unknownAndA), 0);
        QCOMPARE(spies.failed.size(), 1);
        QCOMPARE(box.library->setCategory(aAndB, other), 0);
        QCOMPARE(spies.failed.size(), 2);
    }
    QCOMPARE(box.rows(), before);
}

void LibraryRefusedCallsTest::aRefusalIsNotRememberedForTheNextCall_data()
{
    QTest::addColumn<QString>("next");
    for (const char *next : { "refresh nobody", "setPath nobody", "setPath to another's path",
             "relink nowhere", "recordPlay nobody", "recordPlay at no time", "recordSkip nobody",
             "setStatistics nobody", "addFile not there", "addFile known",
             "addFile no such category" })
        QTest::addRow("%s", next) << QString::fromLatin1(next);
}

// A refusal's words belong to the call that was refused. The very next call, with nothing to do, is
// silent, whichever call it is.
void LibraryRefusedCallsTest::aRefusalIsNotRememberedForTheNextCall()
{
    QFETCH(QString, next);
    Box box;
    QVERIFY(box.library);
    box.library->setWriteWait(100ms);
    Spies spies(box.library.get());
    const qint64 nobody = box.c + 1000;
    {
        Other other(box.database);
        other.run(QStringLiteral("BEGIN IMMEDIATE"));
        // Refused by a call of another kind than the one that follows.
        if (next.startsWith(QLatin1String("recordPlay")))
            QVERIFY(!box.library->recordSkip(box.b, kNow));
        else
            QVERIFY(!box.library->recordPlay(box.b, kNow));
    }
    QCOMPARE(spies.failed.size(), 1);
    if (next == QLatin1String("refresh nobody"))
        QVERIFY(!box.library->refresh(nobody));
    else if (next == QLatin1String("setPath nobody"))
        QVERIFY(!box.library->setPath(nobody, box.music + QStringLiteral("/x.mp3")));
    else if (next == QLatin1String("setPath to another's path"))
        QVERIFY(!box.library->setPath(box.a, box.music + QStringLiteral("/b.mp3")));
    else if (next == QLatin1String("relink nowhere"))
        QCOMPARE(
            box.library->relinkPaths(box.dir.path() + QStringLiteral("/nowhere"), box.elsewhere),
            0);
    else if (next == QLatin1String("recordPlay nobody"))
        QVERIFY(!box.library->recordPlay(nobody, kNow));
    else if (next == QLatin1String("recordPlay at no time"))
        QVERIFY(!box.library->recordPlay(box.a, QDateTime()));
    else if (next == QLatin1String("recordSkip nobody"))
        QVERIFY(!box.library->recordSkip(nobody, kNow));
    else if (next == QLatin1String("setStatistics nobody"))
        QVERIFY(!box.library->setStatistics(nobody, TrackStatistics { kNow, 1, 1, kNow }));
    else if (next == QLatin1String("addFile not there"))
        QCOMPARE(box.library->addFile(
                     box.music + QStringLiteral("/not-there.mp3"), QStringLiteral("Music"), kNow),
            0);
    else if (next == QLatin1String("addFile known"))
        QCOMPARE(box.library->addFile(
                     box.music + QStringLiteral("/a.flac"), QStringLiteral("Music"), kNow),
            box.a);
    else if (next == QLatin1String("addFile no such category"))
        QCOMPARE(box.library->addFile(
                     box.music + QStringLiteral("/a.flac"), QStringLiteral("No such"), kNow),
            0);
    else
        QFAIL("a row this test does not know");
    QCOMPARE(spies.failed.size(), 1);
    QCOMPARE(spies.changes(), 0);
}

void LibraryRefusedCallsTest::aRelinkIsOneChange_data()
{
    QTest::addColumn<QString>("letter"); // the track whose step is refused
    QTest::addColumn<QString>("raise");
    for (const char *letter : { "a", "b", "c" }) {
        for (const char *raise : { "FAIL", "ROLLBACK" })
            QTest::addRow("%s, %s", letter, raise)
                << QString::fromLatin1(letter) << QString::fromLatin1(raise);
    }
}

// L-EDIT relinkPaths: three tracks under the folder. Whichever one is refused, none has moved: a
// library with half its folder at the new place is what this rule is against.
void LibraryRefusedCallsTest::aRelinkIsOneChange()
{
    QFETCH(QString, letter);
    QFETCH(QString, raise);
    Box box;
    QVERIFY(box.library);
    const qint64 id
        = letter == QLatin1String("a") ? box.a : (letter == QLatin1String("b") ? box.b : box.c);
    const QStringList before = box.rows();
    Spies spies(box.library.get());
    {
        Other other(box.database);
        other.run(
            QStringLiteral("CREATE TRIGGER refuse_one BEFORE UPDATE ON tracks WHEN OLD.id = %1 "
                           "BEGIN SELECT RAISE(%2, 'refused for the test'); END")
                .arg(id)
                .arg(raise));
        QCOMPARE(box.library->relinkPaths(box.music, box.elsewhere), 0);
        other.run(QStringLiteral("DROP TRIGGER refuse_one"));
    }
    QCOMPARE(spies.failed.size(), 1);
    QCOMPARE(spies.changes(), 0);
    QCOMPARE(box.rows(), before);
    for (const LibraryTrack &track : box.library->tracks())
        QVERIFY2(track.path.startsWith(box.music + QLatin1Char('/')), qPrintable(track.path));

    QCOMPARE(box.library->relinkPaths(box.music, box.elsewhere), 3);
    QCOMPARE(spies.failed.size(), 1);
    QCOMPARE(spies.updated.size(), 1);
    QCOMPARE(spies.moved.size(), 1);
    for (const LibraryTrack &track : box.library->tracks())
        QVERIFY2(track.path.startsWith(box.elsewhere + QLatin1Char('/')), qPrintable(track.path));
    // And it holds the lock for the whole of it: held by another, it is refused before the first
    // step.
    box.library->setWriteWait(100ms);
    {
        Other other(box.database);
        other.run(QStringLiteral("BEGIN IMMEDIATE"));
        QCOMPARE(box.library->relinkPaths(box.elsewhere, box.music), 0);
    }
    QCOMPARE(spies.failed.size(), 2);
}

// L-OPEN: a library whose folder cannot be written to. The words used to be "That file is not a
// linuxBee library", which it is.
void LibraryRefusedCallsTest::aLibraryThatCannotBeWrittenSaysSo()
{
    QTemporaryDir dir;
    const QString folder = dir.path() + QStringLiteral("/lib");
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("lib")));
    const QString database = folder + QStringLiteral("/library.db");
    {
        const std::unique_ptr<Library> made = Library::open(database);
        QVERIFY(made);
    }
    QVERIFY(QFile::exists(database));
    const auto all = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    QVERIFY(QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    QString error;
    const std::unique_ptr<Library> opened = Library::open(database, &error);
    QFile::setPermissions(folder, all); // so that the folder can be cleared away
    if (opened)
        QSKIP("this user may write anywhere (root?), so the case cannot be made");
    QVERIFY2(error.contains(QStringLiteral("cannot be written")), qPrintable(error));
    QVERIFY2(!error.contains(QStringLiteral("not a linuxBee library")), qPrintable(error));
    // Nothing was left behind, and it opens again once the folder can be written to.
    QCOMPARE(QDir(folder).entryList(QDir::Files), QStringList { QStringLiteral("library.db") });
    QVERIFY(Library::open(database));
}

QTEST_GUILESS_MAIN(LibraryRefusedCallsTest)
#include "tst_library_refused_calls.moc"
