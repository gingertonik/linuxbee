#include "playback/audioengine.h"

#include <QProcess>
#include <QTimer>

#include <signal.h>
#include <sys/types.h>

#include <algorithm>
#include <cstring>

namespace linuxbee {

namespace {

// AE-POSITION: positionChanged comes at most once per 100 ms of music.
constexpr qint64 kPositionStepMs = 100;
// How often the engine moves decoded samples into the sink and looks at the decoder.
constexpr int kPumpIntervalMs = 10;
// AE-MEMORY: ffmpeg is held back while more than the high mark waits in the program,
// and let go again once less than the low mark is left.
constexpr qint64 kHighWaterBytes = 4 << 20;
constexpr qint64 kLowWaterBytes = 1 << 20;
// The end of ffmpeg's error output is kept for a failure message.
constexpr qsizetype kErrorTextLimit = 8192;

}

struct FfmpegAudioBackend::Private {
    Private(FfmpegAudioBackend *o, PcmSink *s, const QString &program)
        : owner(o)
        , sink(s)
        , ffmpegProgram(program)
    {
        timer.setInterval(kPumpIntervalMs);
        QObject::connect(&timer, &QTimer::timeout, owner, [this] { pump(); });
        QObject::connect(
            &process, &QProcess::readyReadStandardError, owner, [this] { appendErrorOutput(); });
        QObject::connect(
            &process, &QProcess::finished, owner, [this](int code, QProcess::ExitStatus status) {
                decoderDone = true;
                appendErrorOutput();
                if (!killing && (status != QProcess::NormalExit || code != 0))
                    decodeFailed = true;
            });
        // Can be raised while play() is still running, so it only schedules the report.
        QObject::connect(
            &process, &QProcess::errorOccurred, owner, [this](QProcess::ProcessError error) {
                if (!killing && error == QProcess::FailedToStart)
                    failLater(QStringLiteral("ffmpeg was not found."));
            });
    }

    FfmpegAudioBackend *owner;
    PcmSink *sink;
    QString ffmpegProgram;
    QTimer timer;
    QProcess process;

    QString path; // the file loaded
    AudioInfo info; // its audio properties, from the tag reader
    QString openError; // AE-FAIL: why the file cannot be played, empty when it can
    bool loaded = false; // a file is loaded and not yet ended, failed or stopped
    bool playing = false; // the sink is started and ffmpeg feeds it
    bool paused = false; // playing, but the sink has been told to pause
    bool decoderDone = false; // ffmpeg has exited
    bool decodeFailed = false; // ffmpeg exited with an error
    bool killing = false; // this engine is ending ffmpeg itself; its exit is no failure
    bool held = false; // AE-MEMORY: ffmpeg is stopped with SIGSTOP
    double volume = 1.0; // AE-VOLUME: applied to samples as they are handed over
    PcmFormat format;
    QByteArray pending; // decoded bytes the sink has not taken yet
    QByteArray errorOutput; // the end of what ffmpeg wrote to its error output
    qint64 startMs = 0; // where the current decode started
    qint64 handedBytes = 0; // bytes given to the sink since startMs
    qint64 reportedMs = -1; // last positionChanged value; -1 after a load or seek
    int generation = 0; // counts loads, stops and failures; late callbacks check it

    // AE-POSITION: where playback started, plus what the sink has been given.
    qint64 positionMs() const
    {
        const qint64 bytesPerFrame = format.bytesPerFrame();
        return startMs + (handedBytes / bytesPerFrame) * 1000 / format.sampleRate;
    }

    // A pid of 0 or less would reach a whole process group, so it is never signalled.
    void signalDecoder(int signal)
    {
        const qint64 pid = process.processId();
        if (pid > 0)
            ::kill(static_cast<pid_t>(pid), signal);
    }

    void appendErrorOutput()
    {
        errorOutput += process.readAllStandardError();
        if (errorOutput.size() > kErrorTextLimit)
            errorOutput.remove(0, errorOutput.size() - kErrorTextLimit);
    }

    // AE-FAIL: the last line ffmpeg wrote, or the default when it wrote none.
    QString decodeFailureMessage() const
    {
        const QStringList lines = QString::fromUtf8(errorOutput).split(QLatin1Char('\n'));
        for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
            const QString line = it->trimmed();
            if (!line.isEmpty())
                return line;
        }
        return QStringLiteral("The file cannot be played.");
    }

    // Ends ffmpeg and drops what it had written. Emits nothing.
    void stopDecoder()
    {
        if (process.state() != QProcess::NotRunning) {
            killing = true;
            if (held)
                signalDecoder(SIGCONT); // a held child is resumed before it is killed
            process.kill();
            process.waitForFinished(1000); // the one short wait: a child being killed
            killing = false;
        }
        held = false;
        process.readAll(); // whatever it wrote before it died
        process.readAllStandardError();
        errorOutput.clear();
        decoderDone = false;
        decodeFailed = false;
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

    // AE-FAIL, AE-ASYNC: the failure is reported from the event loop, and only if nothing
    // has been loaded or stopped in the meantime. The file is not played again.
    void failLater(const QString &message)
    {
        timer.stop();
        loaded = false;
        const int gen = generation;
        QTimer::singleShot(0, owner, [this, gen, message] {
            if (generation != gen)
                return;
            halt();
            emit owner->failed(message);
        });
    }

    // AE-MEMORY: hold ffmpeg back while the program has more than a few megabytes of
    // its output waiting; let it run again once the sink has drawn most of it away.
    // Runs while paused too, so a paused engine does not grow either.
    void throttleDecoder()
    {
        if (process.state() != QProcess::Running)
            return;
        const qint64 waiting = process.bytesAvailable();
        if (!held && waiting > kHighWaterBytes) {
            signalDecoder(SIGSTOP);
            held = true;
        } else if (held && waiting < kLowWaterBytes) {
            signalDecoder(SIGCONT);
            held = false;
        }
    }

    // AE-FORMAT: the file's own format, else 48000 Hz stereo, which ffmpeg is asked to resample to.
    bool startSink(PcmFormat preferred)
    {
        format = preferred;
        if (sink->start(format))
            return true;
        format = PcmFormat { 48000, 2 };
        return sink->start(format);
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
        if (!playing)
            return;
        throttleDecoder();
        if (decoderDone && decodeFailed) {
            failLater(decodeFailureMessage());
            return;
        }
        if (paused)
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
            && sink->idle()) {
            // AE-FAIL: no audio at all from the start of a file is a failure, not an end.
            if (handedBytes == 0 && startMs == 0)
                failLater(decodeFailureMessage());
            else
                finish();
        }
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
    d->openError.clear();
    if (result.status == ReadResult::Status::NotFound)
        d->openError = QStringLiteral("The file was not found.");
    else if (result.status == ReadResult::Status::NotAudio)
        d->openError = QStringLiteral("The file is not a music file.");
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
    if (!d->openError.isEmpty()) {
        d->failLater(d->openError);
        return;
    }
    if (!d->startSink(playbackFormat(d->info))) {
        d->failLater(QStringLiteral("No audio device could be opened."));
        return;
    }
    d->playing = true;
    d->decoderDone = false;
    d->decodeFailed = false;
    d->pending.clear();
    d->process.start(d->ffmpegProgram, decodeArguments(d->path, d->startMs, d->format));
    if (d->loaded) // a start that failed has already been scheduled as a failure
        d->timer.start();
}

void FfmpegAudioBackend::pause()
{
    if (!d->playing || d->paused)
        return;
    d->paused = true;
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
    d->stopDecoder();
    d->sink->stop();
    d->pending.clear();
    if (!d->startSink(d->format)) {
        d->failLater(QStringLiteral("No audio device could be opened."));
        return;
    }
    if (wasPaused)
        d->sink->setPaused(true);
    d->process.start(d->ffmpegProgram, decodeArguments(d->path, d->startMs, d->format));
}

void FfmpegAudioBackend::setVolume(double linear)
{
    d->volume = qBound(0.0, linear, 1.0);
}

}
