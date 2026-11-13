// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/audio-engine.md (AE-x). ffmpeg
// really decodes the fixtures of plan 03 and those in tests/data/playback; the sink is a bucket that
// counts what it is given, so no audio device is involved and nothing is
// heard. Lengths are judged by counting samples, not by a clock.
#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <cstring>

#include "playback/audioengine.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QString tone()
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/playback/tone.flac"); // 1 s, 440 Hz, 44100 Hz stereo
}

QString hires()
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/playback/hires.flac"); // 0.5 s, 96000 Hz
}

// Once an engine has shown no sign of life at all, later waits are cut short,
// so that a build with nothing behind it fails in seconds and not in minutes.
bool gNoSignOfLife = false;
int patience(int ms)
{
    return gNoSignOfLife ? 60 : ms;
}

// A sink that counts. By default it takes whatever it is offered at once.
// With `pace` set it behaves like a device: it holds at most `room` bytes
// and plays them away at `pace` bytes a second.
class Bucket : public PcmSink {
public:
    QList<PcmFormat> started;
    int stops = 0;
    bool running = false;
    bool paused = false;
    qint64 bytes = 0; // since the last start
    qint64 allBytes = 0;
    float peak = 0;
    qint64 room = 1 << 20;
    double pace = 0; // bytes a second; 0: at once
    bool refuse = false; // start() says no
    PcmFormat onlyAccept { 0, 0 }; // when set, every other format is refused
    bool busy = false; // idle() says no
    int writesWhilePaused = 0;
    int oddWrites = 0; // writes that were not whole frames

    // Four times as fast as real 44100 Hz stereo, so the tests do not crawl.
    void likeADevice(qint64 capacity)
    {
        room = capacity;
        pace = 4 * 352800.0;
    }
    bool start(const PcmFormat &format) override
    {
        if (refuse || (onlyAccept.sampleRate && !(format == onlyAccept)))
            return false;
        started.append(format);
        running = true;
        paused = false;
        bytes = 0;
        held = 0;
        clock.start();
        return true;
    }
    void stop() override
    {
        ++stops;
        running = false;
        held = 0;
    }
    void setPaused(bool p) override
    {
        settle();
        paused = p;
    }
    qint64 bytesFree() const override
    {
        if (!running || paused)
            return 0;
        settle();
        return pace > 0 ? qMax<qint64>(0, room - qint64(held)) : room;
    }
    qint64 write(const char *data, qint64 size) override
    {
        if (!running)
            return 0;
        if (paused)
            ++writesWhilePaused;
        const PcmFormat f = started.last();
        if (size % f.bytesPerFrame())
            ++oddWrites;
        settle();
        const qint64 take = qMin(size, pace > 0 ? qMax<qint64>(0, room - qint64(held)) : room);
        for (qint64 i = 0; i + 4 <= take; i += 4) {
            float v;
            std::memcpy(&v, data + i, 4);
            peak = qMax(peak, std::fabs(v));
        }
        bytes += take;
        allBytes += take;
        if (pace > 0)
            held += double(take);
        return take;
    }
    bool idle() const override
    {
        settle();
        return !busy && (pace <= 0 || held < 1.0);
    }
    qint64 frames() const { return started.isEmpty() ? 0 : bytes / started.last().bytesPerFrame(); }

private:
    // Plays away what the time since the last look allows.
    void settle() const
    {
        const double seconds = double(clock.nsecsElapsed()) / 1e9;
        clock.restart();
        if (running && !paused && pace > 0)
            held = qMax(0.0, held - seconds * pace);
    }
    mutable QElapsedTimer clock;
    mutable double held = 0; // bytes taken and not yet played
};

struct Rig {
    Bucket bucket;
    FfmpegAudioBackend engine;
    QSignalSpy position;
    QSignalSpy duration;
    QSignalSpy ended;
    QSignalSpy failed;
    explicit Rig(const QString &program = QStringLiteral("ffmpeg"))
        : engine(&bucket, nullptr, program)
        , position(&engine, &AudioBackend::positionChanged)
        , duration(&engine, &AudioBackend::durationChanged)
        , ended(&engine, &AudioBackend::ended)
        , failed(&engine, &AudioBackend::failed)
    {
    }
    // Waits for a signal; notes it when the engine has never said or done anything.
    bool waitFor(QSignalSpy &spy, int ms = 15000)
    {
        const bool ok = spy.wait(patience(ms));
        if (!ok && position.isEmpty() && duration.isEmpty() && ended.isEmpty() && failed.isEmpty() && bucket.started.isEmpty())
            gNoSignOfLife = true;
        return ok;
    }
    bool playToEnd(const QString &path)
    {
        engine.load(path);
        engine.play();
        return waitFor(ended);
    }
    qint64 lastPosition() const { return position.isEmpty() ? -1 : position.last().at(0).toLongLong(); }
};

}

class AudioEngineTest : public QObject {
    Q_OBJECT
private slots:
    void format();
    void arguments();
    void playsToTheEnd();
    void nothingFromInsideACall();
    void positionsRiseToTheEnd();
    void keepsTheSourceRate();
    void everyFormat_data();
    void everyFormat();
    void volume();
    void wholeFramesOnly();
    void pauseAndResume();
    void seek();
    void seekBeforePlay();
    void stopLetsGo();
    void loadReplaces();
    void waitsForTheSink();
    void sinkRefusesFormat();
    void noDevice();
    void badFiles_data();
    void badFiles();
    void noFfmpeg();
    void destroyedWhilePlaying();
    void longFileStaysSmall();
};

void AudioEngineTest::format()
{
    // AE-FORMAT
    AudioInfo info;
    info.sampleRateHz = 44100;
    info.channels = 2;
    QVERIFY((playbackFormat(info) == PcmFormat { 44100, 2 }));
    info.sampleRateHz = 96000;
    QVERIFY((playbackFormat(info) == PcmFormat { 96000, 2 }));
    info.channels = 1;
    QVERIFY((playbackFormat(info) == PcmFormat { 96000, 1 }));
    info.channels = 6; // more than two channels are mixed down to two
    QVERIFY((playbackFormat(info) == PcmFormat { 96000, 2 }));
    info.channels = 0;
    QVERIFY((playbackFormat(info) == PcmFormat { 96000, 2 }));
    info.sampleRateHz = 192000;
    QCOMPARE(playbackFormat(info).sampleRate, 192000);
    info.sampleRateHz = 8000;
    QCOMPARE(playbackFormat(info).sampleRate, 8000);
    for (int odd : { 0, -1, 7999, 192001, 2822400 }) {
        info.sampleRateHz = odd;
        QCOMPARE(playbackFormat(info).sampleRate, 44100);
    }
    QCOMPARE((PcmFormat { 48000, 2 }).bytesPerFrame(), 8);
    QCOMPARE((PcmFormat { 48000, 1 }).bytesPerFrame(), 4);
}

void AudioEngineTest::arguments()
{
    // AE-ARGS
    QCOMPARE(decodeArguments(s(u"/m/a b.flac"), 0, { 44100, 2 }),
        QStringList({ s(u"-v"), s(u"error"), s(u"-nostdin"), s(u"-i"), s(u"/m/a b.flac"), s(u"-map"), s(u"0:a:0"), s(u"-vn"),
            s(u"-sn"), s(u"-dn"), s(u"-f"), s(u"f32le"), s(u"-acodec"), s(u"pcm_f32le"), s(u"-ar"), s(u"44100"), s(u"-ac"),
            s(u"2"), s(u"pipe:1") }));
    // A start time goes in front of the input, in seconds with three decimals.
    const QStringList later = decodeArguments(s(u"/m/x.mp3"), 83456, { 96000, 1 });
    QCOMPARE(later.mid(0, 7), QStringList({ s(u"-v"), s(u"error"), s(u"-nostdin"), s(u"-ss"), s(u"83.456"), s(u"-i"), s(u"/m/x.mp3") }));
    QCOMPARE(later.mid(later.size() - 5), QStringList({ s(u"-ar"), s(u"96000"), s(u"-ac"), s(u"1"), s(u"pipe:1") }));
    QCOMPARE(decodeArguments(s(u"/m/x.mp3"), 500, { 44100, 2 }).at(4), s(u"0.500"));
    QVERIFY(!decodeArguments(s(u"/m/x.mp3"), -9, { 44100, 2 }).contains(s(u"-ss")));
    // A name that looks like an option stays one argument, right after -i.
    const QStringList odd = decodeArguments(s(u"-weird name.flac"), 0, { 44100, 2 });
    QCOMPARE(odd.at(odd.indexOf(s(u"-i")) + 1), s(u"-weird name.flac"));
}

void AudioEngineTest::playsToTheEnd()
{
    // AE-PLAY
    Rig r;
    QVERIFY(r.playToEnd(tone()));
    QCOMPARE(r.ended.size(), 1);
    QCOMPARE(r.failed.size(), 0);
    QCOMPARE(r.bucket.started.size(), 1);
    QVERIFY((r.bucket.started.first() == PcmFormat { 44100, 2 }));
    QCOMPARE(r.bucket.frames(), 44100); // one second, to the sample
    QVERIFY2(r.bucket.peak > 0.05f && r.bucket.peak <= 1.0f, qPrintable(QString::number(r.bucket.peak)));
    // The duration is known before any sound.
    QVERIFY(r.duration.size() >= 1);
    QCOMPARE(r.duration.first().at(0).toLongLong(), 1000);
    // At the end the sink has been let go.
    QVERIFY(!r.bucket.running);
    QCOMPARE(r.bucket.writesWhilePaused, 0);
    // The same engine plays the next file.
    QVERIFY(r.playToEnd(hires()));
    QCOMPARE(r.ended.size(), 2);
}

void AudioEngineTest::nothingFromInsideACall()
{
    // AE-ASYNC: no signal is emitted from inside a call to the engine.
    Rig r;
    r.engine.load(tone());
    r.engine.setVolume(0.5);
    r.engine.play();
    r.engine.seek(100);
    r.engine.pause();
    r.engine.play();
    QCOMPARE(r.position.size() + r.duration.size() + r.ended.size() + r.failed.size(), 0);
    r.engine.stop();
    r.engine.load(s(u"/no/such/file.flac"));
    r.engine.play();
    QCOMPARE(r.failed.size(), 0);
    QVERIFY(r.waitFor(r.failed, 5000));
}

void AudioEngineTest::positionsRiseToTheEnd()
{
    // AE-POSITION
    Rig r;
    r.bucket.likeADevice(16384);
    QVERIFY(r.playToEnd(tone()));
    QVERIFY2(r.position.size() >= 5, qPrintable(QString::number(r.position.size())));
    qint64 last = -1;
    for (const auto &e : r.position) {
        const qint64 ms = e.at(0).toLongLong();
        QVERIFY2(ms > last, qPrintable(QStringLiteral("%1 after %2").arg(ms).arg(last)));
        QVERIFY(ms >= 0 && ms <= 1000);
        last = ms;
    }
    QCOMPARE(last, 1000); // the last report is the end
    // Not a report for every scrap of audio either: at most one per 100 ms of music, and the last.
    QVERIFY2(r.position.size() <= 12, qPrintable(QString::number(r.position.size())));
}

void AudioEngineTest::keepsTheSourceRate()
{
    // AE-FORMAT: a 96 kHz file is played at 96 kHz.
    Rig r;
    QVERIFY(r.playToEnd(hires()));
    QVERIFY((r.bucket.started.first() == PcmFormat { 96000, 2 }));
    QCOMPARE(r.bucket.frames(), 48000);
    QCOMPARE(r.duration.first().at(0).toLongLong(), 500);
    QCOMPARE(r.lastPosition(), 500);
}

void AudioEngineTest::everyFormat_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("minFrames");
    QTest::addColumn<int>("maxFrames");
    // The plan-03 fixtures: a quarter of a second of silence, 44100 Hz stereo.
    // Lossy formats pad a little at the edges.
    QTest::newRow("flac") << s(u"tags/basic.flac") << 11025 << 11025;
    QTest::newRow("wav") << s(u"tags/basic.wav") << 11025 << 11025;
    QTest::newRow("wavpack") << s(u"tags/basic.wv") << 11025 << 11025;
    QTest::newRow("mp3") << s(u"tags/basic.mp3") << 11025 << 14000;
    QTest::newRow("ogg") << s(u"tags/basic.ogg") << 10500 << 12500;
    QTest::newRow("opus") << s(u"tags/basic.opus") << 11025 << 13500;
    QTest::newRow("m4a") << s(u"tags/basic.m4a") << 11025 << 13500;
    QTest::newRow("mp3 320") << s(u"playback/tone.mp3") << 44100 << 47000;
}

void AudioEngineTest::everyFormat()
{
    QFETCH(QString, file);
    QFETCH(int, minFrames);
    QFETCH(int, maxFrames);
    Rig r;
    QVERIFY(r.playToEnd(QStringLiteral(LINUXBEE_TEST_DATA "/") + file));
    QCOMPARE(r.failed.size(), 0);
    const int rate = r.bucket.started.first().sampleRate;
    const qint64 frames = r.bucket.frames() * 44100 / rate; // Opus is decoded at 48000
    QVERIFY2(frames >= minFrames && frames <= maxFrames, qPrintable(QString::number(frames)));
}

void AudioEngineTest::volume()
{
    // AE-VOLUME
    Rig full;
    QVERIFY(full.playToEnd(tone()));
    const float peak = full.bucket.peak;
    Rig half;
    half.engine.setVolume(0.5);
    QVERIFY(half.playToEnd(tone()));
    QVERIFY2(std::fabs(half.bucket.peak - peak / 2) < peak * 0.01f, qPrintable(QString::number(half.bucket.peak)));
    Rig silent;
    silent.engine.load(tone());
    silent.engine.setVolume(0.0); // between load and play, as the player does it
    silent.engine.play();
    QVERIFY(silent.waitFor(silent.ended, 15000));
    QCOMPARE(silent.bucket.peak, 0.0f);
    QCOMPARE(silent.bucket.frames(), 44100); // silence is still played
    Rig loud;
    loud.engine.setVolume(3.0); // held at 1: the engine never amplifies
    QVERIFY(loud.playToEnd(tone()));
    QVERIFY(std::fabs(loud.bucket.peak - peak) < peak * 0.01f);
}

void AudioEngineTest::wholeFramesOnly()
{
    // AE-PLAY: the sink is only ever given whole frames, however awkward its appetite.
    Rig r;
    r.bucket.room = 1001; // not a multiple of 8
    r.engine.load(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"));
    r.engine.play();
    QVERIFY(r.waitFor(r.ended, 30000));
    QCOMPARE(r.bucket.oddWrites, 0);
    QCOMPARE(r.bucket.frames(), 11025);
}

void AudioEngineTest::pauseAndResume()
{
    // AE-PAUSE
    Rig r;
    r.bucket.likeADevice(8192);
    r.engine.load(tone());
    r.engine.play();
    QTRY_VERIFY_WITH_TIMEOUT(r.bucket.bytes > 40000, patience(10000));
    r.engine.pause();
    QVERIFY(r.bucket.paused); // the sink is told
    const qint64 held = r.bucket.bytes;
    const qsizetype reports = r.position.size();
    QTest::qWait(250);
    QCOMPARE(r.bucket.bytes, held); // nothing moves while paused
    QCOMPARE(r.position.size(), reports);
    QCOMPARE(r.ended.size(), 0);
    r.engine.pause(); // again: nothing
    r.engine.play();
    QVERIFY(!r.bucket.paused);
    QVERIFY(r.waitFor(r.ended, 15000));
    QCOMPARE(r.bucket.frames(), 44100); // not a sample lost or repeated
    QCOMPARE(r.bucket.started.size(), 1); // the same sink session throughout
    QCOMPARE(r.bucket.writesWhilePaused, 0);
}

void AudioEngineTest::seek()
{
    // AE-SEEK
    Rig r;
    r.bucket.likeADevice(8192);
    r.engine.load(tone());
    r.engine.play();
    QTRY_VERIFY_WITH_TIMEOUT(r.bucket.bytes > 20000, patience(10000));
    r.position.clear();
    r.engine.seek(600);
    QVERIFY(r.waitFor(r.ended, 15000));
    // What was buffered is dropped and the sink starts afresh: 0.4 s follow.
    QCOMPARE(r.bucket.started.size(), 2);
    QVERIFY2(qAbs(r.bucket.frames() - 17640) <= 64, qPrintable(QString::number(r.bucket.frames())));
    QVERIFY(r.position.size() >= 2);
    QVERIFY(r.position.first().at(0).toLongLong() >= 600); // positions go on from the target
    QVERIFY(qAbs(r.lastPosition() - 1000) <= 2);
    // Seeking while paused stays paused, and resumes from there.
    Rig p;
    p.engine.load(tone());
    p.engine.play();
    p.engine.pause();
    p.engine.seek(900);
    QTest::qWait(200);
    QCOMPARE(p.ended.size(), 0);
    QCOMPARE(p.bucket.bytes, 0);
    p.engine.play();
    QVERIFY(p.waitFor(p.ended, 15000));
    QVERIFY2(qAbs(p.bucket.frames() - 4410) <= 64, qPrintable(QString::number(p.bucket.frames())));
    // Beyond the end is the end; below zero is the start.
    Rig e;
    e.engine.load(tone());
    e.engine.seek(999999);
    e.engine.play();
    QVERIFY(e.waitFor(e.ended, 15000));
    QVERIFY(e.bucket.frames() <= 64);
    QCOMPARE(e.failed.size(), 0); // an end, not an error
    Rig z;
    z.engine.load(tone());
    z.engine.seek(-50);
    z.engine.play();
    QVERIFY(z.waitFor(z.ended, 15000));
    QCOMPARE(z.bucket.frames(), 44100);
}

void AudioEngineTest::seekBeforePlay()
{
    Rig r;
    r.engine.load(tone());
    r.engine.seek(750);
    QTest::qWait(150);
    QCOMPARE(r.bucket.started.size(), 0); // a seek alone starts nothing
    r.engine.play();
    QVERIFY(r.waitFor(r.ended, 15000));
    QVERIFY2(qAbs(r.bucket.frames() - 11025) <= 64, qPrintable(QString::number(r.bucket.frames())));
    // A seek with nothing loaded does nothing at all.
    Rig none;
    none.engine.seek(100);
    none.engine.play();
    none.engine.pause();
    QTest::qWait(100);
    QCOMPARE(none.bucket.started.size(), 0);
    QCOMPARE(none.failed.size() + none.ended.size() + none.position.size(), 0);
}

void AudioEngineTest::stopLetsGo()
{
    // AE-STOP
    Rig r;
    r.bucket.likeADevice(4096);
    r.engine.load(tone());
    r.engine.play();
    QTRY_VERIFY_WITH_TIMEOUT(r.bucket.bytes > 20000, patience(10000));
    r.engine.stop();
    QVERIFY(!r.bucket.running);
    const qint64 held = r.bucket.allBytes;
    r.position.clear();
    QTest::qWait(250);
    QCOMPARE(r.bucket.allBytes, held);
    QCOMPARE(r.position.size(), 0); // and nothing more is said about that file
    QCOMPARE(r.ended.size(), 0);
    QCOMPARE(r.failed.size(), 0);
    r.engine.play(); // nothing is loaded any more
    r.engine.seek(10);
    QTest::qWait(100);
    QCOMPARE(r.bucket.allBytes, held);
    r.engine.stop(); // again: nothing
    // The file is free: it can be replaced while the engine lives on.
    QTemporaryDir dir;
    const QString copy = dir.path() + s(u"/t.flac");
    QVERIFY(QFile::copy(tone(), copy));
    r.engine.load(copy);
    r.engine.play();
    QTRY_VERIFY_WITH_TIMEOUT(r.bucket.bytes > 4000, patience(10000));
    r.engine.stop();
    QVERIFY(QFile::remove(copy));
}

void AudioEngineTest::loadReplaces()
{
    // AE-LOAD: loading stops what was playing; nothing of the old file is reported afterwards.
    Rig r;
    r.bucket.likeADevice(4096);
    r.engine.load(tone());
    r.engine.play();
    QTRY_VERIFY_WITH_TIMEOUT(r.bucket.bytes > 20000, patience(10000));
    r.engine.load(hires());
    r.position.clear();
    r.duration.clear();
    QTest::qWait(150);
    QCOMPARE(r.position.size(), 0); // loaded, not started
    QCOMPARE(r.duration.size(), 1);
    QCOMPARE(r.duration.first().at(0).toLongLong(), 500);
    r.bucket.pace = 0;
    r.bucket.room = 1 << 20;
    r.engine.play();
    QVERIFY(r.waitFor(r.ended, 15000));
    QCOMPARE(r.ended.size(), 1);
    QVERIFY((r.bucket.started.last() == PcmFormat { 96000, 2 }));
    QCOMPARE(r.bucket.frames(), 48000);
    QCOMPARE(r.lastPosition(), 500);
    // A file that failed to load is forgotten by the next load.
    r.engine.load(s(u"/no/such.flac"));
    r.engine.load(tone());
    r.engine.play();
    QVERIFY(r.waitFor(r.ended, 15000));
    QCOMPARE(r.failed.size(), 0);
}

void AudioEngineTest::waitsForTheSink()
{
    // AE-END: the end is announced when the sink has played what it was given, not before.
    Rig r;
    r.bucket.busy = true;
    r.engine.load(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"));
    r.engine.play();
    QTRY_COMPARE_WITH_TIMEOUT(r.bucket.frames(), 11025, patience(10000));
    QTest::qWait(200);
    QCOMPARE(r.ended.size(), 0);
    QVERIFY(r.bucket.running);
    r.bucket.busy = false;
    QVERIFY(r.waitFor(r.ended, 5000));
}

void AudioEngineTest::sinkRefusesFormat()
{
    // AE-FORMAT: a device that cannot do the file's own format gets 48000 Hz stereo.
    Rig r;
    r.bucket.onlyAccept = { 48000, 2 };
    QVERIFY(r.playToEnd(hires()));
    QCOMPARE(r.failed.size(), 0);
    QVERIFY((r.bucket.started.first() == PcmFormat { 48000, 2 }));
    QCOMPARE(r.bucket.frames(), 24000); // resampled: half a second at 48000
    QCOMPARE(r.lastPosition(), 500);
}

void AudioEngineTest::noDevice()
{
    Rig r;
    r.bucket.refuse = true;
    r.engine.load(tone());
    r.engine.play();
    QVERIFY(r.waitFor(r.failed, 5000));
    QCOMPARE(r.failed.first().at(0).toString(), s(u"No audio device could be opened."));
    QCOMPARE(r.ended.size(), 0);
    QCOMPARE(r.bucket.allBytes, 0);
    // The device comes back: the next file plays.
    r.bucket.refuse = false;
    QVERIFY(r.playToEnd(tone()));
}

void AudioEngineTest::badFiles_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("message");
    QTest::newRow("missing") << s(u"/no/such/file.flac") << s(u"The file was not found.");
    QTest::newRow("not audio") << QStringLiteral(LINUXBEE_TEST_DATA "/tags/notaudio.mp3") << s(u"The file is not a music file.");
    QTest::newRow("empty") << QStringLiteral(LINUXBEE_TEST_DATA "/tags/zero.mp3") << s(u"The file is not a music file.");
    QTest::newRow("a folder") << QStringLiteral(LINUXBEE_TEST_DATA "/tags") << QString();
}

void AudioEngineTest::badFiles()
{
    // AE-FAIL
    QFETCH(QString, file);
    QFETCH(QString, message);
    Rig r;
    r.engine.load(file);
    QTest::qWait(50);
    QCOMPARE(r.failed.size(), 0); // a file is not judged until it is played
    r.engine.play();
    QVERIFY(r.waitFor(r.failed, 10000));
    QCOMPARE(r.failed.size(), 1);
    if (!message.isEmpty())
        QCOMPARE(r.failed.first().at(0).toString(), message);
    QVERIFY(!r.failed.first().at(0).toString().trimmed().isEmpty());
    QCOMPARE(r.ended.size(), 0);
    QVERIFY(!r.bucket.running);
    QTest::qWait(100);
    QCOMPARE(r.failed.size(), 1); // said once
    // And the engine is fit for the next file.
    QVERIFY(r.playToEnd(tone()));
    QCOMPARE(r.failed.size(), 1);
}

void AudioEngineTest::noFfmpeg()
{
    Rig r(s(u"/no/such/ffmpeg-program"));
    r.engine.load(tone());
    r.engine.play();
    QVERIFY(r.waitFor(r.failed, 10000));
    QCOMPARE(r.failed.first().at(0).toString(), s(u"ffmpeg was not found."));
    QCOMPARE(r.ended.size(), 0);
    QVERIFY(!r.bucket.running);
}

void AudioEngineTest::destroyedWhilePlaying()
{
    // Nothing is left behind: the sink is stopped and no ffmpeg lingers.
    Bucket bucket;
    bucket.likeADevice(2048);
    {
        FfmpegAudioBackend engine(&bucket);
        engine.load(tone());
        engine.play();
        QTRY_VERIFY_WITH_TIMEOUT(bucket.bytes > 4000, patience(10000));
    }
    QVERIFY(!bucket.running);
    const qint64 held = bucket.allBytes;
    QTest::qWait(150);
    QCOMPARE(bucket.allBytes, held);
}

void AudioEngineTest::longFileStaysSmall()
{
    // AE-MEMORY: the engine decodes a little ahead of what is heard, not the whole file.
    // Twenty minutes of silence are tiny on disk and 400 MB once decoded.
    QTemporaryDir dir;
    const QString path = dir.path() + s(u"/long.flac");
    QProcess make;
    make.start(s(u"ffmpeg"), { s(u"-v"), s(u"error"), s(u"-f"), s(u"lavfi"), s(u"-i"), s(u"anullsrc=r=44100:cl=stereo"), s(u"-t"),
                                 s(u"1200"), path });
    QVERIFY(make.waitForFinished(120000));
    QVERIFY(QFileInfo(path).size() > 0);
    auto residentMb = [] {
        QFile statm(s(u"/proc/self/statm"));
        if (!statm.open(QIODevice::ReadOnly))
            return 0.0;
        return QString::fromLatin1(statm.readAll()).section(QLatin1Char(' '), 1, 1).toDouble() * 4096.0 / 1048576.0;
    };
    Rig r;
    r.bucket.likeADevice(4096);
    const double before = residentMb();
    r.engine.load(path);
    r.engine.play();
    QTRY_VERIFY_WITH_TIMEOUT(r.bucket.bytes > 30000, patience(10000));
    QTest::qWait(gNoSignOfLife ? 10 : 5000); // time enough to decode all of it, if nothing held it back
    const double grown = residentMb() - before;
    QVERIFY2(grown < 24.0, qPrintable(QStringLiteral("grew by %1 MB").arg(grown)));
    QCOMPARE(r.duration.first().at(0).toLongLong(), 1200000);
    // A seek far in still lands, and the engine shuts down cleanly from this state.
    r.bucket.pace = 0;
    r.bucket.room = 1 << 20;
    r.engine.seek(1199000);
    QVERIFY(r.waitFor(r.ended, 20000));
    QVERIFY2(qAbs(r.bucket.frames() - 44100) <= 64, qPrintable(QString::number(r.bucket.frames())));
}

QTEST_GUILESS_MAIN(AudioEngineTest)
#include "tst_audioengine.moc"
