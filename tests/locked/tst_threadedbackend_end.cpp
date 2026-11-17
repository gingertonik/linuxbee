// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The audio thread, when its backend is stuck in a call: the program can still close.
// Expected values: docs/spec/audio-thread.md, AT-END.
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <cstring>
#include <new>

#include "playback/threadedbackend.h"

using namespace linuxbee;
using namespace std::chrono_literals;

namespace {

constexpr int kLong = 15000; // a limit, never a measure

struct Probe {
    std::atomic<QThread *> madeOn { nullptr };
    std::atomic<QThread *> destroyedOn { nullptr };
    std::atomic<bool> inLoad { false };
    std::atomic<bool> leftLoad { false };
    std::atomic<int> reportsAfter { 0 }; // emitted after the stuck call came back
    QSemaphore gate; // released: the stuck call comes back
    QSemaphore destroyed;
};

// Its load() does not come back until the gate is released (or thirty seconds have passed).
class Stuck : public AudioBackend {
public:
    explicit Stuck(Probe *probe)
        : m_probe(probe)
    {
        m_probe->madeOn = QThread::currentThread();
    }
    ~Stuck() override
    {
        m_probe->destroyedOn = QThread::currentThread();
        m_probe->destroyed.release();
    }
    void load(const QString &) override
    {
        m_probe->inLoad = true;
        m_probe->gate.tryAcquire(1, 30000);
        m_probe->leftLoad = true;
        // What an engine does when a call comes back: it reports.
        emit durationChanged(1000);
        emit positionChanged(5);
        emit failed(QStringLiteral("late"));
        emit ended();
        m_probe->reportsAfter = 4;
    }
    void play() override { emit positionChanged(6); }
    void pause() override { }
    void stop() override { }
    void seek(qint64) override { }
    void setVolume(double) override { }

private:
    Probe *m_probe;
};

int gWarnings = 0;
QtMessageHandler gEarlier = nullptr;
void counting(QtMsgType type, const QMessageLogContext &context, const QString &text)
{
    if (type == QtWarningMsg && text.contains(QLatin1String("audio thread")))
        ++gWarnings;
    else if (gEarlier)
        gEarlier(type, context, text);
}

struct CountWarnings {
    CountWarnings()
    {
        gWarnings = 0;
        gEarlier = qInstallMessageHandler(counting);
    }
    ~CountWarnings() { qInstallMessageHandler(gEarlier); }
};

}

class ThreadedBackendEndTest : public QObject {
    Q_OBJECT
private slots:
    void aHealthyOneEndsAsBeforeAndSaysNothing();
    void aWaitOfNothingIsTheUsualWait();
    void aStuckOneIsLeftBehind();
    void whatItDoesLaterReachesNothing();
    void manyInARow();
};

void ThreadedBackendEndTest::aHealthyOneEndsAsBeforeAndSaysNothing()
{
    const CountWarnings count;
    Probe probe;
    {
        ThreadedAudioBackend backend([&probe]() -> AudioBackend * { return new Stuck(&probe); });
        backend.setEndWait(100ms);
        backend.play();
    }
    // Already so when the destructor has returned.
    QVERIFY(probe.destroyedOn.load() != nullptr);
    QCOMPARE(probe.destroyedOn.load(), probe.madeOn.load());
    QCOMPARE(gWarnings, 0);
}

void ThreadedBackendEndTest::aWaitOfNothingIsTheUsualWait()
{
    // AT-END: no wait, or less, is not a wait anyone can mean: a healthy thread would be left
    // behind. The inner backend here takes a third of a second to go, as one closing a sound device
    // may: the usual wait is long enough for it, and no wait, or a wait of a moment, is not.
    class Lingering : public Stuck {
    public:
        using Stuck::Stuck;
        ~Lingering() override { QThread::msleep(300); }
    };
    const CountWarnings count;
    for (const std::chrono::milliseconds wait : { 0ms, -5ms }) {
        Probe probe;
        {
            ThreadedAudioBackend backend(
                [&probe]() -> AudioBackend * { return new Lingering(&probe); });
            backend.setEndWait(wait);
            backend.play();
        }
        // Already so when the destructor has returned: it waited, and the thread ended.
        QVERIFY(probe.destroyedOn.load() != nullptr);
        QCOMPARE(probe.destroyedOn.load(), probe.madeOn.load());
    }
    QCOMPARE(gWarnings, 0);
}

void ThreadedBackendEndTest::aStuckOneIsLeftBehind()
{
    const CountWarnings count;
    Probe probe;
    QElapsedTimer clock;
    {
        ThreadedAudioBackend backend([&probe]() -> AudioBackend * { return new Stuck(&probe); });
        backend.setEndWait(100ms);
        backend.load(QStringLiteral("/music/on-a-dead-mount.flac"));
        QTRY_VERIFY_WITH_TIMEOUT(probe.inLoad.load(), kLong);
        backend.play();
        clock.start();
    }
    const qint64 took = clock.elapsed();
    // The destructor came back while the call was still stuck: it did not wait for it.
    QVERIFY2(!probe.leftLoad.load(), "the destructor waited for a call that never came back");
    QVERIFY(probe.destroyedOn.load() == nullptr);
    // It waited the time it was told to, not the three seconds it waits unless told. (The one clock
    // in this test: a wait of a tenth of a second, with twenty times that as its limit.)
    QVERIFY2(took < 2000, qPrintable(QStringLiteral("%1 ms").arg(took)));
    QCOMPARE(gWarnings, 1); // said once

    // When the call does come back, what was left behind clears itself away, on its own thread.
    probe.gate.release();
    QVERIFY(probe.destroyed.tryAcquire(1, kLong));
    QCOMPARE(probe.destroyedOn.load(), probe.madeOn.load());
    QVERIFY(probe.destroyedOn.load() != QThread::currentThread());
    QCOMPARE(probe.reportsAfter.load(), 4);
    QCOMPARE(gWarnings, 1);
}

// The object is built in memory of this test's own, destroyed while its backend is stuck, and the
// memory is then filled with rubbish. Whatever the audio thread does afterwards (it reports four
// times, and handles the play() that was on its way) must not touch that memory: if it posts to the
// object that was there, it follows rubbish.
void ThreadedBackendEndTest::whatItDoesLaterReachesNothing()
{
    const CountWarnings count;
    Probe probe;
    alignas(ThreadedAudioBackend) static unsigned char room[sizeof(ThreadedAudioBackend)];
    int delivered = 0;
    {
        auto *backend = new (room)
            ThreadedAudioBackend([&probe]() -> AudioBackend * { return new Stuck(&probe); });
        connect(backend, &AudioBackend::ended, this, [&delivered] { ++delivered; });
        connect(
            backend, &AudioBackend::positionChanged, this, [&delivered](qint64) { ++delivered; });
        backend->setEndWait(50ms);
        backend->load(QStringLiteral("/music/stuck.flac"));
        QTRY_VERIFY_WITH_TIMEOUT(probe.inLoad.load(), kLong);
        backend->play();
        backend->~ThreadedAudioBackend();
    }
    std::memset(room, 0xAB, sizeof(room));
    probe.gate.release();
    QVERIFY(probe.destroyed.tryAcquire(1, kLong));
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    QCOMPARE(delivered, 0);
    QCOMPARE(probe.reportsAfter.load(), 4);
}

void ThreadedBackendEndTest::manyInARow()
{
    const CountWarnings count;
    QList<Probe *> probes;
    for (int round = 0; round < 12; ++round) {
        auto *probe = new Probe;
        probes.append(probe);
        ThreadedAudioBackend backend([probe]() -> AudioBackend * { return new Stuck(probe); });
        backend.setEndWait(20ms);
        backend.load(QStringLiteral("/music/stuck.flac"));
        QTRY_VERIFY_WITH_TIMEOUT(probe->inLoad.load(), kLong);
    }
    QCOMPARE(gWarnings, 12);
    for (Probe *probe : std::as_const(probes))
        probe->gate.release();
    for (Probe *probe : std::as_const(probes)) {
        QVERIFY(probe->destroyed.tryAcquire(1, kLong));
        QCOMPARE(probe->destroyedOn.load(), probe->madeOn.load());
    }
    qDeleteAll(probes);
}

QTEST_GUILESS_MAIN(ThreadedBackendEndTest)
#include "tst_threadedbackend_end.moc"
