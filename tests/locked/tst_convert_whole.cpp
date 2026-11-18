// LOCKED ACCEPTANCE TEST (the converter: an encoder is not taken at its word). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are C-WHOLE in docs/spec/convert.md. The fault this holds shut (the write-path
// audit, part 3, F1): an encoder that ended well with a short or unreadable file was believed, and
// with Overwrite the user's old file was replaced by it and deleted.
//
// The sound is made here with ffmpeg (four seconds of a tone), as tst_convert.cpp needs ffmpeg too.
// The encoders that misbehave are shell scripts. Everything is in a temporary folder.
#include <QProcess>
#include <QtTest>

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
    return p.waitForFinished(60000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

}

class ConvertWholeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void aShortResult_data();
    void aShortResult();
    void aResultThatIsNotSound_data();
    void aResultThatIsNotSound();
    void aSecondIsAllowed();
    void aWholeResult_data();
    void aWholeResult();

private:
    QString script(const char *name, const QByteArray &body);
    QStringList leftovers() const;
    QTemporaryDir m_dir;
    QString m_work;
    QString m_long; // four seconds
    int m_counter = 0;
};

void ConvertWholeTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_long = m_dir.filePath(s(u"long.flac"));
    QVERIFY2(ffmpeg({ s(u"-f"), s(u"lavfi"), s(u"-i"), s(u"sine=frequency=440:duration=4"),
                 s(u"-ac"), s(u"2"), m_long }),
        "ffmpeg is needed, with its lavfi input");
    QCOMPARE(linuxbee::readTags(m_long).tags.audio.durationMs, 4000);
    // What a damaged source gives: sound that ends early, in a file that is whole as a file.
    for (const char *name : { "short.mp3", "short.flac", "short.m4a", "short.ogg", "nearly.mp3" }) {
        const QString length
            = QLatin1String(name).startsWith(QLatin1String("nearly")) ? s(u"3.5") : s(u"1");
        QVERIFY2(
            ffmpeg({ s(u"-i"), m_long, s(u"-t"), length, m_dir.filePath(QLatin1String(name)) }),
            name);
    }
}

void ConvertWholeTest::init()
{
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

QString ConvertWholeTest::script(const char *name, const QByteArray &body)
{
    const QString path = m_work + QLatin1Char('/') + QLatin1String(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write("#!/bin/sh\nfor a in \"$@\"; do out=$a; done\n");
        file.write(body);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    return path;
}

// Everything in the work folder but the scripts, sorted.
QStringList ConvertWholeTest::leftovers() const
{
    QStringList names = QDir(m_work).entryList(QDir::Files | QDir::Hidden, QDir::Name);
    names.removeIf([](const QString &name) { return name.endsWith(QLatin1String(".sh")); });
    return names;
}

void ConvertWholeTest::aShortResult_data()
{
    QTest::addColumn<QString>("preset");
    QTest::addColumn<QString>("shortFile");
    QTest::addColumn<bool>("onto");
    QTest::addColumn<bool>("copyTags");
    QTest::newRow("mp3, onto a file of the user's")
        << s(u"mp3-320") << s(u"short.mp3") << true << true;
    QTest::newRow("mp3, onto a file, no tags copied")
        << s(u"mp3-320") << s(u"short.mp3") << true << false;
    QTest::newRow("mp3, a new file") << s(u"mp3-320") << s(u"short.mp3") << false << true;
    QTest::newRow("flac, onto a file") << s(u"flac") << s(u"short.flac") << true << true;
    QTest::newRow("m4a, onto a file") << s(u"aac-256") << s(u"short.m4a") << true << true;
    QTest::newRow("ogg, onto a file") << s(u"ogg-q5") << s(u"short.ogg") << true << false;
}

void ConvertWholeTest::aShortResult()
{
    // C-WHOLE: the encoder ends well, and what it wrote is one second of a source of four. The job
    // fails; the file of the user's that was to be replaced is as it was; nothing is left.
    QFETCH(QString, preset);
    QFETCH(QString, shortFile);
    QFETCH(bool, onto);
    QFETCH(bool, copyTags);
    ConvertJob job;
    job.source = m_long;
    job.settings = linuxbee::encoderPreset(preset)->settings;
    job.destination = m_work + s(u"/song.") + linuxbee::fileExtension(job.settings.format);
    job.ifExists = ConvertJob::IfExists::Overwrite;
    job.copyTags = copyTags;
    if (onto) {
        QFile mine(job.destination);
        QVERIFY(mine.open(QIODevice::WriteOnly));
        mine.write("the user's own file, which is all there is of it");
    }
    const QByteArray was = bytesOf(job.destination);
    job.ffmpegPath = script(
        "short.sh", "cp '" + QFile::encodeName(m_dir.filePath(shortFile)) + "' \"$out\"\nexit 0\n");
    const ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(u"the converted file is shorter than the source"));
    QCOMPARE(bytesOf(job.destination), was);
    QCOMPARE(
        leftovers(), onto ? QStringList({ QFileInfo(job.destination).fileName() }) : QStringList());
}

void ConvertWholeTest::aResultThatIsNotSound_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::newRow("words") << QByteArray("echo not audio > \"$out\"\nexit 0\n");
    QTest::newRow("nothing in it") << QByteArray(": > \"$out\"\nexit 0\n");
    QTest::newRow("no file") << QByteArray("exit 0\n");
    QTest::newRow("the head of a file") << QByteArray("head -c 2000 SHORT > \"$out\"\nexit 0\n");
}

void ConvertWholeTest::aResultThatIsNotSound()
{
    // C-WHOLE: with no tags to copy, nothing used to look at the result at all.
    QFETCH(QByteArray, body);
    body.replace("SHORT", "'" + QFile::encodeName(m_dir.filePath(s(u"short.mp3"))) + "'");
    ConvertJob job;
    job.source = m_long;
    job.settings = linuxbee::encoderPreset(s(u"mp3-320"))->settings;
    job.destination = m_work + s(u"/song.mp3");
    job.ifExists = ConvertJob::IfExists::Overwrite;
    job.copyTags = false;
    {
        QFile mine(job.destination);
        QVERIFY(mine.open(QIODevice::WriteOnly));
        mine.write("the user's own file");
    }
    job.ffmpegPath = script("liar.sh", body);
    const ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QVERIFY2(r.message == s(u"the converted file could not be read")
            || r.message == s(u"the converted file is shorter than the source"),
        qPrintable(r.message));
    QCOMPARE(bytesOf(job.destination), QByteArray("the user's own file"));
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3") }));
}

void ConvertWholeTest::aSecondIsAllowed()
{
    // C-WHOLE: a result may be up to a second shorter than the length read from the source.
    // (Lengths are read from headers, and of 251 real files none was read more than 71 ms longer
    // than it decodes.)
    ConvertJob job;
    job.source = m_long;
    job.settings = linuxbee::encoderPreset(s(u"mp3-320"))->settings;
    job.destination = m_work + s(u"/song.mp3");
    job.ffmpegPath = script("nearly.sh",
        "cp '" + QFile::encodeName(m_dir.filePath(s(u"nearly.mp3"))) + "' \"$out\"\nexit 0\n");
    const ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Converted);
    QVERIFY(QFileInfo::exists(job.destination));
}

void ConvertWholeTest::aWholeResult_data()
{
    QTest::addColumn<QString>("preset");
    for (const char *id :
        { "mp3-320", "mp3-v2", "flac", "ogg-q5", "opus-128", "aac-256", "alac", "wav", "wavpack" })
        QTest::newRow(id) << QString::fromLatin1(id);
}

void ConvertWholeTest::aWholeResult()
{
    // The other side: the real encoder, every format. A whole result is not refused, and its length
    // as the program reads it is within the second.
    QFETCH(QString, preset);
    ConvertJob job;
    job.source = m_long;
    job.settings = linuxbee::encoderPreset(preset)->settings;
    job.destination = m_work + s(u"/song.") + linuxbee::fileExtension(job.settings.format);
    job.copyTags = false;
    const ConvertResult r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    const int length = linuxbee::readTags(job.destination).tags.audio.durationMs;
    QVERIFY2(length >= 3900 && length <= 4200, qPrintable(QString::number(length)));
}

QTEST_GUILESS_MAIN(ConvertWholeTest)
#include "tst_convert_whole.moc"
