#include "playback/audioengine.h"

// Stub. Plan 14c replaces every body in this file.
namespace linuxbee {

struct FfmpegAudioBackend::Private { };

PcmFormat playbackFormat(const AudioInfo &info)
{
    Q_UNUSED(info);
    return { 0, 0 };
}

QStringList decodeArguments(const QString &path, qint64 startMs, const PcmFormat &format)
{
    Q_UNUSED(path);
    Q_UNUSED(startMs);
    Q_UNUSED(format);
    return { };
}

FfmpegAudioBackend::FfmpegAudioBackend(PcmSink *sink, QObject *parent, const QString &ffmpegProgram)
    : AudioBackend(parent)
    , d(nullptr)
{
    Q_UNUSED(sink);
    Q_UNUSED(ffmpegProgram);
}

FfmpegAudioBackend::~FfmpegAudioBackend()
{
    Q_UNUSED(d);
}

void FfmpegAudioBackend::load(const QString &path)
{
    Q_UNUSED(path);
}

void FfmpegAudioBackend::play()
{
}
void FfmpegAudioBackend::pause()
{
}
void FfmpegAudioBackend::stop()
{
}

void FfmpegAudioBackend::seek(qint64 ms)
{
    Q_UNUSED(ms);
}

void FfmpegAudioBackend::setVolume(double linear)
{
    Q_UNUSED(linear);
}

}
