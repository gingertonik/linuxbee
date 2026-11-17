// LOCKED ACCEPTANCE TEST (the program's real sound is made on the audio thread). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/playback-shell.md (PS-MAIN) and docs/spec/audio-thread.md
// (AT-OWN, AT-END). A test must never make a noise, and a machine that builds linuxBee may have no
// sound device: nothing here plays.
#include <QPointer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <atomic>

#include "audio/devicebackend.h"
#include "audio/devicesink.h"
#include "playback/audioengine.h"
#include "playback/threadedbackend.h"

using namespace linuxbee;
using linuxbee::ui::DeviceSink;
using linuxbee::ui::makeDeviceBackend;

namespace {

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

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(name);
}

}

class DeviceBackendTest : public QObject {
    Q_OBJECT
private slots:
    void madeOnTheThreadThatAsks();
    void eachItsOwnSink();
    void behindTheAudioThread();
    void twoAtOnce();
};

void DeviceBackendTest::madeOnTheThreadThatAsks()
{
    // PS-MAIN: an engine over a device sink; the sink is its child and goes with it; both belong to
    // the thread that made them, which here is not the test's.
    QThread *const mine = QThread::currentThread();
    std::atomic<bool> isEngine { false }, sinkIsChild { false }, sinkWentWithIt { false };
    std::atomic<int> sinks { -1 };
    std::atomic<QThread *> madeOn { nullptr }, backendOn { nullptr }, sinkOn { nullptr };
    QThread *worker = QThread::create([&] {
        madeOn = QThread::currentThread();
        AudioBackend *backend = makeDeviceBackend();
        if (!backend)
            return;
        isEngine = qobject_cast<FfmpegAudioBackend *>(backend) != nullptr;
        backendOn = backend->thread();
        const QList<DeviceSink *> found = backend->findChildren<DeviceSink *>();
        sinks = int(found.size());
        QPointer<DeviceSink> sink = found.isEmpty() ? nullptr : found.first();
        if (sink) {
            sinkIsChild = sink->parent() == backend;
            sinkOn = sink->thread();
        }
        delete backend;
        sinkWentWithIt = sink.isNull();
    });
    worker->start();
    QVERIFY(worker->wait(15000));
    delete worker;
    QVERIFY2(isEngine, "what was made is not the ffmpeg engine");
    QCOMPARE(sinks.load(), 1);
    QVERIFY(sinkIsChild);
    QVERIFY2(sinkWentWithIt, "the sink outlived its engine");
    QVERIFY(madeOn.load() != nullptr && madeOn.load() != mine);
    QCOMPARE(backendOn.load(), madeOn.load());
    QCOMPARE(sinkOn.load(), madeOn.load());
}

void DeviceBackendTest::eachItsOwnSink()
{
    // Two engines never share a sink (gapless playback will want two).
    AudioBackend *a = makeDeviceBackend();
    AudioBackend *b = makeDeviceBackend();
    QVERIFY(a && b && a != b);
    DeviceSink *sa = a->findChild<DeviceSink *>();
    DeviceSink *sb = b->findChild<DeviceSink *>();
    QVERIFY(sa && sb);
    QVERIFY(sa != sb);
    QVERIFY(!sa->isStarted() && !sb->isStarted()); // making it starts no sound
    delete a;
    QVERIFY(!sb->isStarted());
    delete b;
}

void DeviceBackendTest::behindTheAudioThread()
{
    // PS-MAIN as main has it: the factory given to a ThreadedAudioBackend. The engine is made on
    // the audio thread, answers from there, and ends in time without a word.
    const CountWarnings count;
    QThread *const mine = QThread::currentThread();
    std::atomic<QThread *> madeOn { nullptr };
    QElapsedTimer timer;
    {
        ThreadedAudioBackend backend([&madeOn]() -> AudioBackend * {
            AudioBackend *made = makeDeviceBackend();
            madeOn = made ? made->thread() : nullptr;
            return made;
        });
        QSignalSpy duration(&backend, &AudioBackend::durationChanged);
        QSignalSpy failed(&backend, &AudioBackend::failed);
        backend.load(fixture("basic.flac")); // loaded, never played
        QTRY_COMPARE_WITH_TIMEOUT(duration.size(), 1, 15000);
        QVERIFY(duration.at(0).at(0).toLongLong() > 0);
        QCOMPARE(failed.size(), 0);
        QVERIFY(madeOn.load() != nullptr && madeOn.load() != mine);
        backend.stop();
        timer.start();
    }
    QVERIFY2(timer.elapsed() < 2500,
        "the audio thread did not end in time"); // a limit, not a measure: under AT-END's three
                                                 // seconds
    QCOMPARE(gWarnings, 0);
}

void DeviceBackendTest::twoAtOnce()
{
    const CountWarnings count;
    {
        ThreadedAudioBackend one(makeDeviceBackend);
        ThreadedAudioBackend two(makeDeviceBackend);
        QSignalSpy first(&one, &AudioBackend::durationChanged);
        QSignalSpy second(&two, &AudioBackend::durationChanged);
        one.load(fixture("basic.flac"));
        two.load(fixture("basic.mp3"));
        QTRY_COMPARE_WITH_TIMEOUT(first.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(second.size(), 1, 15000);
    }
    QCOMPARE(gWarnings, 0);
}

QTEST_MAIN(DeviceBackendTest)
#include "tst_ui_devicebackend.moc"
