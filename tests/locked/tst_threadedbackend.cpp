// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// An audio backend on a thread of its own. Expected values: docs/spec/audio-thread.md (AT-x).
// Nothing here is judged by a clock: a wait ends when a count is reached, and a time limit only
// turns a hang into a failure.
#include <QMutex>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <atomic>

#include "playback/audioengine.h"
#include "playback/threadedbackend.h"

using namespace linuxbee;

namespace {

constexpr int kLong = 15000; // a limit, never a measure

enum class OnPlay { Nothing, Reports, Ended, Failed, Position, Ticks };

// What the two threads share. It outlives every backend.
struct Probe {
    QMutex mutex;
    QStringList calls;
    QList<QThread *> callThreads;
    std::atomic<QThread *> factoryThread { nullptr };
    std::atomic<QThread *> destroyedOn { nullptr };
    std::atomic<int> factoryRuns { 0 };
    std::atomic<bool> enteredLoad { false };
    std::atomic<bool> leftLoad { false };
    std::atomic<bool> blockInLoad { false };
    std::atomic<OnPlay> onPlay { OnPlay::Nothing };
    QSemaphore made;
    QSemaphore called; // one for each call handled
    QSemaphore emitted; // play() has emitted what it was told to
    QSemaphore ticks;
    QSemaphore gate;

    void note(const QString &call)
    {
        const QMutexLocker lock(&mutex);
        calls.append(call);
        callThreads.append(QThread::currentThread());
    }
    QStringList seen()
    {
        const QMutexLocker lock(&mutex);
        return calls;
    }
    bool allOn(QThread *thread)
    {
        const QMutexLocker lock(&mutex);
        return callThreads.count(thread) == callThreads.size();
    }
};

class Fake : public AudioBackend {
    Q_OBJECT
public:
    explicit Fake(Probe *probe)
        : m_probe(probe)
    {
        connect(&m_timer, &QTimer::timeout, this, [this] { m_probe->ticks.release(); });
    }
    ~Fake() override { m_probe->destroyedOn = QThread::currentThread(); }

    void load(const QString &path) override
    {
        m_probe->note(QStringLiteral("load ") + path);
        if (m_probe->blockInLoad) {
            m_probe->enteredLoad = true;
            m_probe->gate.tryAcquire(1, 3000);
            m_probe->leftLoad = true;
        }
        if (path == QLatin1String("marker"))
            emit durationChanged(777);
        m_probe->called.release();
    }
    void play() override
    {
        m_probe->note(QStringLiteral("play"));
        switch (m_probe->onPlay.load()) {
        case OnPlay::Nothing:
            break;
        case OnPlay::Reports:
            emit durationChanged(1000);
            emit positionChanged(10);
            emit positionChanged(20);
            emit failed(QStringLiteral("a message"));
            emit ended();
            break;
        case OnPlay::Ended:
            emit ended();
            break;
        case OnPlay::Failed:
            emit failed(QStringLiteral("old"));
            break;
        case OnPlay::Position:
            emit durationChanged(1);
            emit positionChanged(5000);
            break;
        case OnPlay::Ticks:
            m_timer.start(1);
            break;
        }
        m_probe->emitted.release();
        m_probe->called.release();
    }
    void pause() override
    {
        m_probe->note(QStringLiteral("pause"));
        m_probe->called.release();
    }
    void stop() override
    {
        m_probe->note(QStringLiteral("stop"));
        m_probe->called.release();
    }
    void seek(qint64 ms) override
    {
        m_probe->note(QStringLiteral("seek %1").arg(ms));
        emit positionChanged(ms);
        m_probe->called.release();
    }
    void setVolume(double linear) override
    {
        m_probe->note(QStringLiteral("volume %1").arg(linear));
        m_probe->called.release();
    }

private:
    Probe *m_probe;
    QTimer m_timer;
};

ThreadedAudioBackend::Factory fakeFactory(Probe *probe)
{
    return [probe]() -> AudioBackend * {
        probe->factoryThread = QThread::currentThread();
        ++probe->factoryRuns;
        auto *fake = new Fake(probe);
        probe->made.release();
        return fake;
    };
}

// The reports of a backend, as text, with the thread each came on.
struct Reports {
    QStringList lines;
    bool allHere = true;
    explicit Reports(AudioBackend *backend)
    {
        QObject::connect(backend, &AudioBackend::positionChanged, backend,
            [this](qint64 ms) { add(QStringLiteral("position %1").arg(ms)); });
        QObject::connect(backend, &AudioBackend::durationChanged, backend,
            [this](qint64 ms) { add(QStringLiteral("duration %1").arg(ms)); });
        QObject::connect(
            backend, &AudioBackend::ended, backend, [this] { add(QStringLiteral("ended")); });
        QObject::connect(backend, &AudioBackend::failed, backend,
            [this](const QString &message) { add(QStringLiteral("failed ") + message); });
    }
    void add(const QString &line)
    {
        lines.append(line);
        allHere = allHere && QThread::currentThread() == QCoreApplication::instance()->thread();
    }
};

// A sink that counts, safe to read from another thread.
class CountingSink : public QObject, public PcmSink {
public:
    explicit CountingSink(
        Probe *probe, std::atomic<qint64> *bytes, std::atomic<QThread *> *startedOn)
        : m_probe(probe)
        , m_bytes(bytes)
        , m_startedOn(startedOn)
    {
    }
    bool start(const PcmFormat &) override
    {
        *m_startedOn = QThread::currentThread();
        return true;
    }
    void stop() override { }
    void setPaused(bool) override { }
    qint64 bytesFree() const override { return 1 << 15; }
    qint64 write(const char *, qint64 bytes) override
    {
        *m_bytes += bytes;
        m_probe->ticks.release();
        return bytes;
    }
    bool idle() const override { return true; }

private:
    Probe *m_probe;
    std::atomic<qint64> *m_bytes;
    std::atomic<QThread *> *m_startedOn;
};

}

class ThreadedBackendTest : public QObject {
    Q_OBJECT
private slots:
    void theInnerBackendIsMadeOnAThreadOfItsOwn();
    void callsArriveInOrderOnThatThread();
    void aCallNeverWaitsForTheEngine();
    void reportsComeOnTheCallersThreadInOrder();
    void theEngineGoesOnWhileTheCallerIsBusy();
    void aReportOfTheFileBeforeIsDropped_data();
    void aReportOfTheFileBeforeIsDropped();
    void aPositionFromBeforeASeekIsDropped();
    void anEndBeforeASeekStillArrives();
    void itEndsOnItsOwnThread();
    void itEndsWithCallsOnTheirWay();
    void theRealEnginePlaysWhileTheCallerIsBusy();
};

void ThreadedBackendTest::theInnerBackendIsMadeOnAThreadOfItsOwn()
{
    Probe probe;
    ThreadedAudioBackend backend(fakeFactory(&probe));
    QVERIFY(probe.made.tryAcquire(1, kLong));
    QVERIFY(probe.factoryThread.load() != nullptr);
    QVERIFY(probe.factoryThread.load() != QThread::currentThread());
    QCOMPARE(backend.thread(), QThread::currentThread()); // the object itself stays with its caller
    backend.play();
    QVERIFY(probe.called.tryAcquire(1, kLong));
    QCOMPARE(probe.factoryRuns.load(), 1);
}

void ThreadedBackendTest::callsArriveInOrderOnThatThread()
{
    Probe probe;
    ThreadedAudioBackend backend(fakeFactory(&probe));
    // At once: most of these are made before the factory has run (AT-CALLS).
    backend.load(QStringLiteral("/music/a.flac"));
    backend.setVolume(0.25);
    backend.play();
    backend.seek(1500);
    backend.pause();
    backend.play();
    backend.stop();
    backend.load(QStringLiteral("/music/b.flac"));
    QVERIFY(probe.called.tryAcquire(8, kLong));
    const QStringList expected { QStringLiteral("load /music/a.flac"),
        QStringLiteral("volume 0.25"), QStringLiteral("play"), QStringLiteral("seek 1500"),
        QStringLiteral("pause"), QStringLiteral("play"), QStringLiteral("stop"),
        QStringLiteral("load /music/b.flac") };
    QCOMPARE(probe.seen(), expected);
    QVERIFY(probe.allOn(probe.factoryThread.load()));
    QVERIFY(probe.factoryThread.load() != QThread::currentThread());
}

// The inner backend sits in load() until it is let go (or three seconds have passed). A caller that
// waited for it would come back only after it had left.
void ThreadedBackendTest::aCallNeverWaitsForTheEngine()
{
    Probe probe;
    probe.blockInLoad = true;
    {
        ThreadedAudioBackend backend(fakeFactory(&probe));
        backend.load(QStringLiteral("/music/slow.flac"));
        QTRY_VERIFY_WITH_TIMEOUT(probe.enteredLoad.load(), kLong);
        backend.play();
        backend.seek(10);
        backend.pause();
        backend.setVolume(0.5);
        backend.stop();
        backend.load(QStringLiteral("/music/next.flac"));
        QVERIFY2(!probe.leftLoad.load(), "a call waited for the engine");
        QCOMPARE(probe.seen().size(), 1);
        probe.blockInLoad = false;
        probe.gate.release();
        QVERIFY(probe.called.tryAcquire(7, kLong));
        QCOMPARE(probe.seen().last(), QStringLiteral("load /music/next.flac"));
    }
}

void ThreadedBackendTest::reportsComeOnTheCallersThreadInOrder()
{
    Probe probe;
    probe.onPlay = OnPlay::Reports;
    ThreadedAudioBackend backend(fakeFactory(&probe));
    Reports reports(&backend);
    backend.play();
    QTRY_COMPARE_WITH_TIMEOUT(reports.lines.size(), 5, kLong);
    const QStringList expected { QStringLiteral("duration 1000"), QStringLiteral("position 10"),
        QStringLiteral("position 20"), QStringLiteral("failed a message"),
        QStringLiteral("ended") };
    QCOMPARE(reports.lines, expected);
    QVERIFY(reports.allHere);
}

// The caller runs no event loop here: it sits in tryAcquire. The inner backend's timer must tick
// all the same.
void ThreadedBackendTest::theEngineGoesOnWhileTheCallerIsBusy()
{
    Probe probe;
    probe.onPlay = OnPlay::Ticks;
    ThreadedAudioBackend backend(fakeFactory(&probe));
    backend.play();
    QVERIFY2(probe.ticks.tryAcquire(25, kLong), "the engine stood still while its caller was busy");
}

void ThreadedBackendTest::aReportOfTheFileBeforeIsDropped_data()
{
    QTest::addColumn<int>("onPlay");
    QTest::addColumn<bool>("stopFirst");
    QTest::newRow("an end, then load") << int(OnPlay::Ended) << false;
    QTest::newRow("a failure, then load") << int(OnPlay::Failed) << false;
    QTest::newRow("a length and a position, then load") << int(OnPlay::Position) << false;
    QTest::newRow("an end, then stop") << int(OnPlay::Ended) << true;
    QTest::newRow("a failure, then stop") << int(OnPlay::Failed) << true;
    QTest::newRow("a length and a position, then stop") << int(OnPlay::Position) << true;
}

// AT-STALE. The old file's report is emitted on the audio thread, and only then, with no event loop
// run in between, does the caller ask for something else.
void ThreadedBackendTest::aReportOfTheFileBeforeIsDropped()
{
    QFETCH(int, onPlay);
    QFETCH(bool, stopFirst);
    Probe probe;
    probe.onPlay = OnPlay(onPlay);
    ThreadedAudioBackend backend(fakeFactory(&probe));
    Reports reports(&backend);
    backend.load(QStringLiteral("/music/old.flac"));
    backend.play();
    QVERIFY(
        probe.emitted.tryAcquire(1, kLong)); // emitted over there; nothing has been delivered here
    QVERIFY(reports.lines.isEmpty());
    probe.onPlay = OnPlay::Nothing;
    if (stopFirst) {
        backend.stop();
        // After a stop alone nothing of the old file arrives either.
        QVERIFY(probe.called.tryAcquire(3, kLong));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QCOMPARE(reports.lines, QStringList { });
    }
    backend.load(QStringLiteral("marker")); // the fake reports a length of 777 for it
    QTRY_VERIFY_WITH_TIMEOUT(reports.lines.contains(QStringLiteral("duration 777")), kLong);
    QCOMPARE(reports.lines, QStringList { QStringLiteral("duration 777") });
    // And the new file's reports are not held back.
    probe.onPlay = OnPlay::Ended;
    backend.play();
    QTRY_COMPARE_WITH_TIMEOUT(reports.lines.size(), 2, kLong);
    QCOMPARE(reports.lines.last(), QStringLiteral("ended"));
}

void ThreadedBackendTest::aPositionFromBeforeASeekIsDropped()
{
    Probe probe;
    probe.onPlay = OnPlay::Position;
    ThreadedAudioBackend backend(fakeFactory(&probe));
    Reports reports(&backend);
    backend.load(QStringLiteral("/music/a.flac"));
    backend.play();
    QVERIFY(probe.emitted.tryAcquire(1, kLong));
    backend.seek(100); // the fake reports the position 100 for it
    QTRY_VERIFY_WITH_TIMEOUT(reports.lines.contains(QStringLiteral("position 100")), kLong);
    // The length is not a position: it arrives. The position 5000 does not.
    const QStringList expected { QStringLiteral("duration 1"), QStringLiteral("position 100") };
    QCOMPARE(reports.lines, expected);
    // Positions after the seek arrive as ever.
    backend.seek(200);
    QTRY_COMPARE_WITH_TIMEOUT(reports.lines.size(), 3, kLong);
    QCOMPARE(reports.lines.last(), QStringLiteral("position 200"));
}

void ThreadedBackendTest::anEndBeforeASeekStillArrives()
{
    Probe probe;
    probe.onPlay = OnPlay::Ended;
    ThreadedAudioBackend backend(fakeFactory(&probe));
    Reports reports(&backend);
    backend.load(QStringLiteral("/music/a.flac"));
    backend.play();
    QVERIFY(probe.emitted.tryAcquire(1, kLong));
    backend.seek(100);
    QTRY_VERIFY_WITH_TIMEOUT(reports.lines.contains(QStringLiteral("position 100")), kLong);
    const QStringList expected { QStringLiteral("ended"), QStringLiteral("position 100") };
    QCOMPARE(reports.lines, expected);
}

void ThreadedBackendTest::itEndsOnItsOwnThread()
{
    Probe probe;
    QStringList late;
    {
        ThreadedAudioBackend backend(fakeFactory(&probe));
        connect(&backend, &AudioBackend::ended, this,
            [&late] { late.append(QStringLiteral("ended")); });
        probe.onPlay = OnPlay::Ended;
        backend.play();
        QVERIFY(probe.emitted.tryAcquire(1, kLong)); // an end is on its way and never delivered
    }
    // Already so when the destructor has returned: no waiting here.
    QVERIFY(probe.destroyedOn.load() != nullptr);
    QCOMPARE(probe.destroyedOn.load(), probe.factoryThread.load());
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCOMPARE(late, QStringList { });
}

void ThreadedBackendTest::itEndsWithCallsOnTheirWay()
{
    for (int round = 0; round < 50; ++round) {
        Probe probe;
        {
            ThreadedAudioBackend backend(fakeFactory(&probe));
            backend.load(QStringLiteral("/music/a.flac"));
            backend.play();
            backend.seek(round);
        }
        // Whether the factory ever ran is not promised; if it did, what it made is gone, from
        // there.
        if (probe.factoryRuns.load() > 0)
            QCOMPARE(probe.destroyedOn.load(), probe.factoryThread.load());
        else
            QVERIFY(probe.destroyedOn.load() == nullptr);
    }
}

// The engine itself, ffmpeg and all, with a sink that counts: one second of sound goes through
// while this thread does nothing but wait for the count.
void ThreadedBackendTest::theRealEnginePlaysWhileTheCallerIsBusy()
{
    Probe probe;
    std::atomic<qint64> bytes { 0 };
    std::atomic<QThread *> startedOn { nullptr };
    ThreadedAudioBackend backend([&]() -> AudioBackend * {
        auto *sink = new CountingSink(&probe, &bytes, &startedOn);
        auto *engine = new FfmpegAudioBackend(sink);
        sink->setParent(engine); // goes after the engine has let go of it
        return engine;
    });
    Reports reports(&backend);
    backend.load(QStringLiteral(LINUXBEE_TEST_DATA "/playback/tone.flac")); // 1 s, 44100 Hz stereo
    backend.play();
    const qint64 whole = 44100 * 2 * 4;
    int writes = 0;
    while (bytes.load() < whole && probe.ticks.tryAcquire(1, kLong))
        ++writes;
    QCOMPARE(bytes.load(), whole);
    QVERIFY2(writes >= 4, "the sound came in pieces, as from a pump");
    QVERIFY(startedOn.load() != nullptr);
    QVERIFY(startedOn.load() != QThread::currentThread());
    QVERIFY2(
        reports.lines.isEmpty(), "this thread ran no event loop, so nothing was delivered yet");
    QTRY_VERIFY_WITH_TIMEOUT(reports.lines.contains(QStringLiteral("ended")), kLong);
    QVERIFY(reports.lines.contains(QStringLiteral("duration 1000")));
    QVERIFY(reports.allHere);
}

QTEST_GUILESS_MAIN(ThreadedBackendTest)
#include "tst_threadedbackend.moc"
