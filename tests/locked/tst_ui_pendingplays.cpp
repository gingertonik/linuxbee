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

    Files()
        : library(Library::open(dir.path() + QStringLiteral("/library.db")))
    {
        QDir(dir.path()).mkpath(QStringLiteral("music"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.m4a", "music/b.m4a");
        c = put("basic.ogg", "music/c.ogg");
        for (const QString &p : { a, b, c })
            if (library->addFile(p, QStringLiteral("Music"), t0) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        library->setWriteWait(50ms);
        library->setQuickWait(1ms); // not the quarter second: these tests refuse thousands of times
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

    Rig()
        : playlists(made())
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
    p.play(f.id(f.a), f.at(10));
    p.skip(f.id(f.b), f.at(20));
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
        p.play(f.id(f.a), f.at(10));
    }
    QCOMPARE(p.kept(), 1);
    QCOMPARE(kept.size(), 1);
    QCOMPARE(kept.at(0).at(0).toInt(), 1);
    QCOMPARE(f.plays(f.a), 0);
    // The library is free again. Nothing happens by itself; the next play brings the kept one in
    // before it.
    QCOMPARE(f.plays(f.a), 0);
    p.play(f.id(f.b), f.at(300));
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
        p.play(f.id(f.a), f.at(10));
        p.play(f.id(f.a), f.at(250));
        p.play(f.id(f.a), f.at(500));
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
        p.play(f.id(f.a), f.at(500));
        p.skip(f.id(f.b), f.at(20));
        p.play(f.id(f.a), f.at(30));
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
    p.play(f.id(f.a), f.at(1));
    QCOMPARE(failed.size(), 1);
    p.play(f.id(f.b), f.at(2));
    QCOMPARE(failed.size(), 2);
    p.skip(f.id(f.c), f.at(3));
    QCOMPARE(failed.size(), 3);
    p.retry();
    QCOMPARE(failed.size(), 4);
    QCOMPARE(p.kept(), 3);
}

void PendingPlaysTest::nothingToDoIsNotKept()
{
    // A track the library does not have is not a refusal (L-FAILED): it is not kept, held or free.
    Files f;
    PendingPlays p(f.library.get());
    p.play(987654, f.at(1));
    QCOMPARE(p.kept(), 0);
    {
        Holder holder(f);
        p.play(987654, f.at(2));
        p.skip(987655, f.at(3));
        QCOMPARE(p.kept(), 0);
        p.play(f.id(f.a), f.at(4));
        QCOMPARE(p.kept(), 1);
    }
    // A track that was removed while its play was kept: let go when it is offered, and the rest go
    // on.
    {
        Holder holder(f);
        p.play(f.id(f.b), f.at(5));
    }
    QCOMPARE(p.kept(), 2);
    QCOMPARE(f.library->remove({ f.id(f.a) }), 1);
    p.retry();
    QCOMPARE(p.kept(), 0);
    QCOMPARE(f.plays(f.b), 1);
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
            p.play(f.id(f.b), f.at(i));
        for (int i = 0; i < 997; ++i)
            p.play(f.id(f.a), f.at(100 + i));
        QCOMPARE(p.kept(), 1000);
        QCOMPARE(dropped.size(), 0);
        for (int i = 0; i < 5; ++i)
            p.play(f.id(f.a), f.at(2000 + i));
        QCOMPARE(p.kept(), 1000);
        QCOMPARE(dropped.size(), 1); // said once, though five went
    }
    p.retry();
    QCOMPARE(p.kept(), 0);
    QCOMPARE(f.plays(f.a), 1000); // 997 + 5 kept, less the two oldest of them
    QCOMPARE(f.plays(f.b), 0); // the three oldest of all
    QCOMPARE(f.last(f.a), f.at(2004));
    QCOMPARE(dropped.size(), 1);
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
