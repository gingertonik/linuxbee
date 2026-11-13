// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/audio-device.md (AD-x).
//
// What this test can and cannot do: a machine that builds linuxBee may have
// no sound device, and a test must never make a noise. So everything here
// holds with or without a device, and the only samples ever written are
// silence. Whether music sounds right is for a person to judge.
#include <QtTest>

#include <vector>

#include "audio/devicesink.h"

using linuxbee::PcmFormat;
using linuxbee::ui::DeviceSink;

class AudioDeviceTest : public QObject {
    Q_OBJECT
private slots:
    void notStarted();
    void refusedFormats();
    void session();
    void restart();
    void destroyedWhileRunning();

private:
    // Starts the sink if this machine can; false means there is no usable device.
    static bool startAny(DeviceSink &sink)
    {
        return sink.start({ 44100, 2 }) || sink.start({ 48000, 2 });
    }
};

void AudioDeviceTest::notStarted()
{
    // AD-IDLE: a sink that is not started takes nothing and has nothing left to play.
    DeviceSink sink;
    QVERIFY(!sink.isStarted());
    QVERIFY((sink.format() == PcmFormat { 0, 0 }));
    QCOMPARE(sink.bytesFree(), 0);
    const std::vector<float> silence(256, 0.0f);
    QCOMPARE(sink.write(reinterpret_cast<const char *>(silence.data()), 1024), 0);
    QVERIFY(sink.idle());
    sink.setPaused(true); // harmless
    sink.setPaused(false);
    sink.stop();
    sink.stop();
    QVERIFY(!sink.isStarted());
    QCOMPARE(sink.bytesFree(), 0);
}

void AudioDeviceTest::refusedFormats()
{
    // AD-START: formats outside what the engine may ask for are refused, device or no device.
    DeviceSink sink;
    for (const PcmFormat &bad : { PcmFormat { 0, 0 }, PcmFormat { 44100, 0 }, PcmFormat { 44100, 3 }, PcmFormat { 44100, -1 },
             PcmFormat { 7999, 2 }, PcmFormat { 192001, 2 }, PcmFormat { -44100, 2 } }) {
        QVERIFY2(!sink.start(bad), qPrintable(QStringLiteral("%1/%2").arg(bad.sampleRate).arg(bad.channels)));
        QVERIFY(!sink.isStarted());
        QCOMPARE(sink.bytesFree(), 0);
    }
}

void AudioDeviceTest::session()
{
    DeviceSink sink;
    if (!startAny(sink)) {
        // No device here: the refusal must be a clean one.
        QVERIFY(!sink.isStarted());
        QCOMPARE(sink.bytesFree(), 0);
        QVERIFY(sink.idle());
        return;
    }
    // AD-START
    QVERIFY(sink.isStarted());
    const PcmFormat f = sink.format();
    QVERIFY(f.channels == 2 && (f.sampleRate == 44100 || f.sampleRate == 48000));
    // AD-WRITE: room is offered in whole frames, and no more is taken than was offered.
    const qint64 room = sink.bytesFree();
    QVERIFY2(room > 0, "a sink that has just started has room");
    QCOMPARE(room % f.bytesPerFrame(), 0);
    const std::vector<float> silence(size_t(room / 4 + 64), 0.0f);
    const char *data = reinterpret_cast<const char *>(silence.data());
    QCOMPARE(sink.write(data, 0), 0);
    const qint64 some = qMin<qint64>(room, 4096);
    QCOMPARE(sink.write(data, some), some);
    const qint64 left = sink.bytesFree();
    QVERIFY(left <= room);
    const qint64 took = sink.write(data, left + 64 * 4); // more than there is room for
    QVERIFY2(took <= left, qPrintable(QStringLiteral("%1 of %2").arg(took).arg(left)));
    QCOMPARE(took % f.bytesPerFrame(), 0);
    QCOMPARE(sink.write(data, 3), 0); // less than a frame: nothing
    // AD-PAUSE: paused, it offers no room and takes nothing.
    sink.setPaused(true);
    QCOMPARE(sink.bytesFree(), 0);
    QCOMPARE(sink.write(data, 800), 0);
    QVERIFY(sink.isStarted());
    sink.setPaused(true);
    sink.setPaused(false);
    QVERIFY(sink.bytesFree() >= 0);
    // AD-STOP
    sink.stop();
    QVERIFY(!sink.isStarted());
    QVERIFY((sink.format() == PcmFormat { 0, 0 }));
    QCOMPARE(sink.bytesFree(), 0);
    QCOMPARE(sink.write(data, 800), 0);
    QVERIFY(sink.idle());
}

void AudioDeviceTest::restart()
{
    // AD-START: starting again replaces the session; a refused start leaves none.
    DeviceSink sink;
    if (!startAny(sink))
        return;
    QVERIFY(startAny(sink)); // without a stop in between
    QVERIFY(sink.isStarted());
    sink.setPaused(true);
    QVERIFY(startAny(sink)); // a new session is not paused
    QVERIFY(sink.bytesFree() > 0);
    QVERIFY(!sink.start({ 44100, 7 }));
    QVERIFY(!sink.isStarted());
    QCOMPARE(sink.bytesFree(), 0);
    for (int i = 0; i < 5; ++i) { // open and close a few times: nothing leaks into the next
        QVERIFY(startAny(sink));
        sink.stop();
    }
    QVERIFY(!sink.isStarted());
}

void AudioDeviceTest::destroyedWhileRunning()
{
    {
        DeviceSink sink;
        startAny(sink);
        const std::vector<float> silence(1024, 0.0f);
        sink.write(reinterpret_cast<const char *>(silence.data()), 4096);
    }
    QTest::qWait(50); // nothing of it may fire after it has gone
    QVERIFY(true);
}

QTEST_MAIN(AudioDeviceTest)
#include "tst_ui_audiodevice.moc"
