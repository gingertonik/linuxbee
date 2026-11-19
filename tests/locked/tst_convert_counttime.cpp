// LOCKED ACCEPTANCE TEST (the converter: the count has a time limit and answers Cancel; the
// 250 ms allowance). Written by the reviewer, not the builder. The builder makes these pass by
// changing src/, never by editing this file. A test believed to be wrong is reported, with the
// reason, and left alone.
//
// Expected values are C-REPLACE ("The count is watched") and C-WHOLE in docs/spec/convert.md. The
// faults this holds shut (the review of fix-85b, 2026-11-18): the program that counts (ffprobe)
// was waited for without end and nobody was asked meanwhile, so one that hung stopped the whole
// batch and Cancel was not answered; and no test held the 250 ms allowance of a very short file.
//
// ffprobe is looked for beside ffmpeg first, so a stand-in for each is put in a folder of its
// own. A stand-in that "hangs" gives up by itself after 20 seconds, so that a program without the
// rule fails this test and does not stop the gate. The sound is made here with ffmpeg; the last
// two cases use the real ffprobe. Everything is in a temporary folder.
#include <QElapsedTimer>
#include <QProcess>
#include <QtTest>

#include <signal.h>

#include "convert/converter.h"

using linuxbee::ConvertJob;
using linuxbee::ConvertResult;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArrayLiteral("<cannot open>");
    return file.readAll();
}

bool ffmpeg(const QStringList &arguments)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"),
        QStringList({ s(u"-nostdin"), s(u"-v"), s(u"error"), s(u"-y") }) + arguments);
    return p.waitForFinished(120000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

const char16_t *kSilent = u"the converted file could not be checked: ffprobe gave no answer";
const char16_t *kShorter = u"the converted file is shorter than the source";

// What a stand-in ffprobe does once it has written its number to `started`.
const QByteArray kHangs = "exec sleep 20\n";
const QByteArray kSomeLinesThenHangs
    = "echo sample_rate=44100\necho nb_samples=1152\necho nb_samples=1152\nexec sleep 20\n";
// Four seconds of sound, told in ten lines a tenth of a second apart: slow, and alive.
const QByteArray kSlowAndAlive = "echo sample_rate=44100\nfor i in 1 2 3 4 5 6 7 8 9 10; do sleep "
                                 "0.1; echo nb_samples=17640; done\nexit 0\n";
// The same over some two and a half seconds.
const QByteArray kSlower = "echo sample_rate=44100\nfor i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 "
                           "16 17 18 19 20 21 22 23 24 25; do sleep 0.1; "
                           "echo nb_samples=7056; done\nexit 0\n";

}

class ConvertCountTimeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void theLimitWhenNoneIsGiven();
    void aCountThatSaysNothing_data();
    void aCountThatSaysNothing();
    void aCountThatIsSlowIsNotOneThatHangs();
    void cancelIsAnsweredDuringTheCount();
    void theFunctionIsAskedDuringTheCount();
    void aVeryShortSource_data();
    void aVeryShortSource();

private:
    QString f(const char16_t *name) const { return m_dir.filePath(s(name)); }
    // A folder with an encoder that says it is half-way and writes `result`, and beside it an
    // ffprobe that does `probe`.
    // No ffprobe is put there when `probe` is empty. The path of the encoder.
    QString tools(const QString &result, const QByteArray &probe);
    ConvertJob job(const QString &encoder) const;
    qint64 probeNumber() const;
    bool probeGone() const;
    QStringList leftovers() const;
    QTemporaryDir m_dir;
    QString m_work;
    QByteArray m_old;
    int m_counter = 0;
};

void ConvertCountTimeTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY2(!QStandardPaths::findExecutable(s(u"ffprobe")).isEmpty(), "ffprobe is needed");
    QVERIFY2(ffmpeg({ s(u"-f"), s(u"lavfi"), s(u"-i"), s(u"sine=frequency=440:duration=4"),
                 s(u"-ac"), s(u"2"), f(u"four.flac") }),
        "ffmpeg is needed, with its lavfi input");
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), f(u"four.mp3") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-af"), s(u"volume=0.5"), f(u"old.mp3") }));
    // For the allowance: FLAC, where the count of samples is exact.
    for (const char *ms : { "300", "060", "040" }) {
        QVERIFY(ffmpeg({ s(u"-f"), s(u"lavfi"), s(u"-i"),
            QStringLiteral("sine=frequency=440:sample_rate=44100:duration=0.%1")
                .arg(QLatin1String(ms)),
            m_dir.filePath(QStringLiteral("ms%1.flac").arg(QLatin1String(ms))) }));
    }
}

void ConvertCountTimeTest::init()
{
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work + s(u"/out")));
    QVERIFY(QFile::copy(f(u"old.mp3"), m_work + s(u"/out/song.mp3")));
    m_old = bytesOf(m_work + s(u"/out/song.mp3"));
}

QString ConvertCountTimeTest::tools(const QString &result, const QByteArray &probe)
{
    const QString folder = m_work + s(u"/tools");
    QDir().mkpath(folder);
    const auto script = [](const QString &path, const QByteArray &text) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write("#!/bin/sh\n" + text);
            file.close();
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
    };
    script(folder + s(u"/ffmpeg"),
        "for a in \"$@\"; do out=$a; done\necho out_time_us=2000000\ncp '"
            + QFile::encodeName(result) + "' \"$out\" || exit 1\nexit 0\n");
    if (!probe.isEmpty())
        script(folder + s(u"/ffprobe"),
            "echo $$ > '" + QFile::encodeName(m_work) + "/started'\n" + probe);
    return folder + s(u"/ffmpeg");
}

ConvertJob ConvertCountTimeTest::job(const QString &encoder) const
{
    ConvertJob j;
    j.source = f(u"four.flac");
    j.settings = linuxbee::encoderPreset(s(u"mp3-320"))->settings;
    j.destination = m_work + s(u"/out/song.mp3");
    j.ifExists = ConvertJob::IfExists::Overwrite;
    j.copyTags = false;
    j.ffmpegPath = encoder;
    return j;
}

qint64 ConvertCountTimeTest::probeNumber() const
{
    return bytesOf(m_work + s(u"/started")).trimmed().toLongLong();
}

// The stand-in ffprobe is no longer a process (it was ended, and waited for).
bool ConvertCountTimeTest::probeGone() const
{
    const qint64 number = probeNumber();
    return number > 1 && ::kill(pid_t(number), 0) != 0;
}

QStringList ConvertCountTimeTest::leftovers() const
{
    return QDir(m_work + s(u"/out")).entryList(QDir::Files | QDir::Hidden, QDir::Name);
}

void ConvertCountTimeTest::theLimitWhenNoneIsGiven()
{
    QCOMPARE(ConvertJob().countSilenceMs, 30000);
}

void ConvertCountTimeTest::aCountThatSaysNothing_data()
{
    QTest::addColumn<QByteArray>("probe");
    QTest::newRow("from the start") << kHangs;
    QTest::newRow("after some lines") << kSomeLinesThenHangs;
}

void ConvertCountTimeTest::aCountThatSaysNothing()
{
    // ffprobe says nothing for longer than the job allows (here 300 ms). It is ended, the job
    // fails and says why, the file that was there is whole, and nothing is left. The caller has
    // its answer in seconds, not when ffprobe pleases.
    QFETCH(QByteArray, probe);
    ConvertJob j = job(tools(f(u"four.mp3"), probe));
    j.countSilenceMs = 300;
    QElapsedTimer clock;
    clock.start();
    const ConvertResult r = linuxbee::convertFile(j);
    const qint64 took = clock.elapsed();
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kSilent));
    QVERIFY2(took < 5000, qPrintable(QStringLiteral("it took %1 ms").arg(took)));
    QVERIFY(probeGone());
    QCOMPARE(bytesOf(j.destination), m_old);
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3") }));
}

void ConvertCountTimeTest::aCountThatIsSlowIsNotOneThatHangs()
{
    // The limit is on silence, not on the whole count: this one takes a second and never says
    // nothing for 400 ms.
    ConvertJob j = job(tools(f(u"four.mp3"), kSlowAndAlive));
    j.countSilenceMs = 400;
    const ConvertResult r = linuxbee::convertFile(j);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(bytesOf(j.destination), bytesOf(f(u"four.mp3")));
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3") }));
}

void ConvertCountTimeTest::cancelIsAnsweredDuringTheCount()
{
    // The limit is the one of a job that gives none (30 s), and ffprobe hangs. The caller says
    // "stop" as soon as the count has begun: Cancelled, soon, with ffprobe ended.
    ConvertJob j = job(tools(f(u"four.mp3"), kHangs));
    const QString started = m_work + s(u"/started");
    QElapsedTimer clock;
    clock.start();
    const ConvertResult r
        = linuxbee::convertFile(j, [&started](double) { return !QFileInfo::exists(started); });
    const qint64 took = clock.elapsed();
    QCOMPARE(r.status, ConvertResult::Status::Cancelled);
    QVERIFY2(took < 5000, qPrintable(QStringLiteral("it took %1 ms").arg(took)));
    QVERIFY(probeGone());
    QCOMPARE(bytesOf(j.destination), m_old);
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3") }));
}

void ConvertCountTimeTest::theFunctionIsAskedDuringTheCount()
{
    // C-PROGRESS goes on through the count: asked about once a second, with the value it was
    // last given. Here the count takes some two and a half seconds.
    ConvertJob j = job(tools(f(u"four.mp3"), kSlower));
    const QString started = m_work + s(u"/started");
    QList<double> before;
    QList<double> during;
    const ConvertResult r = linuxbee::convertFile(j, [&](double value) {
        (QFileInfo::exists(started) ? during : before).append(value);
        return true;
    });
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QVERIFY(!before.isEmpty());
    QVERIFY2(during.size() >= 3, qPrintable(QStringLiteral("asked %1 times").arg(during.size())));
    QCOMPARE(during.last(), 1.0);
    during.removeLast();
    for (double value : during)
        QCOMPARE(value, before.last());
    QCOMPARE(before.last(), 0.5); // what the encoder last said of four seconds
}

void ConvertCountTimeTest::aVeryShortSource_data()
{
    QTest::addColumn<QString>("result");
    QTest::addColumn<bool>("refused");
    // C-WHOLE: under half a second the allowance is 250 ms. The source is 300 ms long.
    QTest::newRow("40 ms: more than 250 ms short") << s(u"ms040.flac") << true;
    QTest::newRow("60 ms: 240 ms short") << s(u"ms060.flac") << false;
}

void ConvertCountTimeTest::aVeryShortSource()
{
    QFETCH(QString, result);
    QFETCH(bool, refused);
    ConvertJob j = job(tools(m_dir.filePath(result), QByteArray())); // the real ffprobe counts
    j.source = f(u"ms300.flac");
    j.settings = linuxbee::encoderPreset(s(u"flac"))->settings;
    j.destination = m_work + s(u"/out/song.flac");
    {
        QFile there(j.destination); // not music: nothing to be shorter than
        QVERIFY(there.open(QIODevice::WriteOnly) && there.write("a file that is here\n") == 20);
    }
    const ConvertResult r = linuxbee::convertFile(j);
    if (refused) {
        QCOMPARE(r.status, ConvertResult::Status::Failed);
        QCOMPARE(r.message, s(kShorter));
        QCOMPARE(bytesOf(j.destination), QByteArrayLiteral("a file that is here\n"));
    } else {
        QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
        QCOMPARE(bytesOf(j.destination), bytesOf(m_dir.filePath(result)));
    }
    QCOMPARE(leftovers(), QStringList({ s(u"song.flac"), s(u"song.mp3") }));
}

QTEST_GUILESS_MAIN(ConvertCountTimeTest)
#include "tst_convert_counttime.moc"
