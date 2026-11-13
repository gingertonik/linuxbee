#include "playback/audioengine.h"

#include <QProcess>
#include <QTimer>

#include <algorithm>

namespace linuxbee {

namespace {

// AE-POSITION: positionChanged comes at most once per 100 ms of music.
constexpr qint64 kPositionStepMs = 100;
// How often the engine moves decoded samples into the sink.
constexpr int kPumpIntervalMs = 10;

}

struct FfmpegAudioBackend::Private {
    Private(FfmpegAudioBackend *o, PcmSink *s, const QString &program)
        : owner(o)
        , sink(s)
        , ffmpegProgram(program)
    {
        timer.setInterval(kPumpIntervalMs);
        QObject::connect(&timer, &QTimer::timeout, owner, [this] { pump(); });
        QObject::connect(&process, &QProcess::finished, owner,
            [this](int, QProcess::ExitStatus) { decoderDone = true; });
    }

    FfmpegAudioBackend *owner;
    PcmSink *sink;
    QString ffmpegProgram;
    QTimer timer;
    QProcess process;

    QString path; // the file loaded
    AudioInfo info; // its audio properties, from the tag reader
    bool loaded = false; // a file is loaded and not yet ended or stopped
    bool playing = false; // the sink is started and ffmpeg feeds it
    bool decoderDone = false; // ffmpeg has exited
    PcmFormat format;
    QByteArray pending; // decoded bytes the sink has not taken yet
    qint64 startMs = 0; // where the current decode started
    qint64 handedBytes = 0; // bytes given to the sink since startMs
    qint64 reportedMs = -1; // last positionChanged value; -1 after a load or seek
    int generation = 0; // counts loads and stops; late callbacks check it

    // AE-POSITION: where playback started, plus what the sink has been given.
    qint64 positionMs() const
    {
        const qint64 bytesPerFrame = format.bytesPerFrame();
        return startMs + (handedBytes / bytesPerFrame) * 1000 / format.sampleRate;
    }

    // Stops everything at once and forgets the file.
    void halt()
    {
        ++generation;
        timer.stop();
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000); // the one short wait: a child being killed
        }
        if (playing)
            sink->stop();
        playing = false;
        decoderDone = false;
        loaded = false;
        pending.clear();
    }

    void pump()
    {
        if (!playing)
            return;
        const qint64 bytesPerFrame = format.bytesPerFrame();
        qint64 room = sink->bytesFree();
        room -= room % bytesPerFrame;
        if (room > 0 && pending.size() < room)
            pending += process.read(room - pending.size());
        // Only whole frames go to the sink; a remainder waits for the next round.
        qint64 take = std::min<qint64>(room, pending.size());
        take -= take % bytesPerFrame;
        if (take > 0) {
            const qint64 wrote = sink->write(pending.constData(), take);
            if (wrote > 0) {
                pending.remove(0, wrote);
                handedBytes += wrote;
                const qint64 ms = positionMs();
                if (reportedMs < 0 || ms - reportedMs >= kPositionStepMs) {
                    reportedMs = ms;
                    emit owner->positionChanged(ms);
                }
            }
        }
        // AE-END: all decoded audio is with the sink and the sink has played it.
        if (decoderDone && process.bytesAvailable() == 0 && pending.size() < bytesPerFrame
            && sink->idle())
            finish();
    }

    void finish()
    {
        timer.stop();
        const qint64 ms = positionMs();
        if (ms > reportedMs) {
            reportedMs = ms;
            emit owner->positionChanged(ms);
        }
        sink->stop();
        playing = false;
        loaded = false; // the file is let go
        pending.clear();
        emit owner->ended();
    }
};

FfmpegAudioBackend::FfmpegAudioBackend(PcmSink *sink, QObject *parent, const QString &ffmpegProgram)
    : AudioBackend(parent)
    , d(new Private(this, sink, ffmpegProgram))
{
}

FfmpegAudioBackend::~FfmpegAudioBackend()
{
    d->halt();
    delete d;
}

void FfmpegAudioBackend::load(const QString &path)
{
    d->halt();
    const ReadResult result = readTags(path, ReadOptions { false, false });
    d->path = path;
    d->info = result.tags.audio;
    d->loaded = true;
    d->startMs = 0;
    d->handedBytes = 0;
    d->reportedMs = -1;
    if (result.status == ReadResult::Status::Ok) {
        // AE-ASYNC: reported from the event loop, and only while this file is still the one loaded.
        Private *p = d;
        const int generation = d->generation;
        const qint64 ms = result.tags.audio.durationMs;
        QTimer::singleShot(0, this, [p, generation, ms] {
            if (p->generation == generation)
                emit p->owner->durationChanged(ms);
        });
    }
}

void FfmpegAudioBackend::play()
{
    if (!d->loaded || d->playing)
        return;
    d->format = playbackFormat(d->info);
    if (!d->sink->start(d->format))
        return;
    d->playing = true;
    d->decoderDone = false;
    d->pending.clear();
    d->process.start(d->ffmpegProgram, decodeArguments(d->path, d->startMs, d->format));
    d->timer.start();
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
