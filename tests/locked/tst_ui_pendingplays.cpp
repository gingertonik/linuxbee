// LOCKED ACCEPTANCE TEST (a play the library could not count is kept and tried again, and the
// window does not wait five seconds for it). Written by the reviewer, not the builder. The builder
// makes these pass by changing src/, never by editing this file. A test believed to be wrong is
// reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-WAIT, "nobody is waiting") and
// docs/spec/playback-shell.md (PS-COUNT). The library is a file here; another holder has it for
// writing, so the library refuses. No sound is involved.
#include <QAction>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"
#include "shell/pendingplays.h"
#include "shell/playbackshell.h"

using namespace linuxbee;
using namespace linuxbee::ui;
using namespace std::chrono_literals;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

struct Files {
    QTemporaryDir dir;
    QString a, b, c;
    QDateTime t0 = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<Library> library;

    // `withoutLog`: the library is one that could not have a write-ahead log (as on a network
    // disk): another program that holds it stops reading too.
    explicit Files(bool withoutLog = false)
        : library(Library::open(dir.path() + QStringLiteral("/library.db")))
    {
        QDir(dir.path()).mkpath(QStringLiteral("music"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.m4a", "music/b.m4a");
        c = put("basic.ogg", "music/c.ogg");
        for (const QString &p : { a, b, c })
            if (library->addFile(p, QStringLiteral("Music"), t0) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        if (withoutLog)
            reopenWithoutLog();
        library->setWriteWait(50ms);
        library->setQuickWait(1ms); // not the quarter second: these tests refuse thousands of times
    }
    // The library file is made one without the log, and stays so: it is opened while someone reads,
    // so that it cannot be given a log.
    void reopenWithoutLog()
    {
        const QString db = dir.path() + QStringLiteral("/library.db");
        const QString name = QStringLiteral("nolog-%1").arg(quintptr(this));
        library.reset();
        QString mode;
        {
            QSqlDatabase other = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            other.setDatabaseName(db);
            QSqlQuery q(other);
            if (!other.open() || !q.exec(QStringLiteral("PRAGMA journal_mode=DELETE"))
                || !q.exec(QStringLiteral("BEGIN"))
                || !q.exec(QStringLiteral("SELECT count(*) FROM tracks")) || !q.next())
                QTest::qFail("could not prepare a library without the log", __FILE__, __LINE__);
            q.finish();
            library = Library::open(db);
            q.exec(QStringLiteral("ROLLBACK"));
            if (q.exec(QStringLiteral("PRAGMA journal_mode")) && q.next())
                mode = q.value(0).toString();
            q.finish();
            other.close();
        }
        QSqlDatabase::removeDatabase(name);
        if (!library || mode != QLatin1String("delete"))
            QTest::qFail(
                "this test could not make a library without the log; the case is not tested",
                __FILE__, __LINE__);
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from),
                target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    qint64 id(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->id : 0;
    }
    int plays(const QString &path) const { return library->trackByPath(path)->playCount; }
    int skips(const QString &path) const { return library->trackByPath(path)->skipCount; }
    QDateTime last(const QString &path) const { return library->trackByPath(path)->lastPlayed; }
    QDateTime at(int seconds) const { return t0.addSecs(seconds); }
};

// Holds the library's database against readers and writers alike, as a program does while it writes
// to a library that has no log.
struct Exclusive {
    QString name;
    explicit Exclusive(const Files &f)
        : name(QStringLiteral("exclusive-%1").arg(quintptr(this)))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(f.dir.path() + QStringLiteral("/library.db"));
        if (!db.open() || !QSqlQuery(db).exec(QStringLiteral("BEGIN EXCLUSIVE")))
            QTest::qFail("could not hold the database", __FILE__, __LINE__);
    }
    ~Exclusive()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(name);
            QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
};

// Holds the library's database for writing for as long as it lives.
struct Holder {
    QString name;
    explicit Holder(const Files &f)
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

// A backend that only does what the test says.
class Tape : public AudioBackend {
    Q_OBJECT
public:
    void load(const QString &) override { }
    void play() override { }
    void pause() override { }
    void stop() override { }
    void seek(qint64) override { }
    void setVolume(double) override { }
    void at(qint64 ms) { emit positionChanged(ms); }
    void lasts(qint64 ms) { emit durationChanged(ms); }
};

struct Rig : Files {
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    Tape tape;
    QDateTime now = t0;
    std::unique_ptr<LibraryShell> lib;
    std::unique_ptr<PlaybackShell> play;

    explicit Rig(bool withoutLog = false)
        : Files(withoutLog)
        , playlists(made())
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        lib = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        play = std::make_unique<PlaybackShell>(
            &window, &actions, library.get(), lib.get(), &tape, &settings, [this] { return now; });
    }
    QString made()
    {
        QDir(dir.path()).mkpath(QStringLiteral("playlists"));
        return dir.path() + QStringLiteral("/playlists");
    }
    PendingPlays *pending() { return play->findChild<PendingPlays *>(); }
    QString bar() { return window.statusBar()->currentMessage(); }
    // The track now playing is heard to its end, `seconds` after the last.
    void hearToTheEnd(int seconds)
    {
        tape.lasts(100000);
        tape.at(99000);
        now = now.addSecs(seconds);
        emit tape.ended();
    }
};

}

class PendingPlaysTest : public QObject {
    Q_OBJECT
private slots:
    void aPlayWaitsLittle();
    void countedAtOnce();
    void refusedIsKeptAndCountedLater();
    void theSameTrackTwiceIsTwoPlays();
    void oldestFirstAndSkipsToo();
    void oneCallAsksTheLibraryOnce();
    void nothingToDoIsNotKept();
    void oneChangeForAll();
    void theLibraryTakesAllOrNone();
    void aLibraryWithoutItsLog();
    void lostAtTheEndIsSaid();
    void noMoreThanAThousand();
    void theShellCountsThroughIt();
    void theShellTriesAgainOnTheWayOut();
};

void PendingPlaysTest::aPlayWaitsLittle()
{
    // L-WAIT: a change nobody is waiting for waits the short wait; a command waits the long one,
    // whatever came before it. (The one place with a clock: limits, not measures.)
    Files f;
    f.library->setWriteWait(800ms);
    f.library->setQuickWait(100ms);
    QSignalSpy failed(f.library.get(), &Library::failed);
    QVERIFY(f.library->recordPlay(f.id(f.c), f.at(0))); // one that is taken, first
    Holder holder(f);
    QElapsedTimer timer;
    const auto commandWaitsLong = [&](const char *after) {
        timer.restart();
        const int removed = f.library->remove({ f.id(f.c) });
        const qint64 took = timer.elapsed();
        if (removed != 0 || took < 650)
            QTest::qFail(qPrintable(QStringLiteral("after %1, a command waited only %2 ms")
                                 .arg(QLatin1String(after))
                                 .arg(took)),
                __FILE__, __LINE__);
    };
    commandWaitsLong("a play that was taken");

    timer.restart();
    QVERIFY(!f.library->recordPlay(f.id(f.a), f.at(1)));
    qint64 took = timer.elapsed();
    QVERIFY2(took < 500, qPrintable(QStringLiteral("a play held the caller %1 ms").arg(took)));
    QVERIFY2(took >= 70, qPrintable(QStringLiteral("a play did not wait at all: %1 ms").arg(took)));
    commandWaitsLong("a play that was refused");

    timer.restart();
    QVERIFY(!f.library->recordSkip(f.id(f.b), f.at(2)));
    took = timer.elapsed();
    QVERIFY2(took < 500, qPrintable(QStringLiteral("a skip held the caller %1 ms").arg(took)));
    QVERIFY2(took >= 70, qPrintable(QStringLiteral("a skip did not wait at all: %1 ms").arg(took)));
    commandWaitsLong("a skip that was refused");
    QCOMPARE(
        failed.size(), 5); // each refused, and said (L-FAILED): two of these and three commands
}

void PendingPlaysTest::countedAtOnce()
{
    Files f;
    PendingPlays p(f.library.get());
    QSignalSpy kept(&p, &PendingPlays::keptChanged);
    p.play(f.a, f.at(10));
    p.skip(f.b, f.at(20));
    QCOMPARE(f.plays(f.a), 1);
    QCOMPARE(f.last(f.a), f.at(10));
    QCOMPARE(f.skips(f.b), 1);
    QCOMPARE(p.kept(), 0);
    QCOMPARE(kept.size(), 0);
}

void PendingPlaysTest::refusedIsKeptAndCountedLater()
{
    Files f;
    PendingPlays p(f.library.get());
    QSignalSpy kept(&p, &PendingPlays::keptChanged);
    {
        Holder holder(f);
        p.play(f.a, f.at(10));
    }
    QCOMPARE(p.kept(), 1);
    QCOMPARE(kept.size(), 1);
    QCOMPARE(kept.at(0).at(0).toInt(), 1);
    QCOMPARE(f.plays(f.a), 0);
    // The library is free again. Nothing happens by itself; the next play brings the kept one in
    // before it.
    QCOMPARE(f.plays(f.a), 0);
    p.play(f.b, f.at(300));
    QCOMPARE(p.kept(), 0);
    QCOMPARE(f.plays(f.a), 1);
    QCOMPARE(f.last(f.a), f.at(10)); // when it was heard, not when it was stored
    QCOMPARE(f.plays(f.b), 1);
    QCOMPARE(f.last(f.b), f.at(300));
    QCOMPARE(kept.last().at(0).toInt(), 0);
}

void PendingPlaysTest::theSameTrackTwiceIsTwoPlays()
{
    Files f;
    PendingPlays p(f.library.get());
    {
        Holder holder(f);
        p.play(f.a, f.at(10));
        p.play(f.a, f.at(250));
        p.play(f.a, f.at(500));
    }
    QCOMPARE(p.kept(), 3);
    QSignalSpy kept(&p, &PendingPlays::keptChanged);
    p.retry();
    QCOMPARE(p.kept(), 0);
    QCOMPARE(kept.size(), 1); // said by a retry as by a play
    QCOMPARE(kept.at(0).at(0).toInt(), 0);
    p.retry(); // and not when nothing changed
    QCOMPARE(kept.size(), 1);
    QCOMPARE(f.plays(f.a), 3);
    QCOMPARE(f.last(f.a), f.at(500));
}

void PendingPlaysTest::oldestFirstAndSkipsToo()
{
    Files f;
    PendingPlays p(f.library.get());
    {
        Holder holder(f);
        p.play(f.a, f.at(500));
        p.skip(f.b, f.at(20));
        p.play(f.a, f.at(30));
        p.retry(); // still held: nothing changes
        QCOMPARE(p.kept(), 3);
    }
    QCOMPARE(f.plays(f.a), 0);
    p.retry();
    QCOMPARE(p.kept(), 0);
    QCOMPARE(f.plays(f.a), 2);
    QCOMPARE(f.skips(f.b), 1);
    QCOMPARE(f.plays(f.b), 0);
    QCOMPARE(f.last(f.a), f.at(500)); // L-STATS: the later of the two, whichever was stored last
    p.retry(); // with nothing kept: nothing
    QCOMPARE(f.plays(f.a), 2);
}

void PendingPlaysTest::oneCallAsksTheLibraryOnce()
{
    // PS-COUNT: with plays kept and the library still held, a call waits for the library once,
    // however many are kept: it stops at the first refusal, and what is new joins the others
    // untried.
    Files f;
    PendingPlays p(f.library.get());
    QSignalSpy failed(f.library.get(), &Library::failed);
    Holder holder(f);
    p.play(f.a, f.at(1));
    QCOMPARE(failed.size(), 1);
    p.play(f.b, f.at(2));
    QCOMPARE(failed.size(), 2);
    p.skip(f.c, f.at(3));
    QCOMPARE(failed.size(), 3);
    p.retry();
    QCOMPARE(failed.size(), 4);
    QCOMPARE(p.kept(), 3);
}

void PendingPlaysTest::nothingToDoIsNotKept()
{
    // A file the library does not have is not a refusal (L-FAILED): it is not kept, held or free,
    // and does not sit in the number kept while others wait.
    Files f;
    PendingPlays p(f.library.get());
    QSignalSpy failed(f.library.get(), &Library::failed);
    p.play(s(u"/nowhere/x.flac"), f.at(1));
    QCOMPARE(p.kept(), 0);
    {
        Holder holder(f);
        p.play(s(u"/nowhere/x.flac"), f.at(2));
        p.skip(s(u"/nowhere/y.flac"), f.at(3));
        QCOMPARE(p.kept(), 0);
        QCOMPARE(failed.size(), 0); // nothing to do is found out without asking for the lock
        p.play(f.a, f.at(4));
        QCOMPARE(p.kept(), 1);
        p.play(s(u"/nowhere/x.flac"), f.at(5)); // with one kept: still not kept
        QCOMPARE(p.kept(), 1);
        p.play(f.b, f.at(6));
        QCOMPARE(p.kept(), 2);
    }
    // A track that was removed while its play was kept: let go when it is offered, and the rest go
    // on.
    QCOMPARE(f.library->remove({ f.id(f.a) }), 1);
    failed.clear();
    p.retry();
    QCOMPARE(p.kept(), 0);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(f.plays(f.b), 1);
}

void PendingPlaysTest::oneChangeForAll()
{
    // PS-COUNT, L-STATS recordPlays: what is kept goes into the library in one change, with one
    // signal naming each track once. (The review measured 1,001 signals and 3.8 s for a thousand
    // kept plays.)
    Files f;
    PendingPlays p(f.library.get());
    {
        Holder holder(f);
        for (int i = 0; i < 300; ++i) {
            p.play(f.a, f.at(i));
            p.skip(f.b, f.at(i));
            p.play(f.c, f.at(1000 - i));
        }
    }
    QCOMPARE(p.kept(), 900);
    QSignalSpy updated(f.library.get(), &Library::tracksUpdated);
    QSignalSpy failed(f.library.get(), &Library::failed);
    p.retry();
    QCOMPARE(p.kept(), 0);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(updated.size(), 1);
    QList<qint64> ids = updated.at(0).at(0).value<QList<qint64>>();
    std::sort(ids.begin(), ids.end());
    QList<qint64> all { f.id(f.a), f.id(f.b), f.id(f.c) };
    std::sort(all.begin(), all.end());
    QCOMPARE(ids, all);
    QCOMPARE(f.plays(f.a), 300);
    QCOMPARE(f.skips(f.b), 300);
    QCOMPARE(f.plays(f.b), 0);
    QCOMPARE(f.plays(f.c), 300);
    QCOMPARE(f.last(f.a), f.at(299));
    QCOMPARE(f.last(f.c), f.at(1000)); // the latest, though it was the first given
    // Nothing kept: the library is not asked, and nothing is said.
    p.retry();
    QCOMPARE(updated.size(), 1);
}

void PendingPlaysTest::theLibraryTakesAllOrNone()
{
    // L-STATS recordPlays, on the library itself. What it gives back is what was refused and is to
    // be offered again.
    Files f;
    QSignalSpy updated(f.library.get(), &Library::tracksUpdated);
    QSignalSpy failed(f.library.get(), &Library::failed);
    const QString nowhere = s(u"/nowhere/x.flac");
    const QList<PlayRecord> some { { f.a, f.at(10), false }, { nowhere, f.at(11), false },
        { f.b, f.at(12), true }, { f.a, QDateTime(), false }, { f.a, f.at(5), false } };
    QVERIFY(f.library->recordPlays({ }).isEmpty());
    {
        Holder holder(f);
        QVERIFY(f.library->recordPlays({ }).isEmpty());
        QVERIFY(f.library->recordPlays({ { nowhere, f.at(1), false } })
                .isEmpty()); // nothing to do, though held
        QCOMPARE(failed.size(), 0);
        const QList<PlayRecord> back = f.library->recordPlays(some); // refused whole
        QCOMPARE(failed.size(), 1);
        // What comes back is what the library has and could not store, in the order given: not the
        // file it does not have, and not the one with no time.
        QCOMPARE(back.size(), 3);
        QCOMPARE(back.at(0).path, f.a);
        QCOMPARE(back.at(0).when, f.at(10));
        QCOMPARE(back.at(1).path, f.b);
        QVERIFY(back.at(1).skip);
        QCOMPARE(back.at(2).when, f.at(5));
    }
    QCOMPARE(updated.size(), 0);
    QCOMPARE(f.plays(f.a), 0);
    QCOMPARE(f.skips(f.b), 0);
    QVERIFY(f.library->recordPlays(some).isEmpty());
    QCOMPARE(failed.size(), 1);
    QCOMPARE(updated.size(), 1);
    QCOMPARE(updated.at(0).at(0).value<QList<qint64>>().size(), 2);
    QCOMPARE(f.plays(f.a), 2);
    QCOMPARE(f.last(f.a), f.at(10));
    QCOMPARE(f.skips(f.b), 1);
    QVERIFY(f.library->recordPlays({ { nowhere, f.at(1), false } }).isEmpty());
    QCOMPARE(updated.size(), 1); // no signal for nothing
    // A change that fails part-way (as when the disk fills at the second of three) leaves none of
    // them stored: all or none.
    const auto sql = [&f](const QString &text) {
        const QString name = QStringLiteral("sql-%1").arg(quintptr(&f));
        bool ok = false;
        {
            QSqlDatabase other = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            other.setDatabaseName(f.dir.path() + QStringLiteral("/library.db"));
            ok = other.open() && QSqlQuery(other).exec(text);
            other.close();
        }
        QSqlDatabase::removeDatabase(name);
        return ok;
    };
    QVERIFY(sql(
        QStringLiteral("CREATE TRIGGER full BEFORE UPDATE ON tracks WHEN NEW.path LIKE '%/b.m4a' "
                       "BEGIN SELECT RAISE(ABORT, 'database or disk is full'); END")));
    const QList<PlayRecord> three { { f.a, f.at(20), false }, { f.b, f.at(21), false },
        { f.c, f.at(22), false } };
    const QList<PlayRecord> back = f.library->recordPlays(three);
    QCOMPARE(back.size(), 3);
    QCOMPARE(failed.size(), 2);
    QCOMPARE(updated.size(), 1);
    QCOMPARE(f.plays(f.a), 2); // as it was: the first of the three is not left stored
    QCOMPARE(f.plays(f.c), 0);
    QVERIFY(sql(QStringLiteral("DROP TRIGGER full")));
    QVERIFY(f.library->recordPlays(back).isEmpty());
    QCOMPARE(f.plays(f.a), 3);
    QCOMPARE(f.plays(f.b), 1);
    QCOMPARE(f.plays(f.c), 1);
    // And the long wait is what a command gets afterwards (L-WAIT).
    f.library->setWriteWait(700ms);
    Holder holder(f);
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(f.library->remove({ f.id(f.c) }), 0);
    QVERIFY2(timer.elapsed() >= 550,
        qPrintable(QStringLiteral("a command waited only %1 ms").arg(timer.elapsed())));
}

void PendingPlaysTest::aLibraryWithoutItsLog()
{
    // PS-COUNT, found by the review: on a library that could not have a write-ahead log (a network
    // disk), another program that holds it stops reading too. A play was then taken for a play of a
    // file the library does not have, and lost with nothing said, after ten seconds. It is kept.
    // (One library for the whole case: making one without the log takes the opening five seconds.)
    Rig r(true);
    QVERIFY(r.pending());
    const qint64 idA = r.id(r.a);
    PendingPlays p(r.library.get());
    QSignalSpy failed(r.library.get(), &Library::failed);
    r.play->queue()->playNow({ r.c, r.b }, 0);
    {
        Exclusive held(r);
        QVERIFY2(!r.library->trackByPath(r.a),
            "the rig: a reader is not stopped, so this is not the case meant");
        r.library->setWriteWait(1500ms);
        r.library->setQuickWait(100ms);
        QElapsedTimer timer;
        timer.start();
        p.play(r.a, r.at(10));
        p.skip(r.b, r.at(11));
        qint64 took = timer.elapsed();
        QCOMPARE(p.kept(), 2); // the library could not even look: all of it is kept
        QCOMPARE(failed.size(), 2); // said, each time
        // Two short waits, not long ones. (Limits, not measures.)
        QVERIFY2(
            took < 900, qPrintable(QStringLiteral("two plays held the caller %1 ms").arg(took)));
        // The library's own functions by id look before they change: the look waits little too, and
        // a look that could not be made is said; it is not "no such track".
        timer.restart();
        QVERIFY(!r.library->recordPlay(idA, r.at(12)));
        QCOMPARE(failed.size(), 3);
        QVERIFY(!r.library->recordSkip(idA, r.at(13)));
        QCOMPARE(failed.size(), 4);
        took = timer.elapsed();
        QVERIFY2(took < 900,
            qPrintable(
                QStringLiteral("recordPlay and recordSkip held the caller %1 ms").arg(took)));
        // The same in the real shell: it hands the path over and looks nothing up itself for the
        // count. (How long the end of a track holds the window on such a library is not looked at
        // here: the shell reads the library for other things then, and those reads still wait the
        // long wait. library.md, "Not in this version".)
        r.library->setWriteWait(50ms);
        r.hearToTheEnd(100); // c
        QCOMPARE(r.pending()->kept(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(r.bar(),
            s(u"The library could not be changed: 1 play or skip is not counted yet and will be "
              u"tried again"));
    }
    p.retry();
    QCOMPARE(p.kept(), 0);
    QCOMPARE(r.plays(r.a), 1);
    QCOMPARE(r.skips(r.b), 1);
    r.play->saveState();
    QCOMPARE(r.pending()->kept(), 0);
    QCOMPARE(r.plays(r.c), 1);
}

void PendingPlaysTest::lostAtTheEndIsSaid()
{
    // PS-COUNT: plays still kept when the program ends are lost (they are kept in memory); that is
    // said once, where a user who started it from a terminal sees it. Nothing kept: nothing said.
    static QStringList warnings;
    static QtMessageHandler earlier = nullptr;
    warnings.clear();
    earlier = qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext &context, const QString &text) {
            if (type == QtWarningMsg)
                warnings.append(text);
            else if (earlier)
                earlier(type, context, text);
        });
    {
        Files f;
        {
            PendingPlays quiet(f.library.get());
            quiet.play(f.a, f.at(1));
        }
        QCOMPARE(warnings.size(), 0);
        Holder holder(f);
        PendingPlays p(f.library.get());
        p.play(f.a, f.at(2));
        p.skip(f.b, f.at(3));
        p.play(f.a, f.at(4));
        QCOMPARE(p.kept(), 3);
    }
    qInstallMessageHandler(earlier);
    QCOMPARE(warnings.size(), 1);
    QCOMPARE(warnings.at(0),
        s(u"linuxBee: 3 plays or skips could not be counted before closing, because the library "
          u"could not be changed; they are lost"));
}

void PendingPlaysTest::noMoreThanAThousand()
{
    Files f;
    PendingPlays p(f.library.get());
    QSignalSpy dropped(&p, &PendingPlays::oldestDropped);
    QCOMPARE(PendingPlays::kMost, 1000);
    {
        Holder holder(f);
        // Three of b first, which will be the ones let go; then a thousand and two of a.
        for (int i = 0; i < 3; ++i)
            p.play(f.b, f.at(i));
        for (int i = 0; i < 997; ++i)
            p.play(f.a, f.at(100 + i));
        QCOMPARE(p.kept(), 1000);
        QCOMPARE(dropped.size(), 0);
        for (int i = 0; i < 5; ++i)
            p.play(f.a, f.at(2000 + i));
        QCOMPARE(p.kept(), 1000);
        QCOMPARE(dropped.size(), 1); // said once, though five went
    }
    p.retry();
    QCOMPARE(p.kept(), 0);
    QCOMPARE(f.plays(f.a), 1000); // 997 + 5 kept, less the two oldest of them
    QCOMPARE(f.plays(f.b), 0); // the three oldest of all
    QCOMPARE(f.last(f.a), f.at(2004));
    QCOMPARE(dropped.size(), 1);
    // Nothing is kept now. When it fills again, that is said again: once.
    {
        Holder holder(f);
        for (int i = 0; i < 1003; ++i)
            p.skip(f.c, f.at(i));
        QCOMPARE(p.kept(), 1000);
    }
    QCOMPARE(dropped.size(), 2);
    p.retry();
    QCOMPARE(f.skips(f.c), 1000);
}

void PendingPlaysTest::theShellCountsThroughIt()
{
    // PS-COUNT in the real shell: a track heard to its end while the library is held is counted
    // when it is free, and the status bar says what is true of it.
    Rig r;
    QVERIFY2(r.pending(), "the playback shell has no PendingPlays among its children");
    QCOMPARE(r.play->findChildren<PendingPlays *>().size(), 1);
    r.play->queue()->playNow({ r.a, r.b, r.c }, 0);
    {
        Holder holder(r);
        r.hearToTheEnd(100); // a
        QCOMPARE(r.pending()->kept(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(r.bar(),
            s(u"The library could not be changed: 1 play or skip is not counted yet and will be "
              u"tried again"));
        r.hearToTheEnd(100); // b
        QCOMPARE(r.pending()->kept(), 2);
        QCoreApplication::processEvents();
        QCOMPARE(r.bar(),
            s(u"The library could not be changed: 2 plays or skips are not counted yet and will be "
              u"tried again"));
    }
    QCOMPARE(r.plays(r.a), 0);
    const QDateTime heardA = r.t0.addSecs(100);
    r.hearToTheEnd(100); // c, with the library free: all three go in
    QCOMPARE(r.pending()->kept(), 0);
    QCOMPARE(r.plays(r.a), 1);
    QCOMPARE(r.last(r.a), heardA);
    QCOMPARE(r.plays(r.b), 1);
    QCOMPARE(r.plays(r.c), 1);
    QCoreApplication::processEvents();
    QCOMPARE(r.bar(), s(u"The plays and skips that were waiting are counted"));
}

void PendingPlaysTest::theShellTriesAgainOnTheWayOut()
{
    // PS-STORE: what is kept is offered once more when the program saves its state on the way out.
    Rig r;
    QVERIFY2(r.pending(), "the playback shell has no PendingPlays among its children");
    r.play->queue()->playNow({ r.a, r.b }, 0);
    {
        Holder holder(r);
        r.hearToTheEnd(100);
    }
    QCOMPARE(r.pending()->kept(), 1);
    QCOMPARE(r.plays(r.a), 0);
    r.play->saveState();
    QCOMPARE(r.pending()->kept(), 0);
    QCOMPARE(r.plays(r.a), 1);
}

QTEST_MAIN(PendingPlaysTest)
#include "tst_ui_pendingplays.moc"
