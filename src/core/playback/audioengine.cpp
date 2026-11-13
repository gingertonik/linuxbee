#include "playback/audioengine.h"

#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <cstring>

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
    bool paused = false; // playing, but the sink has been told to pause
    bool decoderDone = false; // ffmpeg has exited
    double volume = 1.0; // AE-VOLUME: applied to samples as they are handed over
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

    // Ends ffmpeg and drops what it had written. Emits nothing.
    void stopDecoder()
    {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000); // the one short wait: a child being killed
        }
        process.readAll(); // whatever it wrote before it died
        decoderDone = false;
    }

    // Stops everything at once and forgets the file. Emits nothing.
    void halt()
    {
        ++generation;
        timer.stop();
        stopDecoder();
        if (playing)
            sink->stop();
        playing = false;
        paused = false;
        loaded = false;
        pending.clear();
    }

    // AE-VOLUME: the samples handed over are scaled on the way out. What the sink
    // does not take stays unscaled in pending, so it is scaled once, at the volume
    // in force when it is handed over.
    qint64 writeToSink(qint64 bytes)
    {
        if (volume >= 1.0)
            return sink->write(pending.constData(), bytes);
        QByteArray scaled(pending.constData(), bytes);
        for (qint64 i = 0; i + 4 <= bytes; i += 4) {
            float v = 0;
            std::memcpy(&v, scaled.constData() + i, 4);
            v *= static_cast<float>(volume);
            std::memcpy(scaled.data() + i, &v, 4);
        }
        return sink->write(scaled.constData(), bytes);
    }

    void pump()
    {
        if (!playing || paused)
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
            const qint64 wrote = writeToSink(take);
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
        const qint64 ms = positionMs();
        halt();
        if (ms > reportedMs) {
            reportedMs = ms;
            emit owner->positionChanged(ms);
        }
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
    if (!d->loaded)
        return;
    if (d->playing) {
        if (d->paused) {
            d->paused = false;
            d->sink->setPaused(false);
            d->timer.start();
        }
        return;
    }
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
    if (!d->playing || d->paused)
        return;
    d->paused = true;
    d->timer.stop();
    d->sink->setPaused(true);
}

void FfmpegAudioBackend::stop()
{
    d->halt();
}

void FfmpegAudioBackend::seek(qint64 ms)
{
    if (!d->loaded)
        return;
    d->startMs = std::clamp<qint64>(ms, 0, d->info.durationMs);
    d->handedBytes = 0;
    d->reportedMs = -1;
    if (!d->playing)
        return;
    // Start afresh: the sink drops what it buffered and ffmpeg begins at the target.
    const bool wasPaused = d->paused;
    d->timer.stop();
    d->stopDecoder();
    d->sink->stop();
    d->pending.clear();
    if (!d->sink->start(d->format)) {
        d->halt();
        return;
    }
    if (wasPaused)
        d->sink->setPaused(true);
    d->process.start(d->ffmpegProgram, decodeArguments(d->path, d->startMs, d->format));
    if (!wasPaused)
        d->timer.start();
}

void FfmpegAudioBackend::setVolume(double linear)
{
    d->volume = qBound(0.0, linear, 1.0);
}

}
