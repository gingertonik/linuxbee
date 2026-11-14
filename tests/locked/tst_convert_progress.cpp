// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/convert.md, the parts settled after the review
// of plan 08: C-PROGRESS (a silent encoder can still be cancelled), C-STEPS
// 9 (the last answer), C-VALID (the compression level).
#include <QtTest>

#include "convert/converter.h"

using linuxbee::AudioFormat;
using linuxbee::ConvertJob;
using linuxbee::ConvertResult;
using linuxbee::EncoderSettings;

namespace {

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/convert/") + QLatin1String(name);
}

EncoderSettings mp3()
{
    const auto p = linuxbee::encoderPreset(QStringLiteral("mp3-320"));
    return p ? p->settings : EncoderSettings();
}

}

class ConvertProgressTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void silentEncoder();
    void repeatsTheLastValue();
    void theLastAnswer();
    void compressionLevel();

private:
    QString script(const char *name, const char *body);
    QStringList leftovers();
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void ConvertProgressTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

QString ConvertProgressTest::script(const char *name, const char *body)
{
    const QString path = m_dir.filePath(QLatin1String(name));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write("#!/bin/sh\n");
        file.write(body);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    return path;
}

QStringList ConvertProgressTest::leftovers()
{
    QStringList names;
    QDirIterator it(m_work, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        names.append(QDir(m_work).relativeFilePath(it.next()));
    names.sort();
    return names;
}

void ConvertProgressTest::silentEncoder()
{
    // A stand-in that leaves half a file and then hangs without a word.
    // C-PROGRESS: the function is still asked, about once a second, and
    // answering false stops the job.
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = m_work + QStringLiteral("/x.mp3");
    job.settings = mp3();
    job.ffmpegPath = script("silent.sh",
        "for a in \"$@\"; do out=$a; done\n"
        "echo half > \"$out\"\n"
        "exec sleep 60\n");
    QList<double> seen;
    QList<qint64> when;
    QElapsedTimer timer;
    timer.start();
    const ConvertResult r = linuxbee::convertFile(job, [&](double fraction) {
        seen.append(fraction);
        when.append(timer.elapsed());
        return seen.size() < 3; // the start, one more, then stop
    });
    QCOMPARE(r.status, ConvertResult::Status::Cancelled);
    QVERIFY2(timer.elapsed() < 15000, "a silent encoder could not be cancelled in time");
    QCOMPARE(seen, QList<double>({ 0.0, 0.0, 0.0 })); // nothing was reported, so nothing is claimed
    // Asked about once a second: not in a busy loop, and not after a long wait.
    QVERIFY2(when.at(2) - when.at(1) >= 500, qPrintable(QString::number(when.at(2) - when.at(1))));
    QVERIFY2(when.at(2) - when.at(1) <= 5000, qPrintable(QString::number(when.at(2) - when.at(1))));
    QCOMPARE(leftovers(), QStringList());
}

void ConvertProgressTest::repeatsTheLastValue()
{
    // One report, then silence: the function is asked again with the same value.
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = m_work + QStringLiteral("/x.mp3");
    job.settings = mp3();
    job.ffmpegPath = script("once.sh",
        "for a in \"$@\"; do out=$a; done\n"
        "echo half > \"$out\"\n"
        "echo out_time_us=250000\n"
        "echo progress=continue\n"
        "exec sleep 60\n");
    QList<double> seen;
    QElapsedTimer timer;
    timer.start();
    const ConvertResult r = linuxbee::convertFile(job, [&](double fraction) {
        seen.append(fraction);
        return seen.size() < 3;
    });
    QCOMPARE(r.status, ConvertResult::Status::Cancelled);
    QVERIFY2(timer.elapsed() < 15000, "cancelling took too long");
    QCOMPARE(seen, QList<double>({ 0.0, 0.25, 0.25 }));
    QCOMPARE(leftovers(), QStringList());
}

void ConvertProgressTest::theLastAnswer()
{
    // C-STEPS 9: the call with 1 comes when the file is finished; its answer changes nothing.
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = m_work + QStringLiteral("/done.mp3");
    job.settings = mp3();
    QList<double> seen;
    const ConvertResult r = linuxbee::convertFile(job, [&](double fraction) {
        seen.append(fraction);
        return fraction < 1.0;
    });
    QCOMPARE(r.status, ConvertResult::Status::Converted);
    QCOMPARE(r.outputPath, job.destination);
    QVERIFY(QFile::exists(job.destination));
    QVERIFY(!seen.isEmpty());
    QCOMPARE(seen.last(), 1.0);
    QCOMPARE(seen.count(1.0), 1);
    QCOMPARE(leftovers(), QStringList({ QStringLiteral("done.mp3") }));
}

void ConvertProgressTest::compressionLevel()
{
    // C-VALID: -1 is "not set", on every format; a level that is set is for Flac, 0 to 12.
    const auto problems = [](AudioFormat format, int level) {
        EncoderSettings e;
        e.format = format;
        e.compressionLevel = level;
        int count = 0;
        for (const QString &line : linuxbee::validateSettings(e))
            count += line.startsWith(QStringLiteral("compression level:")) ? 1 : 0;
        return count;
    };
    QCOMPARE(problems(AudioFormat::Flac, -1), 0);
    QCOMPARE(problems(AudioFormat::Flac, 0), 0);
    QCOMPARE(problems(AudioFormat::Flac, 12), 0);
    QCOMPARE(problems(AudioFormat::Flac, 13), 1);
    QCOMPARE(problems(AudioFormat::Flac, -2), 1);
    QCOMPARE(problems(AudioFormat::Mp3, -1), 0);
    QCOMPARE(problems(AudioFormat::Mp3, 5), 1);
    QCOMPARE(problems(AudioFormat::Wav, 0), 1);
}

QTEST_APPLESS_MAIN(ConvertProgressTest)
#include "tst_convert_progress.moc"
