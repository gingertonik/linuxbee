// LOCKED ACCEPTANCE TEST (a scan the library refuses stops there: it does not go on asking, part
// after part). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason, and
// left alone.
//
// Expected values are rules in docs/spec/scan-runner.md (SR-REFUSED) and docs/spec/library.md
// (L-WAIT, "nobody is waiting"). The library is a file; another holder has it for writing, so the
// library refuses. What is counted is how often the library was asked and refused, never time, but
// for the one case about the wait.
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>
#include <QtTest>

#include <atomic>
#include <chrono>

#include "library/library.h"
#include "shell/scanrunner.h"

using namespace linuxbee;
using namespace linuxbee::ui;
using namespace std::chrono_literals;

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
        root
            = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()) + QStringLiteral("/music");
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
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + s(fixtureName),
                root + QLatin1Char('/') + s(relative)))
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

// A library in a file, which another connection can hold.
struct OnDisk {
    QTemporaryDir dir;
    std::unique_ptr<Library> library;
    OnDisk()
        : library(Library::open(dir.path() + QStringLiteral("/library.db")))
    {
        library->setWriteWait(30ms);
        library->setQuickWait(30ms);
    }
};

// Holds the library's database for writing for as long as it lives.
struct Holder {
    QString name;
    explicit Holder(const OnDisk &f)
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

ScanReport reportOf(const QSignalSpy &finished, int at)
{
    return finished.at(at).at(0).value<ScanReport>();
}

}

class ScanRunnerRefusedTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<linuxbee::ScanReport>(); }
    void askedOnceAndStopped();
    void whatWasStoredStays();
    void theNextScanStillRuns();
    void aStopAskedForIsStillAStop();
    void nobodyWaits();
};

void ScanRunnerRefusedTest::askedOnceAndStopped()
{
    // SR-REFUSED: six files, a part for each. The library is held, so the first part is refused.
    // The library is asked no more for this scan, the looking is told to stop, and the scan ends as
    // refused.
    Folder folder;
    OnDisk disk;
    Gate gate;
    gate.at = 3; // the looking waits before the fourth file
    ScanRunner runner(disk.library.get(), gate.hooks());
    runner.setPartSize(1);
    QSignalSpy failed(disk.library.get(), &Library::failed);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QSignalSpy idle(&runner, &ScanRunner::idle);
    Holder holder(disk);
    runner.scan({ folder.root }, options());
    QVERIFY(gate.reached.tryAcquire(1, 10000));
    // Three parts have been handed over. The first is refused; then the looking is let go.
    QTRY_VERIFY_WITH_TIMEOUT(failed.size() >= 1, 10000);
    gate.go.release();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QTest::qWait(150); // anything still on its way arrives
    QCOMPARE(failed.size(), 1); // asked once, refused once: not once for each part
    const ScanReport report = reportOf(finished, 0);
    QVERIFY2(!report.refused.isEmpty(), "the scan did not end as refused");
    QCOMPARE(report.refused, failed.at(0).at(0).toString());
    QVERIFY2(!report.cancelled, "nobody asked for a stop: it is refused, not stopped");
    QVERIFY(report.added.isEmpty());
    QCOMPARE(disk.library->count(), 0);
    // The looking was told to stop: it was at the fourth file of six and looked at no further one.
    QVERIFY2(gate.calls.load() <= 4,
        qPrintable(QStringLiteral("the looking went on: %1 files").arg(gate.calls.load())));
    QCOMPARE(idle.size(), 1);
    QVERIFY(!runner.isRunning());
}

void ScanRunnerRefusedTest::whatWasStoredStays()
{
    // SR-REFUSED: the parts stored before the refusal stay, and are in the report; the next scan
    // finds the rest.
    Folder folder;
    OnDisk disk;
    Gate gate;
    gate.at = 3;
    ScanRunner runner(disk.library.get(), gate.hooks());
    runner.setPartSize(1);
    QSignalSpy failed(disk.library.get(), &Library::failed);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    runner.scan({ folder.root }, options());
    QVERIFY(gate.reached.tryAcquire(1, 10000));
    QTRY_COMPARE_WITH_TIMEOUT(disk.library->count(), 3, 10000); // one.mp3, two.flac, four.ogg
    {
        Holder holder(disk);
        gate.at = -1;
        gate.go.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QTest::qWait(150);
    }
    QCOMPARE(failed.size(), 1);
    const ScanReport first = reportOf(finished, 0);
    QVERIFY(!first.refused.isEmpty());
    QCOMPARE(first.added.size(), 3);
    QCOMPARE(disk.library->count(), 3);
    // Free again: the same scan stores what is left.
    runner.scan({ folder.root }, options());
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000);
    const ScanReport second = reportOf(finished, 1);
    QVERIFY2(second.refused.isEmpty(), qPrintable(second.refused));
    QCOMPARE(second.added.size(), 2); // three.m4a, five.opus
    QCOMPARE(second.unchanged, 3);
    QCOMPARE(disk.library->count(), 5);
    QCOMPARE(failed.size(), 1);
}

void ScanRunnerRefusedTest::theNextScanStillRuns()
{
    // SR-REFUSED: the refusal ends its own scan only. One asked for meanwhile begins, and asks the
    // library itself: once.
    Folder folder;
    OnDisk disk;
    Gate gate;
    ScanRunner runner(disk.library.get(), gate.hooks());
    runner.setPartSize(2);
    QSignalSpy failed(disk.library.get(), &Library::failed);
    QSignalSpy started(&runner, &ScanRunner::started);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QSignalSpy idle(&runner, &ScanRunner::idle);
    {
        Holder holder(disk);
        runner.scan({ folder.root }, options());
        runner.scan({ folder.root + s(u"/a") }, options());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 15000);
        QTest::qWait(150);
    }
    QCOMPARE(started.size(), 2);
    QCOMPARE(failed.size(), 2);
    QVERIFY(!reportOf(finished, 0).refused.isEmpty());
    QVERIFY(!reportOf(finished, 1).refused.isEmpty());
    QCOMPARE(idle.size(), 1);
    QCOMPARE(disk.library->count(), 0);
    // And a third, with the library free, is a scan as ever: the runner remembers nothing of it.
    runner.scan({ folder.root }, options());
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 15000);
    QVERIFY(reportOf(finished, 2).refused.isEmpty());
    QVERIFY(!reportOf(finished, 2).cancelled);
    QCOMPARE(disk.library->count(), 5);
}

void ScanRunnerRefusedTest::aStopAskedForIsStillAStop()
{
    // SR-STOP beside SR-REFUSED: a scan the user stopped, and which was also refused, says both.
    Folder folder;
    OnDisk disk;
    Gate gate;
    gate.at = 3;
    ScanRunner runner(disk.library.get(), gate.hooks());
    runner.setPartSize(1);
    QSignalSpy failed(disk.library.get(), &Library::failed);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    Holder holder(disk);
    runner.scan({ folder.root }, options());
    QVERIFY(gate.reached.tryAcquire(1, 10000));
    QTRY_VERIFY_WITH_TIMEOUT(failed.size() >= 1, 10000);
    runner.cancel();
    gate.go.release();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QVERIFY(reportOf(finished, 0).cancelled);
    QVERIFY(!reportOf(finished, 0).refused.isEmpty());
    QCOMPARE(failed.size(), 1);
}

void ScanRunnerRefusedTest::nobodyWaits()
{
    // L-WAIT: a scan nobody asked for (`ScanOptions::nobodyWaits`: the folder monitor's) waits the
    // short wait for the library; a scan the user asked for waits the long one, before and after.
    // (The one place with a clock: limits, not measures.)
    Folder folder;
    OnDisk disk;
    disk.library->setWriteWait(800ms);
    disk.library->setQuickWait(100ms);
    const ScanFindings findings = findForScan({ folder.root }, disk.library->scanKnown(), { });
    QSignalSpy failed(disk.library.get(), &Library::failed);
    QElapsedTimer timer;
    const auto took = [&](bool nobody) {
        ScanOptions o = options();
        o.nobodyWaits = nobody;
        timer.restart();
        const ScanReport r = disk.library->applyScan(findings, o);
        const qint64 ms = timer.elapsed();
        return r.refused.isEmpty() ? qint64(-1) : ms;
    };
    {
        Holder holder(disk);
        qint64 ms = took(false);
        QVERIFY2(
            ms >= 650, qPrintable(QStringLiteral("a scan asked for waited only %1 ms").arg(ms)));
        ms = took(true);
        QVERIFY2(ms >= 70 && ms < 500,
            qPrintable(QStringLiteral("a scan nobody asked for waited %1 ms").arg(ms)));
        ms = took(false);
        QVERIFY2(ms >= 650,
            qPrintable(QStringLiteral("after it, a scan asked for waited only %1 ms").arg(ms)));
        QCOMPARE(failed.size(), 3);
    }
    // Free: stored, and the long wait is back after a scan that was stored too.
    ScanOptions o = options();
    o.nobodyWaits = true;
    QVERIFY(disk.library->applyScan(findings, o).refused.isEmpty());
    QCOMPARE(disk.library->count(), 5);
    Holder holder(disk);
    const qint64 ms = took(false);
    QVERIFY2(ms >= 650,
        qPrintable(
            QStringLiteral("after a stored one, a scan asked for waited only %1 ms").arg(ms)));
}

QTEST_MAIN(ScanRunnerRefusedTest)
#include "tst_ui_scanrunner_refused.moc"
