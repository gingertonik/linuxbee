#include "audio/devicesink.h"

// Stub. Plan 14d replaces every body in this file.
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
    Q_UNUSED(format);
    return true;
}

void DeviceSink::stop()
{
}

void DeviceSink::setPaused(bool paused)
{
    Q_UNUSED(paused);
}

qint64 DeviceSink::bytesFree() const
{
    return -1;
}

qint64 DeviceSink::write(const char *data, qint64 bytes)
{
    Q_UNUSED(data);
    return bytes;
}

bool DeviceSink::idle() const
{
    return false;
}

bool DeviceSink::isStarted() const
{
    return false;
}

PcmFormat DeviceSink::format() const
{
    return { };
}

}
