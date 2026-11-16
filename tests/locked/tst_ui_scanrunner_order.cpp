// LOCKED ACCEPTANCE TEST (the scan runner: order, and a scan after a stop). Written by the reviewer, not the builder.
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

QStringList pathsOf(const Library &library, const QList<qint64> &ids)
{
    QStringList out;
    for (const qint64 id : ids) {
        const auto t = library.track(id);
        out.append(t ? t->path : QStringLiteral("<gone>"));
    }
    return out;
}

}

class ScanRunnerOrderTest : public QObject {
    Q_OBJECT
private slots:
    void partsAreStoredInTheOrderLookedAt();
    void aScanAskedForAfterAStop();
    void aRefusedScanIsNeverStopped();
};

void ScanRunnerOrderTest::partsAreStoredInTheOrderLookedAt()
{
    // SR-SCAN: the parts are stored in the order they were handed over.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    Gate gate;
    ScanRunner runner(library.get(), gate.hooks());
    runner.setPartSize(1);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QList<qint64> stored; // as tracksAdded named them, signal after signal
    connect(library.get(), &Library::tracksAdded, this, [&stored](const QList<qint64> &ids) { stored += ids; });
    runner.scan({ f.root }, options());
    QTRY_COMPARE(finished.count(), 1);
    const QStringList looked {
        f.root + s(u"/a/one.mp3"), f.root + s(u"/a/two.flac"), f.root + s(u"/b/four.ogg"),
        f.root + s(u"/b/three.m4a"), f.root + s(u"/c/five.opus"),
    };
    QCOMPARE(pathsOf(*library, stored), looked);
    QCOMPARE(pathsOf(*library, reportOf(finished, 0).added), looked);
}

void ScanRunnerOrderTest::aScanAskedForAfterAStop()
{
    // SR-STOP: a scan asked for after a stop waits for the stopped one to end,
    // and nothing of the stopped one is in its report.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    Gate gate;
    gate.at = 2;
    ScanRunner runner(library.get(), gate.hooks());
    runner.setPartSize(1);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QStringList said; // the runner's signals, in order
    connect(&runner, &ScanRunner::started, this, [&](const QStringList &folders) { said.append(s(u"started ") + QFileInfo(folders.value(0)).fileName()); });
    connect(&runner, &ScanRunner::finished, this, [&](const ScanReport &, const QStringList &folders) { said.append(s(u"finished ") + QFileInfo(folders.value(0)).fileName()); });
    connect(&runner, &ScanRunner::idle, this, [&] { said.append(s(u"idle")); });

    runner.scan({ f.root }, options());
    QVERIFY(gate.reached.tryAcquire(1, 10000)); // held before the third file
    QTRY_COMPARE(library->count(), 2);
    runner.cancel();
    runner.scan({ f.root + s(u"/c") }, options()); // at once, while the stopped one is still held
    QVERIFY(runner.isRunning());
    QCOMPARE(runner.waiting(), 1);
    QCOMPARE(said, QStringList { s(u"started music") });
    gate.at = -1;
    gate.go.release();
    QTRY_COMPARE(finished.count(), 2);
    QTRY_VERIFY(!runner.isRunning());
    QCOMPARE(said, (QStringList { s(u"started music"), s(u"finished music"), s(u"started c"), s(u"finished c"), s(u"idle") }));
    QVERIFY(reportOf(finished, 0).cancelled);
    QCOMPARE(reportOf(finished, 0).added.size(), 2);
    QVERIFY(!reportOf(finished, 1).cancelled);
    QCOMPARE(pathsOf(*library, reportOf(finished, 1).added), QStringList { f.root + s(u"/c/five.opus") });
    QCOMPARE(reportOf(finished, 1).unchanged, 0);
    QCOMPARE(library->count(), 3);
}

void ScanRunnerOrderTest::aRefusedScanIsNeverStopped()
{
    // SR-STOP: a refused scan did nothing that could be stopped.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    Gate gate;
    ScanRunner runner(library.get(), gate.hooks());
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QSignalSpy idle(&runner, &ScanRunner::idle);
    ScanOptions bad = options();
    bad.now = QDateTime();
    runner.scan({ f.root }, bad);
    runner.cancel();
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(!reportOf(finished, 0).cancelled);
    QTRY_COMPARE(idle.count(), 1);
    QCOMPARE(gate.calls.load(), 0);
}

QTEST_MAIN(ScanRunnerOrderTest)
#include "tst_ui_scanrunner_order.moc"
