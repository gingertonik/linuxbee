// LOCKED ACCEPTANCE TEST (the converter: a count that does not end; lines that count nothing are
// silence). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason, and
// left alone.
//
// Expected values are C-REPLACE ("The count is watched") in docs/spec/convert.md. The fault this
// holds shut (the review of fix-97, 2026-11-19): the limit was on silence alone, and any line
// broke the silence. A program that printed for ever in the place of ffprobe held the
// conversion for ever.
//
// ffprobe is looked for beside ffmpeg first, so a stand-in for each is put in a folder of its
// own. A stand-in that goes on "for ever" gives up by itself (after two million lines, or ten
// seconds), so that a program without the rule fails this test and does not stop the gate. The
// sound is made here with ffmpeg. Everything is in a temporary folder.
#include <QElapsedTimer>
#include <QProcess>
#include <QtTest>

#include <signal.h>

#include "convert/converter.h"
#include "tags/tagreader.h"

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
const char16_t *kTooMuch
    = u"the converted file could not be checked: ffprobe counted far more than the file's length";

// The result is four seconds at 22,050 samples a second. The limit is ten times that and a
// minute: 100 seconds, some 2,205,000 samples. A line of 44,100 samples is two seconds of it,
// so 49 lines are under the limit and 52 are over.
QByteArray frames(int count)
{
    return "i=0\nwhile [ $i -lt " + QByteArray::number(count)
        + " ]; do echo nb_samples=44100; i=$((i+1)); done\n";
}

// A line that counts nothing, ten times a second for ten seconds, and then a bad end.
QByteArray tenSecondsOf(const QByteArray &line)
{
    return "i=0\nwhile [ $i -lt 100 ]; do echo " + line + "; sleep 0.1; i=$((i+1)); done\nexit 1\n";
}

}

class ConvertCountEndTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void aCountThatDoesNotEnd_data();
    void aCountThatDoesNotEnd();
    void aLongCountUnderTheLimit();
    void linesThatCountNothingAreSilence_data();
    void linesThatCountNothingAreSilence();

private:
    QString f(const char16_t *name) const { return m_dir.filePath(s(name)); }
    // A folder with an encoder that writes `result`, and beside it an ffprobe that does `probe`.
    // The path of the encoder.
    QString tools(const QString &result, const QByteArray &probe);
    ConvertJob job(const QString &encoder) const;
    // The stand-in ffprobe is no longer a process (it was ended, and waited for).
    bool probeGone() const;
    QStringList leftovers() const;
    QTemporaryDir m_dir;
    QString m_work;
    QByteArray m_old;
    int m_counter = 0;
};

void ConvertCountEndTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY2(ffmpeg({ s(u"-f"), s(u"lavfi"), s(u"-i"), s(u"sine=frequency=440:duration=4"),
                 s(u"-ac"), s(u"2"), f(u"four.flac") }),
        "ffmpeg is needed, with its lavfi input");
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-ar"), s(u"22050"), f(u"low.mp3") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-af"), s(u"volume=0.5"), f(u"old.mp3") }));
    // What the numbers of this test stand on: the header of the result.
    const linuxbee::ReadResult low = linuxbee::readTags(f(u"low.mp3"));
    QVERIFY(low.status == linuxbee::ReadResult::Status::Ok);
    QCOMPARE(qint64(low.tags.audio.sampleRateHz), qint64(22050));
    const qint64 length = qint64(low.tags.audio.durationMs);
    QVERIFY2(length >= 3900 && length <= 4300,
        qPrintable(QStringLiteral("the result is %1 ms long").arg(length)));
}

void ConvertCountEndTest::init()
{
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work + s(u"/out")));
    QVERIFY(QFile::copy(f(u"old.mp3"), m_work + s(u"/out/song.mp3")));
    m_old = bytesOf(m_work + s(u"/out/song.mp3"));
}

QString ConvertCountEndTest::tools(const QString &result, const QByteArray &probe)
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
    script(
        folder + s(u"/ffprobe"), "echo $$ > '" + QFile::encodeName(m_work) + "/started'\n" + probe);
    return folder + s(u"/ffmpeg");
}

ConvertJob ConvertCountEndTest::job(const QString &encoder) const
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

bool ConvertCountEndTest::probeGone() const
{
    const qint64 number = bytesOf(m_work + s(u"/started")).trimmed().toLongLong();
    return number > 1 && ::kill(pid_t(number), 0) != 0;
}

QStringList ConvertCountEndTest::leftovers() const
{
    return QDir(m_work + s(u"/out")).entryList(QDir::Files | QDir::Hidden, QDir::Name);
}

void ConvertCountEndTest::aCountThatDoesNotEnd_data()
{
    QTest::addColumn<QByteArray>("probe");
    QTest::newRow("frames for ever") << frames(2000000) + "exit 1\n";
    QTest::newRow("over the limit, and a good end")
        << frames(52) + "echo sample_rate=22050\nexit 0\n";
}

void ConvertCountEndTest::aCountThatDoesNotEnd()
{
    // More is counted than the result can hold by any reading of its header. The job fails and
    // says why, soon; ffprobe is ended; the file that was there is whole, and nothing is left.
    // The limit on silence is the 30 seconds of a job that gives none: it is not what ends this.
    QFETCH(QByteArray, probe);
    const ConvertJob j = job(tools(f(u"low.mp3"), probe));
    QElapsedTimer clock;
    clock.start();
    const ConvertResult r = linuxbee::convertFile(j);
    const qint64 took = clock.elapsed();
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kTooMuch));
    QVERIFY2(took < 5000, qPrintable(QStringLiteral("it took %1 ms").arg(took)));
    QVERIFY(probeGone());
    QCOMPARE(bytesOf(j.destination), m_old);
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3") }));
}

void ConvertCountEndTest::aLongCountUnderTheLimit()
{
    // 98 seconds counted where the header says four: odd, and under the limit. The limit is for
    // a count that does not end; it is not a second opinion on the length.
    const ConvertJob j = job(tools(f(u"low.mp3"), frames(49) + "echo sample_rate=22050\nexit 0\n"));
    const ConvertResult r = linuxbee::convertFile(j);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(bytesOf(j.destination), bytesOf(f(u"low.mp3")));
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3") }));
}

void ConvertCountEndTest::linesThatCountNothingAreSilence_data()
{
    QTest::addColumn<QByteArray>("line");
    QTest::newRow("a line of something else") << QByteArray("junk=1");
    QTest::newRow("the rate, again and again") << QByteArray("sample_rate=22050");
    QTest::newRow("frames of no samples") << QByteArray("nb_samples=0");
}

void ConvertCountEndTest::linesThatCountNothingAreSilence()
{
    // ffprobe prints ten lines a second and counts nothing. That is silence: it is ended when
    // the job's limit (here 400 ms) is over, not when it pleases.
    QFETCH(QByteArray, line);
    ConvertJob j = job(tools(f(u"low.mp3"), tenSecondsOf(line)));
    j.countSilenceMs = 400;
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

QTEST_GUILESS_MAIN(ConvertCountEndTest)
#include "tst_convert_countend.moc"
