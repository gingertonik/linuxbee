// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/scan-runner.md (SR-x). The looking
// hook lets a test hold the looking thread at a known file, so nothing here
// depends on how fast the machine is.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>
#include <QTimer>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <thread>

#include "library/library.h"
#include "shell/scanrunner.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

// A music folder in a temporary place; six files with a music extension, in this order:
//   a/one.mp3  a/two.flac  b/four.ogg  b/notaudio.mp3  b/three.m4a  c/five.opus
struct Folder {
    QTemporaryDir dir;
    QString root;
    Folder()
    {
        root = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()) + QStringLiteral("/music");
        for (const char16_t *sub : { u"/a", u"/b", u"/c" })
            QDir().mkpath(root + s(sub));
        put(u"basic.mp3", u"a/one.mp3");
        put(u"basic.flac", u"a/two.flac");
        put(u"basic.ogg", u"b/four.ogg");
        put(u"notaudio.mp3", u"b/notaudio.mp3");
        put(u"basic.m4a", u"b/three.m4a");
        put(u"basic.opus", u"c/five.opus");
    }
    void put(const char16_t *fixtureName, const char16_t *relative)
    {
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + s(fixtureName), root + QLatin1Char('/') + s(relative)))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    }
};

ScanOptions options()
{
    ScanOptions o;
    o.category = QStringLiteral("Music");
    o.now = kNow;
    return o;
}

// Holds the looking thread before the file numbered `at` until let go.
struct Gate {
    std::atomic<int> at { -1 }; // -1: never holds
    QSemaphore reached;
    QSemaphore go;
    std::atomic<int> calls { 0 };
    std::atomic<bool> onAnotherThread { true };
    QThread *home = QThread::currentThread();
    ScanRunnerHooks hooks()
    {
        ScanRunnerHooks h;
        h.looking = [this](int done, int) {
            ++calls;
            if (QThread::currentThread() == home)
                onAnotherThread = false;
            if (done == at) {
                reached.release();
                // Held until let go. Bounded, so that a test that has failed ends
                // (the runner's destructor waits for this thread) and does not hang.
                go.tryAcquire(1, 10000);
            }
        };
        return h;
    }
};

ScanReport reportOf(const QSignalSpy &finished, int index)
{
    return finished.at(index).at(0).value<ScanReport>();
}

}

class ScanRunnerTest : public QObject {
    Q_OBJECT
private slots:
    void aScan();
    void theLoopTurnsWhileItLooks();
    void tracksArriveInParts();
    void oneAfterAnother();
    void stopped();
    void refused();
    void destroyedWhileLooking();
};

void ScanRunnerTest::aScan()
{
    // SR-THREADS, SR-SCAN: the looking on another thread, the signals on this one, the report whole.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    Gate gate;
    ScanRunner runner(library.get(), gate.hooks());
    QCOMPARE(runner.partSize(), 200);
    QVERIFY(!runner.isRunning());
    QSignalSpy started(&runner, &ScanRunner::started);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QSignalSpy idle(&runner, &ScanRunner::idle);
    QList<int> dones;
    bool signalsHere = true;
    connect(&runner, &ScanRunner::progress, this, [&](int done, int total) {
        dones.append(done);
        if (total != 6 || QThread::currentThread() != gate.home)
            signalsHere = false;
    });
    connect(&runner, &ScanRunner::finished, this, [&] {
        if (QThread::currentThread() != gate.home)
            signalsHere = false;
    });

    runner.scan({ f.root }, options());
    QVERIFY(runner.isRunning()); // asked for is running, whether or not it has begun
    QCOMPARE(library->count(), 0); // and the call came back before anything was stored
    QTRY_COMPARE(finished.count(), 1);
    QTRY_COMPARE(idle.count(), 1);
    QVERIFY(!runner.isRunning());
    QCOMPARE(started.count(), 1);
    QCOMPARE(started.at(0).at(0).toStringList(), QStringList { f.root });
    QCOMPARE(finished.at(0).at(1).toStringList(), QStringList { f.root });
    const ScanReport report = reportOf(finished, 0);
    QCOMPARE(report.added.size(), 5);
    QCOMPARE(report.notAudio.size(), 1);
    QVERIFY(!report.cancelled);
    QCOMPARE(library->count(), 5);
    QCOMPARE(gate.calls.load(), 6);
    QVERIFY(gate.onAnotherThread.load());
    QVERIFY(signalsHere);
    QVERIFY(!dones.isEmpty());
    QCOMPARE(dones.last(), 6);
    QVERIFY(std::is_sorted(dones.cbegin(), dones.cend()));

    // Again: nothing changed, and the report says so.
    runner.scan({ f.root }, options());
    QTRY_COMPARE(finished.count(), 2);
    QCOMPARE(reportOf(finished, 1).unchanged, 5);
    QCOMPARE(reportOf(finished, 1).added.size(), 0);
    QTRY_COMPARE(idle.count(), 2);
}

void ScanRunnerTest::theLoopTurnsWhileItLooks()
{
    // SR-THREADS: while the looking thread is held, this thread goes on turning its loop.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    Gate gate;
    gate.at = 3;
    ScanRunner runner(library.get(), gate.hooks());
    QSignalSpy finished(&runner, &ScanRunner::finished);
    runner.scan({ f.root }, options());
    QVERIFY(gate.reached.tryAcquire(1, 10000)); // the looking is held before the fourth file
    int turns = 0;
    QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&turns] { ++turns; });
    timer.start(1);
    QTRY_VERIFY(turns >= 5);
    QCOMPARE(finished.count(), 0);
    QVERIFY(runner.isRunning());
    gate.go.release();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(library->count(), 5);
}

void ScanRunnerTest::tracksArriveInParts()
{
    // SR-SCAN: what was found is stored as it is handed over, part by part.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    Gate gate;
    gate.at = 4;
    ScanRunner runner(library.get(), gate.hooks());
    runner.setPartSize(2);
    QCOMPARE(runner.partSize(), 2);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QSignalSpy added(library.get(), &Library::tracksAdded);
    runner.scan({ f.root }, options());
    QVERIFY(gate.reached.tryAcquire(1, 10000)); // four files looked at: two parts handed over
    QTRY_COMPARE(library->count(), 3); // one.mp3, two.flac, four.ogg; notaudio.mp3 is not stored
    QCOMPARE(added.count(), 2);
    QCOMPARE(finished.count(), 0);
    gate.go.release();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(library->count(), 5);
    QCOMPARE(added.count(), 3);
    const ScanReport report = reportOf(finished, 0);
    QCOMPARE(report.added.size(), 5); // the report is of the whole scan
    QCOMPARE(report.notAudio.size(), 1);
    // A size below 1 counts as 1.
    runner.setPartSize(0);
    QCOMPARE(runner.partSize(), 1);
}

void ScanRunnerTest::oneAfterAnother()
{
    // SR-SCAN: scans asked for while one runs wait their turn.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    Gate gate;
    gate.at = 1;
    ScanRunner runner(library.get(), gate.hooks());
    QSignalSpy started(&runner, &ScanRunner::started);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QSignalSpy idle(&runner, &ScanRunner::idle);
    const QStringList a { f.root + s(u"/a") };
    const QStringList b { f.root + s(u"/b") };
    const QStringList c { f.root + s(u"/c") };
    runner.scan(a, options());
    QVERIFY(gate.reached.tryAcquire(1, 10000));
    runner.scan(b, options());
    runner.scan(c, options());
    QCOMPARE(runner.waiting(), 2);
    QCOMPARE(started.count(), 1);
    gate.at = -1; // the later scans are not held
    gate.go.release();
    QTRY_COMPARE(finished.count(), 3);
    QTRY_COMPARE(idle.count(), 1);
    QCOMPARE(started.count(), 3);
    QCOMPARE(finished.at(0).at(1).toStringList(), a);
    QCOMPARE(finished.at(1).at(1).toStringList(), b);
    QCOMPARE(finished.at(2).at(1).toStringList(), c);
    QCOMPARE(reportOf(finished, 0).added.size(), 2);
    QCOMPARE(reportOf(finished, 1).added.size(), 2);
    QCOMPARE(reportOf(finished, 2).added.size(), 1);
    QCOMPARE(runner.waiting(), 0);
    QVERIFY(!runner.isRunning());
}

void ScanRunnerTest::stopped()
{
    // SR-STOP: what was handed over stays; the waiting ones are forgotten.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    Gate gate;
    gate.at = 2;
    ScanRunner runner(library.get(), gate.hooks());
    runner.setPartSize(1);
    QSignalSpy started(&runner, &ScanRunner::started);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QSignalSpy idle(&runner, &ScanRunner::idle);
    runner.cancel(); // nothing runs: nothing happens
    QCoreApplication::processEvents();
    QCOMPARE(idle.count(), 0);

    runner.scan({ f.root }, options());
    runner.scan({ f.root + s(u"/c") }, options());
    QVERIFY(gate.reached.tryAcquire(1, 10000)); // two files looked at, each its own part
    QTRY_COMPARE(library->count(), 2);
    runner.cancel();
    QCOMPARE(runner.waiting(), 0);
    gate.go.release();
    QTRY_COMPARE(finished.count(), 1);
    QTRY_COMPARE(idle.count(), 1);
    QVERIFY(reportOf(finished, 0).cancelled);
    QCOMPARE(reportOf(finished, 0).added.size(), 2);
    QCOMPARE(reportOf(finished, 0).nowMissing.size(), 0);
    QCOMPARE(library->count(), 2);
    QVERIFY(!runner.isRunning());
    QCoreApplication::processEvents();
    QCOMPARE(started.count(), 1); // the second was never begun
    QCOMPARE(finished.count(), 1);

    // It can be used again.
    gate.at = -1;
    runner.scan({ f.root }, options());
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY(!reportOf(finished, 1).cancelled);
    QCOMPARE(library->count(), 5);
}

void ScanRunnerTest::refused()
{
    // SR-SCAN: a scan the library would refuse looks at nothing.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    Gate gate;
    ScanRunner runner(library.get(), gate.hooks());
    QSignalSpy started(&runner, &ScanRunner::started);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    ScanOptions bad = options();
    bad.now = QDateTime();
    runner.scan({ f.root }, bad);
    ScanOptions noSection = options();
    noSection.category = s(u"No Such Section");
    runner.scan({ f.root }, noSection);
    QTRY_COMPARE(finished.count(), 2);
    QCOMPARE(started.count(), 2);
    QCOMPARE(reportOf(finished, 0).added.size(), 0);
    QVERIFY(!reportOf(finished, 0).cancelled);
    QCOMPARE(gate.calls.load(), 0);
    QCOMPARE(library->count(), 0);
}

void ScanRunnerTest::destroyedWhileLooking()
{
    // SR-STOP: destroying the runner ends the looking; nothing is stored or signalled after.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    Gate gate;
    gate.at = 2;
    int finishedSeen = 0;
    {
        auto runner = std::make_unique<ScanRunner>(library.get(), gate.hooks());
        runner->setPartSize(1);
        connect(runner.get(), &ScanRunner::finished, this, [&finishedSeen] { ++finishedSeen; });
        runner->scan({ f.root }, options());
        runner->scan({ f.root }, options());
        QVERIFY(gate.reached.tryAcquire(1, 10000));
        QTRY_COMPARE(library->count(), 2);
        // The looking thread stays held inside the hook while the runner is destroyed,
        // and is let go from a third thread a little later: the destructor must still
        // be waiting then.
        std::atomic<bool> letGo { false };
        std::thread later([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            letGo = true;
            gate.go.release();
        });
        runner.reset();
        const bool waited = letGo.load();
        later.join();
        QVERIFY2(waited, "the destructor came back while the looking thread was still at work");
    }
    // SR-THREADS: the stop took effect when the hook returned; the third file was not looked at.
    QCOMPARE(gate.calls.load(), 3);
    const int kept = library->count();
    QCOMPARE(kept, 2);
    QCoreApplication::processEvents();
    QTest::qWait(50);
    QCOMPARE(finishedSeen, 0);
    QCOMPARE(library->count(), kept);
}

QTEST_MAIN(ScanRunnerTest)
#include "tst_ui_scanrunner.moc"
