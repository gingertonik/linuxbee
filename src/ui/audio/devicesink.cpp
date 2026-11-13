#include "audio/devicesink.h"

// Task 2 of plan 14d: the calls that need no session. A sink that is not
// started takes nothing, offers no room and has nothing left to play (AD-IDLE),
// and every format outside the engine's range is refused (AD-START). Opening
// the default output device, the session and its buffer come in task 3.
namespace linuxbee::ui {

struct DeviceSink::Private { };

DeviceSink::DeviceSink(QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
}

DeviceSink::~DeviceSink()
{
    Q_UNUSED(d);
}

bool DeviceSink::start(const PcmFormat &format)
{
    // AD-START: a start ends any old session first, and a refused start leaves none.
    stop();
    if (format.sampleRate < 8000 || format.sampleRate > 192000)
        return false;
    if (format.channels != 1 && format.channels != 2)
        return false;
    // Opening the default output device is task 3. Until then a valid format is refused too.
    return false;
}

void DeviceSink::stop()
{
    // No session exists yet, so there is nothing to end (AD-STOP comes in task 3).
}

void DeviceSink::setPaused(bool paused)
{
    // Without a session pausing does nothing (AD-PAUSE).
    Q_UNUSED(paused);
}

qint64 DeviceSink::bytesFree() const
{
    // Not started: no room (AD-WRITE).
    return 0;
}

qint64 DeviceSink::write(const char *data, qint64 bytes)
{
    // Not started: takes nothing (AD-WRITE).
    Q_UNUSED(data);
    Q_UNUSED(bytes);
    return 0;
}

bool DeviceSink::idle() const
{
    // Without a session there is nothing left to play (AD-IDLE).
    return true;
}

bool DeviceSink::isStarted() const
{
    return false;
}

PcmFormat DeviceSink::format() const
{
    // { 0, 0 } when there is no session (AD-START).
    return { 0, 0 };
}

}
