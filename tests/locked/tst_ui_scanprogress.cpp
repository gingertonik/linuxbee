// LOCKED ACCEPTANCE TEST (the scan's row in the status bar, and stopping a scan). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/scan-runner.md (SP-x). Runs on the
// offscreen platform. Most of it drives the row by emitting a runner's
// signals; the last two functions run real scans, held by the runner's hook.
#include <QAction>
#include <QLabel>
#include <QProgressBar>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "shell/scanprogress.h"
#include "shell/scanrunner.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QString kQuestion = QStringLiteral("Stop the scan? The files stored so far stay in the library.");

struct Rig {
    QTemporaryDir dir;
    std::unique_ptr<Library> library;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    std::unique_ptr<ScanProgress> progress;
    bool yes = true;
    QStringList asked;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        ScanProgress::Hooks hooks;
        hooks.confirm = [this](const QString &question) {
            asked.append(question);
            return yes;
        };
        progress = std::make_unique<ScanProgress>(&window, &actions, hooks);
    }
    QWidget *row() const { return window.statusBar()->findChild<QWidget *>(s(u"scanProgressRow")); }
    QProgressBar *bar() const { return window.statusBar()->findChild<QProgressBar *>(s(u"scanProgress")); }
    QLabel *text() const { return window.statusBar()->findChild<QLabel *>(s(u"scanProgressText")); }
    QToolButton *button() const { return window.statusBar()->findChild<QToolButton *>(s(u"scanStopButton")); }
    QAction *command() const { return actions.action(s(u"file.scanStop")); }
    bool rowHidden() const { return row() && row()->isHidden(); }
    // Three files to scan, in a folder of their own; returns the folder.
    QString music()
    {
        const QString folder = dir.path() + QStringLiteral("/music");
        QDir().mkpath(folder);
        int n = 0;
        for (const char *name : { "basic.flac", "basic.m4a", "basic.ogg" }) {
            if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(name),
                    folder + QStringLiteral("/%1-").arg(++n) + QLatin1String(name)))
                QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        }
        return folder;
    }
    ScanOptions options() const
    {
        ScanOptions o;
        o.category = QStringLiteral("Music");
        o.now = now;
        return o;
    }
};

// A runner's hook that holds the looking until the test lets it go on.
struct Gate {
    QSemaphore go;
    ScanRunnerHooks hooks()
    {
        ScanRunnerHooks h;
        h.looking = [this](int, int) {
            if (!go.tryAcquire(1, 10000))
                qWarning("the test never let this scan go on");
        };
        return h;
    }
};

void settle()
{
    for (int i = 0; i < 3; ++i)
        QCoreApplication::processEvents();
}

#define NEED_ROW(r) \
    do { \
        if (!(r).row() || !(r).bar() || !(r).text() || !(r).button() || !(r).command()) \
            QFAIL("the scan's row has not been built"); \
    } while (false)

}

class ScanProgressTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void followsARunner();
    void onceATurn();
    void stopping();
    void twoRunners();
    void aRealScanStopped();
    void aRealScanToItsEnd();
};

void ScanProgressTest::structure()
{
    // SP-ROW: the row is in the status bar and hidden; the command is there and off.
    Rig r;
    NEED_ROW(r);
    r.window.show();
    QVERIFY(r.rowHidden());
    QVERIFY(!r.progress->isShown());
    QVERIFY(r.actions.isImplemented(s(u"file.scanStop")));
    QVERIFY(!r.command()->isEnabled());
    QCOMPARE(r.button()->toolTip(), s(u"Stop the scan"));
    QCOMPARE(r.progress->updates(), 0);
    // Stopping when nothing is scanned asks nothing.
    QVERIFY(!r.progress->stop());
    r.command()->trigger();
    QCOMPARE(r.asked, QStringList());
    // Destroying the object frees the command and takes the row away.
    r.progress.reset();
    QVERIFY(!r.actions.isImplemented(s(u"file.scanStop")));
    QVERIFY(!r.row());
}

void ScanProgressTest::followsARunner()
{
    // SP-ROW, SP-VALUE: shown from `started` to `idle`, with what `progress` said last.
    Rig r;
    NEED_ROW(r);
    r.window.show();
    ScanRunner runner(r.library.get());
    r.progress->watch(&runner);
    r.progress->watch(&runner); // twice is once
    QVERIFY(r.rowHidden());

    emit runner.started({ s(u"/music") });
    QVERIFY(r.progress->isShown()); // at once
    settle();
    QVERIFY(r.row()->isVisible());
    QVERIFY(r.command()->isEnabled());
    QVERIFY(r.button()->isEnabled());
    QCOMPARE(r.text()->text(), s(u"Looking for files\u2026"));
    QCOMPARE(r.bar()->maximum(), 0); // busy: no total yet

    emit runner.progress(3, 120);
    settle();
    QCOMPARE(r.bar()->maximum(), 120);
    QCOMPARE(r.bar()->value(), 3);
    QCOMPARE(r.text()->text(), s(u"3 of 120 files"));
    emit runner.progress(1, 1);
    settle();
    QCOMPARE(r.text()->text(), s(u"1 of 1 file"));
    emit runner.progress(1500, 17000);
    settle();
    QCOMPARE(r.text()->text(), s(u"1,500 of 17,000 files"));
    QCOMPARE(r.bar()->value(), 1500);

    // One scan ends and the next of the same runner begins: the row stays, and starts over.
    emit runner.finished(ScanReport { }, { s(u"/music") });
    QVERIFY(r.progress->isShown());
    emit runner.started({ s(u"/more") });
    settle();
    QCOMPARE(r.text()->text(), s(u"Looking for files\u2026"));
    QCOMPARE(r.bar()->maximum(), 0);
    emit runner.progress(0, 0); // nothing to look at
    settle();
    QCOMPARE(r.text()->text(), s(u"0 of 0 files"));
    emit runner.finished(ScanReport { }, { s(u"/more") });
    emit runner.idle();
    QVERIFY(!r.progress->isShown());
    settle();
    QVERIFY(r.rowHidden());
    QVERIFY(!r.command()->isEnabled());
}

void ScanProgressTest::onceATurn()
{
    // SP-ONCE: however many files are reported in one turn of the event loop, the row is written once.
    Rig r;
    NEED_ROW(r);
    r.window.show();
    ScanRunner runner(r.library.get());
    r.progress->watch(&runner);
    emit runner.started({ s(u"/music") });
    settle();
    const int before = r.progress->updates();
    QVERIFY(before >= 1);
    for (int i = 1; i <= 1000; ++i)
        emit runner.progress(i, 1000);
    QCOMPARE(r.progress->updates(), before); // not inside the signal
    settle();
    QCOMPARE(r.progress->updates(), before + 1);
    QCOMPARE(r.bar()->value(), 1000);
    QCOMPARE(r.text()->text(), s(u"1,000 of 1,000 files"));
    // Nothing new: nothing written.
    settle();
    QCOMPARE(r.progress->updates(), before + 1);
    // The same values again: nothing written either.
    emit runner.progress(1000, 1000);
    settle();
    QCOMPARE(r.progress->updates(), before + 1);
    emit runner.idle();
}

void ScanProgressTest::stopping()
{
    // SP-STOP: the button and the command ask first; yes tells the runner once; then the row waits for the end.
    Rig r;
    NEED_ROW(r);
    r.window.show();
    Gate gate;
    ScanRunner runner(r.library.get(), gate.hooks());
    r.progress->watch(&runner);
    const QString folder = r.music();
    QSignalSpy finished(&runner, &ScanRunner::finished);
    runner.scan({ folder }, r.options());
    QVERIFY(runner.isRunning());
    QVERIFY(r.progress->isShown());
    settle();

    // No: nothing happens.
    r.yes = false;
    r.button()->click();
    QCOMPARE(r.asked, QStringList({ kQuestion }));
    QVERIFY(r.button()->isEnabled());
    QVERIFY(!r.progress->stop());
    QCOMPARE(r.asked.size(), 2);
    QVERIFY(runner.isRunning());

    // Yes, by the command.
    r.yes = true;
    r.command()->trigger();
    QCOMPARE(r.asked.size(), 3);
    settle();
    QCOMPARE(r.text()->text(), s(u"Stopping\u2026"));
    QVERIFY(!r.button()->isEnabled());
    QVERIFY(!r.command()->isEnabled());
    QVERIFY(r.progress->isShown()); // until the scan has ended
    // Asked again while stopping: not asked, not stopped twice.
    QVERIFY(!r.progress->stop());
    QCOMPARE(r.asked.size(), 3);
    // A file reported late does not bring the numbers back.
    emit runner.progress(2, 3);
    settle();
    QCOMPARE(r.text()->text(), s(u"Stopping\u2026"));

    gate.go.release(100);
    QTRY_VERIFY_WITH_TIMEOUT(!runner.isRunning(), 5000);
    QCOMPARE(finished.count(), 1);
    QVERIFY(finished.at(0).at(0).value<ScanReport>().cancelled);
    QVERIFY(!r.progress->isShown());
    settle();
    QVERIFY(r.rowHidden());
    // The next scan starts fresh.
    emit runner.started({ folder });
    settle();
    QVERIFY(r.button()->isEnabled());
    QCOMPARE(r.text()->text(), s(u"Looking for files\u2026"));
    emit runner.idle();
}

void ScanProgressTest::twoRunners()
{
    // SP-WATCH: the row is shown while any watched runner scans; it shows the one that began first; stopping stops all.
    Rig r;
    NEED_ROW(r);
    r.window.show();
    Gate gateA, gateB;
    auto first = std::make_unique<ScanRunner>(r.library.get(), gateA.hooks());
    ScanRunner second(r.library.get(), gateB.hooks());
    ScanRunner idleOne(r.library.get());
    r.progress->watch(first.get());
    r.progress->watch(&second);
    r.progress->watch(&idleOne);

    emit first->started({ s(u"/a") });
    emit second.started({ s(u"/b") });
    emit second.progress(7, 9);
    emit first->progress(2, 50);
    settle();
    QCOMPARE(r.text()->text(), s(u"2 of 50 files")); // the one that began first
    emit second.progress(8, 9);
    settle();
    QCOMPARE(r.text()->text(), s(u"2 of 50 files"));
    emit first->idle();
    QVERIFY(r.progress->isShown()); // the other still scans
    settle();
    QCOMPARE(r.text()->text(), s(u"8 of 9 files")); // with what it said last
    QCOMPARE(r.bar()->maximum(), 9);

    // A runner that is destroyed while it is the one shown is forgotten.
    emit first->started({ s(u"/a") });
    emit second.idle();
    settle();
    QVERIFY(r.progress->isShown());
    first.reset();
    QVERIFY(!r.progress->isShown());
    settle();
    QVERIFY(r.rowHidden());

    // Two real scans at once: one question, both stopped.
    const QString folder = r.music();
    ScanRunner third(r.library.get(), gateA.hooks());
    r.progress->watch(&third);
    QSignalSpy thirdDone(&third, &ScanRunner::finished);
    QSignalSpy secondDone(&second, &ScanRunner::finished);
    third.scan({ folder }, r.options());
    second.scan({ folder }, r.options());
    settle();
    QVERIFY(r.progress->stop());
    QCOMPARE(r.asked.size(), 1);
    gateA.go.release(100);
    gateB.go.release(100);
    QTRY_VERIFY_WITH_TIMEOUT(!third.isRunning() && !second.isRunning(), 5000);
    QVERIFY(thirdDone.at(0).at(0).value<ScanReport>().cancelled);
    QVERIFY(secondDone.at(0).at(0).value<ScanReport>().cancelled);
    QVERIFY(!r.progress->isShown());
}

void ScanProgressTest::aRealScanStopped()
{
    // SP-WATCH: a runner that is scanning already when it is given is shown at once; what was stored stays.
    Rig r;
    NEED_ROW(r);
    r.window.show();
    Gate gate;
    ScanRunner runner(r.library.get(), gate.hooks());
    runner.setPartSize(1);
    const QString folder = r.music();
    runner.scan({ folder }, r.options());
    r.progress->watch(&runner);
    QVERIFY(r.progress->isShown());
    gate.go.release(1); // one file is looked at; the second is held
    QTRY_COMPARE_WITH_TIMEOUT(r.library->count(), 1, 5000); // the first part is stored
    settle();
    QVERIFY(r.text()->text().endsWith(s(u" of 3 files")));
    QVERIFY(r.progress->stop());
    gate.go.release(100);
    QTRY_VERIFY_WITH_TIMEOUT(!runner.isRunning(), 5000);
    QVERIFY(r.library->count() >= 1 && r.library->count() < 3);
    QVERIFY(!r.progress->isShown());
}

void ScanProgressTest::aRealScanToItsEnd()
{
    // SP-ROW: a scan that is not stopped: the row comes, counts to the end, and goes.
    Rig r;
    NEED_ROW(r);
    r.window.show();
    ScanRunner runner(r.library.get());
    r.progress->watch(&runner);
    QStringList texts;
    QObject::connect(&runner, &ScanRunner::finished, &runner, [&] {
        settle();
        texts.append(r.text()->text());
    });
    runner.scan({ r.music() }, r.options());
    QVERIFY(r.progress->isShown());
    QTRY_VERIFY_WITH_TIMEOUT(!runner.isRunning(), 5000);
    QCOMPARE(r.library->count(), 3);
    QCOMPARE(texts, QStringList({ s(u"3 of 3 files") }));
    QVERIFY(!r.progress->isShown());
    QCOMPARE(r.asked, QStringList());
    QVERIFY(r.progress->updates() < 20); // a handful, not one for each signal
}

QTEST_MAIN(ScanProgressTest)
#include "tst_ui_scanprogress.moc"
