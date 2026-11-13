#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

#include "playback/player.h"
#include "tags/tagreader.h"

// Plays a file: ffmpeg decodes it to plain samples in a child process, and
// the samples go to a sink a little at a time. Contract:
// docs/spec/audio-engine.md (AE-x). The sink is where sound leaves the
// program; the tests use one that only counts.
namespace linuxbee {

// Samples are 32-bit floats in the machine's byte order, channels interleaved.
struct PcmFormat {
    int sampleRate = 44100;
    int channels = 2;
    int bytesPerFrame() const { return channels * 4; }
    bool operator==(const PcmFormat &) const = default;
};

class PcmSink {
public:
    virtual ~PcmSink() = default;
    virtual bool start(const PcmFormat &format) = 0; // false: cannot play this format
    virtual void stop() = 0; // drops whatever is still buffered
    virtual void setPaused(bool paused) = 0;
    virtual qint64 bytesFree() const = 0; // how much write() would take now
    virtual qint64 write(const char *data, qint64 bytes) = 0; // returns what it took
    virtual bool idle() const = 0; // everything written has been heard
};

// AE-FORMAT
PcmFormat playbackFormat(const AudioInfo &info);

// AE-ARGS: the arguments ffmpeg is run with (the program name is not included).
QStringList decodeArguments(const QString &path, qint64 startMs, const PcmFormat &format);

class FfmpegAudioBackend : public AudioBackend {
    Q_OBJECT
public:
    // The sink is not owned and must outlive the backend.
    explicit FfmpegAudioBackend(PcmSink *sink, QObject *parent = nullptr,
        const QString &ffmpegProgram = QStringLiteral("ffmpeg"));
    ~FfmpegAudioBackend() override;

    void load(const QString &path) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 ms) override;
    void setVolume(double linear) override;

private:
    struct Private;
    Private *d;
};

}
