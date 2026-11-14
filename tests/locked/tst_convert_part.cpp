// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/convert.md, C-CLEAN as rewritten after the
// review of plans 08 and 08b: the file ffmpeg writes to has a name no user
// file has, and a conversion never removes a file it did not make.
#include <QRegularExpression>
#include <QtTest>

#include "convert/converter.h"

using linuxbee::ConvertJob;
using linuxbee::ConvertResult;

namespace {

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/convert/") + QLatin1String(name);
}

linuxbee::EncoderSettings mp3()
{
    const auto p = linuxbee::encoderPreset(QStringLiteral("mp3-320"));
    return p ? p->settings : linuxbee::EncoderSettings();
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArrayLiteral("<cannot open>");
}

bool put(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

}

class ConvertPartTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void aUserFileNamedPart();
    void whereFfmpegWrites();
    void failureAndCancelRemoveOnlyTheirOwn();
    void leftoversOfAKilledRun();

private:
    QString script(const char *name, const char *body);
    QStringList names();
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void ConvertPartTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

QString ConvertPartTest::script(const char *name, const char *body)
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

QStringList ConvertPartTest::names()
{
    QStringList out = QDir(m_work).entryList(QDir::Files | QDir::Hidden);
    out.sort();
    return out;
}

void ConvertPartTest::aUserFileNamedPart()
{
    // The old rule removed "x.part.mp3" before converting to "x.mp3". Files
    // with such names are the user's.
    for (const char *mine : { "x.part.mp3", "x.part", "x.mp3.part" })
        QVERIFY(put(m_work + QLatin1Char('/') + QLatin1String(mine), "mine"));
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = m_work + QStringLiteral("/x.mp3");
    job.settings = mp3();
    const ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Converted);
    QCOMPARE(names(), QStringList({ QStringLiteral("x.mp3"), QStringLiteral("x.mp3.part"), QStringLiteral("x.part"), QStringLiteral("x.part.mp3") }));
    for (const char *mine : { "x.part.mp3", "x.part", "x.mp3.part" })
        QCOMPARE(bytesOf(m_work + QLatin1Char('/') + QLatin1String(mine)), QByteArray("mine"));
    QVERIFY(QFileInfo(job.destination).size() > 1000);
    // And a destination without an extension.
    job.destination = m_work + QStringLiteral("/x");
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::Converted);
    QCOMPARE(bytesOf(m_work + QStringLiteral("/x.part")), QByteArray("mine"));
}

void ConvertPartTest::whereFfmpegWrites()
{
    // A stand-in that notes the output path it was given, then fails.
    const QString noted = m_dir.filePath(QStringLiteral("noted.txt"));
    QFile::remove(noted);
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = m_work + QStringLiteral("/My Song.mp3");
    job.settings = mp3();
    job.ffmpegPath = script("note.sh", (QByteArray("for a in \"$@\"; do out=$a; done\nprintf '%s' \"$out\" > '") + QFile::encodeName(noted) + "'\nexit 1\n").constData());
    QList<QString> seen;
    for (int i = 0; i < 2; ++i) {
        QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::Failed);
        seen.append(QString::fromUtf8(bytesOf(noted)));
    }
    for (const QString &path : seen) {
        const QFileInfo info(path);
        QCOMPARE(info.absolutePath(), m_work); // beside the destination
        const QRegularExpression shape(QStringLiteral("^\\.lbpart-[a-z0-9]{6}-My Song\\.mp3$"));
        QVERIFY2(shape.match(info.fileName()).hasMatch(), qPrintable(info.fileName()));
    }
    QVERIFY2(seen.at(0) != seen.at(1), "the same name twice: it is not chosen at random");
    QCOMPARE(names(), QStringList()); // nothing left
}

void ConvertPartTest::failureAndCancelRemoveOnlyTheirOwn()
{
    QVERIFY(put(m_work + QStringLiteral("/x.part.mp3"), "mine"));
    QVERIFY(put(m_work + QStringLiteral("/x.mp3"), "the old one"));
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = m_work + QStringLiteral("/x.mp3");
    job.settings = mp3();
    job.ifExists = ConvertJob::IfExists::Overwrite;
    // Fails after writing half a file.
    job.ffmpegPath = script("half.sh", "for a in \"$@\"; do out=$a; done\necho half > \"$out\"\nexit 1\n");
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::Failed);
    QCOMPARE(names(), QStringList({ QStringLiteral("x.mp3"), QStringLiteral("x.part.mp3") }));
    QCOMPARE(bytesOf(m_work + QStringLiteral("/x.part.mp3")), QByteArray("mine"));
    QCOMPARE(bytesOf(m_work + QStringLiteral("/x.mp3")), QByteArray("the old one"));
    // Cancelled while ffmpeg hangs.
    job.ffmpegPath = script("slow.sh", "for a in \"$@\"; do out=$a; done\necho half > \"$out\"\necho out_time_us=250000\nexec sleep 60\n");
    const ConvertResult r = linuxbee::convertFile(job, [](double fraction) { return fraction == 0.0; });
    QCOMPARE(r.status, ConvertResult::Status::Cancelled);
    QCOMPARE(names(), QStringList({ QStringLiteral("x.mp3"), QStringLiteral("x.part.mp3") }));
    QCOMPARE(bytesOf(m_work + QStringLiteral("/x.part.mp3")), QByteArray("mine"));
    QCOMPARE(bytesOf(m_work + QStringLiteral("/x.mp3")), QByteArray("the old one"));
}

void ConvertPartTest::leftoversOfAKilledRun()
{
    // What a killed conversion of this destination left is cleared away by
    // the next one; nothing else that merely looks similar is touched.
    const QStringList stay { QStringLiteral(".lbpart-abc123-other.mp3"), QStringLiteral(".lbpart-abc12-x.mp3"),
        QStringLiteral(".lbpart-ABC123-x.mp3"), QStringLiteral("lbpart-abc123-x.mp3"), QStringLiteral(".lbpart-abc123-x.mp3.txt") };
    for (const QString &name : stay)
        QVERIFY(put(m_work + QLatin1Char('/') + name, "not ours"));
    QVERIFY(put(m_work + QStringLiteral("/.lbpart-abc123-x.mp3"), "half"));
    QVERIFY(put(m_work + QStringLiteral("/.lbpart-zz99zz-x.mp3"), "half"));
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = m_work + QStringLiteral("/x.mp3");
    job.settings = mp3();
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::Converted);
    QStringList want = stay;
    want.append(QStringLiteral("x.mp3"));
    want.sort();
    QCOMPARE(names(), want);
    for (const QString &name : stay)
        QCOMPARE(bytesOf(m_work + QLatin1Char('/') + name), QByteArray("not ours"));
}

QTEST_APPLESS_MAIN(ConvertPartTest)
#include "tst_convert_part.moc"
