#pragma once
#include <QObject>

#include "playback/audioengine.h"

// The sink that makes sound: the audio engine's samples go to the system's
// default output device. Contract: docs/spec/audio-device.md (AD-x).
namespace linuxbee::ui {

class DeviceSink : public QObject, public PcmSink {
    Q_OBJECT
public:
    explicit DeviceSink(QObject *parent = nullptr);
    ~DeviceSink() override;

    bool start(const PcmFormat &format) override;
    void stop() override;
    void setPaused(bool paused) override;
    qint64 bytesFree() const override;
    qint64 write(const char *data, qint64 bytes) override;
    bool idle() const override;

    bool isStarted() const;
    PcmFormat format() const; // of the running session; { 0, 0 } when not started

private:
    struct Private;
    Private *d;
};

}
