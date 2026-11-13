#include "audio/devicesink.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <memory>

// The session on the default output device (AD-START, AD-WRITE, AD-PAUSE,
// AD-IDLE, AD-STOP). QAudioSink runs in push mode: start() gives the QIODevice
// to write to, and bytesFree() says how much it takes now.
namespace linuxbee::ui {

struct DeviceSink::Private {
    std::unique_ptr<QAudioSink> sink; // null when there is no session
    QIODevice *io = nullptr; // owned by sink
    PcmFormat format { 0, 0 }; // of the running session
    bool paused = false;
};

namespace {

// AD-START: the rates and channel counts refused whatever the machine.
bool formatInRange(const PcmFormat &format)
{
    return format.sampleRate >= 8000 && format.sampleRate <= 192000
        && (format.channels == 1 || format.channels == 2);
}

}

// The Private block is owned by this sink and deleted in its destructor.
DeviceSink::DeviceSink(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
}

DeviceSink::~DeviceSink()
{
    stop();
    delete d;
}

bool DeviceSink::start(const PcmFormat &format)
{
    // AD-START: a start ends any old session first, and a refused start leaves none.
    stop();
    if (!formatInRange(format))
        return false;

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return false;

    QAudioFormat wanted;
    wanted.setSampleRate(format.sampleRate);
    wanted.setChannelCount(format.channels);
    wanted.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(wanted))
        return false;

    auto sink = std::make_unique<QAudioSink>(device, wanted);
    QIODevice *io = sink->start();
    if (!io || sink->error() != QtAudio::NoError)
        return false;

    d->sink = std::move(sink);
    d->io = io;
    d->format = format;
    d->paused = false; // AD-START: a new session is never paused
    return true;
}

void DeviceSink::stop()
{
    // AD-STOP: end the session at once and drop what was buffered.
    if (d->sink)
        d->sink->stop();
    d->sink.reset();
    d->io = nullptr;
    d->format = { 0, 0 };
    d->paused = false;
}

void DeviceSink::setPaused(bool paused)
{
    // AD-PAUSE: suspend keeps what is buffered and plays nothing. Without a session, nothing.
    if (!d->sink)
        return;
    d->paused = paused;
    if (paused)
        d->sink->suspend();
    else
        d->sink->resume();
}

qint64 DeviceSink::bytesFree() const
{
    // AD-WRITE: no room when there is no session or it is paused; whole frames otherwise.
    if (!d->sink || d->paused)
        return 0;
    const qint64 frame = d->format.bytesPerFrame();
    const qint64 room = qMax<qint64>(0, d->sink->bytesFree());
    return room - room % frame;
}

qint64 DeviceSink::write(const char *data, qint64 bytes)
{
    // AD-WRITE: hands over at most bytesFree(), in whole frames, and never blocks.
    if (bytes <= 0)
        return 0;
    const qint64 room = bytesFree();
    if (room <= 0 || !d->io)
        return 0;
    const qint64 frame = d->format.bytesPerFrame();
    qint64 take = qMin(bytes, room);
    take -= take % frame;
    if (take <= 0)
        return 0;
    const qint64 took = d->io->write(data, take);
    return took > 0 ? took : 0;
}

bool DeviceSink::idle() const
{
    // AD-IDLE: the device's buffer is empty, and always so when there is no session.
    if (!d->sink)
        return true;
    return d->sink->bytesFree() >= d->sink->bufferSize();
}

bool DeviceSink::isStarted() const
{
    return d->sink != nullptr;
}

PcmFormat DeviceSink::format() const
{
    // { 0, 0 } when there is no session (AD-START).
    return d->format;
}

}
