// LOCKED ACCEPTANCE TEST (a change the library refused is said in the status bar). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-REFUSED, SH-MESSAGE). The library is
// a file here, and another holder has it for writing while a change is asked for, so the library
// refuses and says `failed`.
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"
#include "shell/refusalnotice.h"
#include "shell/scanrunner.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QString kSaid = QStringLiteral("library could not be changed");

struct Files {
    QTemporaryDir dir;
    QString a, b, c, d;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<Library> library;

    Files()
        : library(Library::open(dir.path() + QStringLiteral("/library.db")))
    {
        QDir(dir.path()).mkpath(QStringLiteral("music"));
        QDir(dir.path()).mkpath(QStringLiteral("new"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.mp3", "music/b.mp3");
        c = put("basic.ogg", "new/c.ogg");
        d = put("basic.m4a", "new/d.m4a");
        for (const QString &p : { a, b })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        library->setWriteWait(std::chrono::milliseconds(50)); // L-WAIT: not the five seconds
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

// The notice by itself: a library, a status bar, nothing else.
struct Alone : Files {
    QStatusBar bar;
    RefusalNotice notice;
    QSignalSpy failed;
    QSignalSpy said;
    Alone()
        : notice(library.get(), &bar)
        , failed(library.get(), &Library::failed)
        , said(&notice, &RefusalNotice::said)
    {
    }
    // Refuses one play of `a`, and answers with the library's words for it.
    QString refuse()
    {
        const qsizetype before = failed.size();
        Holder holder(*this);
        if (library->recordPlay(id(a), now) || failed.size() != before + 1)
            QTest::qFail("the play was not refused, or not said once", __FILE__, __LINE__);
        return failed.last().at(0).toString();
    }
    void turn() { QCoreApplication::processEvents(); }
};

// The real window and the real library shell.
struct Rig : Files {
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    std::unique_ptr<LibraryShell> shell;
    QStringList chosenFiles, chosenFolders;

    Rig()
        : playlists(made())
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        ShellHooks hooks;
        hooks.now = [this] { return now; };
        hooks.confirm = [](const QString &) { return true; };
        hooks.chooseFiles = [this] { return chosenFiles; };
        hooks.chooseFolders = [this] { return chosenFolders; };
        shell = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, hooks);
    }
    QString made()
    {
        QDir(dir.path()).mkpath(QStringLiteral("playlists"));
        return dir.path() + QStringLiteral("/playlists");
    }
    void run(const char16_t *id)
    {
        if (QAction *action = actions.action(s(id)))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    QString bar() { return window.statusBar()->currentMessage(); }
    RefusalNotice *notice() { return shell->findChild<RefusalNotice *>(); }
};

#define NEED_NOTICE(r)                                                                             \
    do {                                                                                           \
        if (!(r).notice())                                                                         \
            QFAIL("the library shell has no RefusalNotice among its children (SH-REFUSED)");       \
    } while (false)

}

class RefusalNoticeTest : public QObject {
    Q_OBJECT

private slots:
    void aChangeThatIsTakenSaysNothing();
    void aRefusedChangeIsSaidWhenControlReturns();
    void manyInOneTurnAreOneLine();
    void eachTurnIsItsOwn();
    void aLineShownAfterwardsIsKeptAndAddedTo();
    void aLineShownBeforeIsReplaced();
    void aLineThatSaysItAlreadyIsLeft();
    void neverADialog();
    void eitherMayGoFirst();
    void theShellHasOne();
    void addFilesRefused();
    void removeKeepsItsOwnLine();
    void aRefusedScanSaysSo();
    void aScanThatIsTakenSaysTheUsual();
};

void RefusalNoticeTest::aChangeThatIsTakenSaysNothing()
{
    Alone t;
    QVERIFY(t.library->recordPlay(t.id(t.a), t.now));
    // SH-REFUSED: a call with nothing to do is not a refusal either (L-FAILED).
    QVERIFY(!t.library->recordPlay(987654, t.now));
    t.turn();
    QCOMPARE(t.bar.currentMessage(), QString());
    QCOMPARE(t.notice.count(), 0);
    QCOMPARE(t.notice.lastLine(), QString());
    QCOMPARE(t.said.size(), 0);
}

void RefusalNoticeTest::aRefusedChangeIsSaidWhenControlReturns()
{
    Alone t;
    const QString what = t.refuse();
    QVERIFY2(!what.isEmpty(), "the library gave no words for its refusal");
    // SH-REFUSED: not during the call. The command that made it has not said its own line yet.
    QCOMPARE(t.bar.currentMessage(), QString());
    QCOMPARE(t.notice.count(), 0);
    t.turn();
    const QString want = QStringLiteral("The library could not be changed: ") + what;
    QCOMPARE(t.bar.currentMessage(), want);
    QCOMPARE(t.notice.lastLine(), want);
    QCOMPARE(t.notice.count(), 1);
    QCOMPARE(t.said.size(), 1);
    QCOMPARE(t.said.at(0).at(0).toString(), want);
    // Once: more turns say nothing more.
    t.turn();
    t.turn();
    QCOMPARE(t.notice.count(), 1);
    QCOMPARE(t.said.size(), 1);
}

void RefusalNoticeTest::manyInOneTurnAreOneLine()
{
    // SH-REFUSED: a tagging tool that saved forty files and could have none of them read again says
    // one line.
    Alone t;
    t.refuse();
    t.refuse();
    // The words are those of the last one. (Said here in the library's name, to have words that
    // differ.)
    const QString last = QStringLiteral("the last words");
    emit t.library->failed(last);
    t.turn();
    QCOMPARE(t.bar.currentMessage(),
        QStringLiteral("The library could not be changed, 3 times: ") + last);
    QCOMPARE(t.notice.count(), 1);
    QCOMPARE(t.said.size(), 1);
}

void RefusalNoticeTest::eachTurnIsItsOwn()
{
    Alone t;
    t.refuse();
    t.refuse();
    t.turn();
    const QString what = t.refuse();
    t.turn();
    // The count starts again: this is one refusal, not the third.
    QCOMPARE(t.bar.currentMessage(), QStringLiteral("The library could not be changed: ") + what);
    QCOMPARE(t.notice.count(), 2);
    QCOMPARE(t.said.size(), 2);
}

void RefusalNoticeTest::aLineShownAfterwardsIsKeptAndAddedTo()
{
    // SH-REFUSED: the command's own line came after the refusal and is true as far as it goes (the
    // files were saved).
    Alone t;
    const QString what = t.refuse();
    t.bar.showMessage(QStringLiteral("Saved tags of 1 track"));
    t.turn();
    QCOMPARE(t.bar.currentMessage(),
        QStringLiteral("Saved tags of 1 track; the library could not be changed: ") + what);
    QCOMPARE(t.notice.count(), 1);

    t.refuse();
    const QString last = t.refuse();
    t.bar.showMessage(QStringLiteral("Saved tags of 2 tracks"));
    t.turn();
    QCOMPARE(t.bar.currentMessage(),
        QStringLiteral("Saved tags of 2 tracks; the library could not be changed, 2 times: ")
            + last);
}

void RefusalNoticeTest::aLineShownBeforeIsReplaced()
{
    // SH-REFUSED: a line from before the refusal is about something else.
    Alone t;
    t.bar.showMessage(QStringLiteral("Removed 2 from the library"));
    t.turn();
    const QString what = t.refuse();
    t.turn();
    QCOMPARE(t.bar.currentMessage(), QStringLiteral("The library could not be changed: ") + what);

    // And so is one put there and taken away again after it.
    t.refuse();
    t.bar.showMessage(QStringLiteral("Something"));
    t.bar.clearMessage();
    const QString last = t.refuse();
    t.turn();
    QCOMPARE(t.bar.currentMessage(),
        QStringLiteral("The library could not be changed, 2 times: ") + last);
}

void RefusalNoticeTest::aLineThatSaysItAlreadyIsLeft()
{
    // SH-REFUSED: SH-FAILED's commands and Organise word the refusal themselves, and better.
    for (const QString &own :
        { QStringLiteral("The library could not be changed: none of the 2 tracks was removed"),
            QStringLiteral("Organised: 3 done, 0 skipped, 0 failed; the library could not be "
                           "changed and still lists the files where they were"),
            QStringLiteral("THE LIBRARY COULD NOT BE CHANGED") }) {
        Alone t;
        t.refuse();
        t.bar.showMessage(own);
        t.turn();
        QCOMPARE(t.bar.currentMessage(), own);
        QCOMPARE(t.notice.count(), 0);
        QCOMPARE(t.said.size(), 0);
        // What was gathered is done with: the next line is not added to for it.
        t.bar.showMessage(QStringLiteral("Next"));
        t.turn();
        QCOMPARE(t.bar.currentMessage(), QStringLiteral("Next"));
    }
    // Its own line, still there from the turn before, is not a reason to say nothing now.
    Alone t;
    t.refuse();
    t.turn();
    const QString again = t.refuse();
    t.turn();
    QCOMPARE(t.notice.count(), 2);
    QCOMPARE(t.bar.currentMessage(), QStringLiteral("The library could not be changed: ") + again);
}

void RefusalNoticeTest::neverADialog()
{
    // SH-REFUSED: a play that could not be counted does not stop the user's work.
    Alone t;
    bool dialog = false;
    QTimer watch;
    watch.setInterval(5);
    connect(&watch, &QTimer::timeout, &watch, [&dialog] {
        if (QWidget *modal = QApplication::activeModalWidget()) {
            dialog = true;
            modal->close();
        }
    });
    watch.start();
    t.refuse();
    QTest::qWait(60);
    QVERIFY2(!dialog, "a refusal opened a dialog");
    QCOMPARE(t.notice.count(), 1);
    for (QWidget *top : QApplication::topLevelWidgets())
        QVERIFY2(!top->isVisible(), "a refusal showed a window");
}

void RefusalNoticeTest::eitherMayGoFirst()
{
    // The status bar goes while a refusal is gathered: nothing is said, and nothing breaks.
    {
        Files f;
        auto *bar = new QStatusBar;
        RefusalNotice notice(f.library.get(), bar);
        QSignalSpy said(&notice, &RefusalNotice::said);
        {
            Holder holder(f);
            QVERIFY(!f.library->recordPlay(f.id(f.a), f.now));
        }
        delete bar;
        QCoreApplication::processEvents();
        QCOMPARE(said.size(), 0);
        QCOMPARE(notice.count(), 0);
        {
            Holder holder(f);
            QVERIFY(!f.library->recordPlay(f.id(f.a), f.now));
        }
        QCoreApplication::processEvents();
        QCOMPARE(notice.count(), 0);
    }
    // The notice goes while a refusal is gathered.
    {
        Files f;
        QStatusBar bar;
        auto *notice = new RefusalNotice(f.library.get(), &bar);
        {
            Holder holder(f);
            QVERIFY(!f.library->recordPlay(f.id(f.a), f.now));
        }
        delete notice;
        QCoreApplication::processEvents();
        QCOMPARE(bar.currentMessage(), QString());
        Holder holder(f);
        QVERIFY(!f.library->recordPlay(f.id(f.a), f.now));
        QCoreApplication::processEvents();
        QCOMPARE(bar.currentMessage(), QString());
    }
    // The library goes first.
    {
        QStatusBar bar;
        auto f = std::make_unique<Files>();
        RefusalNotice notice(f->library.get(), &bar);
        f.reset();
        QCoreApplication::processEvents();
        QCOMPARE(notice.count(), 0);
    }
}

void RefusalNoticeTest::theShellHasOne()
{
    Rig r;
    NEED_NOTICE(r);
    QCOMPARE(r.shell->findChildren<RefusalNotice *>().size(), 1);
    // A play nobody asked the shell for (the player counted it) is said in the window's status bar.
    QSignalSpy failed(r.library.get(), &Library::failed);
    {
        Holder holder(r);
        QVERIFY(!r.library->recordPlay(r.id(r.a), r.now));
    }
    QCOMPARE(failed.size(), 1);
    QCoreApplication::processEvents();
    QCOMPARE(r.bar(),
        QStringLiteral("The library could not be changed: ") + failed.at(0).at(0).toString());
    QCOMPARE(r.shell->lastMessage(), QString()); // SH-MESSAGE: the shell's own last line is its own
}

void RefusalNoticeTest::addFilesRefused()
{
    Rig r;
    NEED_NOTICE(r);
    QVERIFY2(r.actions.isImplemented(s(u"file.addFiles")), "Add Files is not built");
    r.chosenFiles = { r.c, r.d };
    QSignalSpy failed(r.library.get(), &Library::failed);
    {
        Holder holder(r);
        r.run(u"file.addFiles");
    }
    QCOMPARE(failed.size(), 2);
    QCOMPARE(r.library->count(), 2);
    QCOMPARE(r.shell->lastMessage(), s(u"Added 0 of 2 files"));
    QCoreApplication::processEvents();
    QCOMPARE(r.bar(),
        s(u"Added 0 of 2 files; the library could not be changed, 2 times: ")
            + failed.at(1).at(0).toString());
    // And the same command when the library takes it says the usual, and no more.
    r.run(u"file.addFiles");
    QCoreApplication::processEvents();
    QCOMPARE(r.library->count(), 4);
    QCOMPARE(r.bar(), s(u"Added 2 of 2 files"));
    QCOMPARE(r.notice()->count(), 1);
}

void RefusalNoticeTest::removeKeepsItsOwnLine()
{
    // SH-FAILED's line is the better one, and stays.
    Rig r;
    NEED_NOTICE(r);
    QVERIFY2(r.shell->trackList(), "the shell has no track list");
    r.shell->trackList()->selectTracks({ r.id(r.a), r.id(r.b) });
    {
        Holder holder(r);
        r.run(u"file.remove");
    }
    const QString own = s(u"The library could not be changed: none of the 2 tracks was removed");
    QCOMPARE(r.shell->lastMessage(), own);
    QCoreApplication::processEvents();
    QCOMPARE(r.bar(), own);
    QCOMPARE(r.notice()->count(), 0);
    QCOMPARE(r.library->count(), 2);
}

void RefusalNoticeTest::aRefusedScanSaysSo()
{
    // SH-MESSAGE: a scan the library could not store does not end with a line that reads as if it
    // had.
    Rig r;
    NEED_NOTICE(r);
    QVERIFY2(r.shell->scanner(), "the shell has no scan runner");
    r.chosenFolders = { r.dir.path() + s(u"/new") };
    QSignalSpy failed(r.library.get(), &Library::failed);
    {
        Holder holder(r);
        r.run(u"file.scanFolders");
        QTRY_VERIFY(!r.shell->scanner()->isRunning());
        QCoreApplication::processEvents();
    }
    QVERIFY2(failed.size() >= 1, "the scan was not refused: the test did not hold the library");
    QCOMPARE(r.library->count(), 2);
    const QString line = r.shell->lastMessage();
    const QString tail = s(u"; the library could not be changed, and what was not stored will be "
                           u"found by the next scan");
    QVERIFY2(line.startsWith(s(u"Scan: ")), qPrintable(line));
    QVERIFY2(line.endsWith(tail), qPrintable(line));
    QCOMPARE(line.count(kSaid, Qt::CaseInsensitive), 1);
    QCoreApplication::processEvents();
    QCOMPARE(r.bar(), line); // the notice adds nothing to a line that says it

    // The next scan, with the library free, finds them, and says the usual.
    r.run(u"file.scanFolders");
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCoreApplication::processEvents();
    QCOMPARE(r.library->count(), 4);
    QCOMPARE(r.shell->lastMessage(), s(u"Scan: 2 added, 0 updated, 0 missing, 0 not audio"));
    QCOMPARE(r.bar(), r.shell->lastMessage());
}

void RefusalNoticeTest::aScanThatIsTakenSaysTheUsual()
{
    // A refusal from before the scan began is not the scan's.
    Rig r;
    NEED_NOTICE(r);
    {
        Holder holder(r);
        QVERIFY(!r.library->recordPlay(r.id(r.a), r.now));
    }
    QCoreApplication::processEvents();
    r.chosenFolders = { r.dir.path() + s(u"/new") };
    r.run(u"file.scanFolders");
    QVERIFY2(r.shell->scanner(), "the shell has no scan runner");
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCoreApplication::processEvents();
    QCOMPARE(r.shell->lastMessage(), s(u"Scan: 2 added, 0 updated, 0 missing, 0 not audio"));
    QCOMPARE(r.bar(), r.shell->lastMessage());
}

QTEST_MAIN(RefusalNoticeTest)
#include "tst_ui_refusalnotice.moc"
