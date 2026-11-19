// LOCKED ACCEPTANCE TEST (the converter: the move into place replaces only the file it saw).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never by
// editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are C-PLACE in docs/spec/convert.md. The fault this holds shut (the review of
// fix-85b, 2026-11-18): the last step set aside and removed whatever was at the destination by
// then. A file that another program had put at a name that was free when the conversion began
// was replaced, in Keep both and in Skip, where the rule is that no file is overwritten.
//
// The other program is played by the stand-in encoder: it writes the result and, before it ends,
// puts a file at the name. The sound is made here with ffmpeg; ffprobe is needed too (one case
// replaces a file, C-REPLACE). Everything is in a temporary folder.
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
    return p.waitForFinished(120000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

const QByteArray kTheirs = QByteArrayLiteral("a file that another program put here meanwhile\n");
const char16_t *kAppeared
    = u"a file appeared at the destination while converting; it was not replaced";

}

class ConvertPlaceTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void keepBothAndTheFreeNameIsTaken();
    void keepBothAndTheNextNamesAreTakenToo();
    void skipAndAFileAppears();
    void overwriteAndAFileAppearsWhereNoneWas();
    void overwriteAndTheFileIsAnotherOneByThen();
    void theControls_data();
    void theControls();

private:
    QString f(const char16_t *name) const { return m_dir.filePath(s(name)); }
    // An encoder that ends well, has written the four-second MP3, and has `more` done besides.
    QString racer(const QByteArray &more);
    QByteArray puts(const QString &path) const;
    QStringList leftovers() const;
    ConvertJob job(ConvertJob::IfExists ifExists) const;
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void ConvertPlaceTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY2(ffmpeg({ s(u"-f"), s(u"lavfi"), s(u"-i"), s(u"sine=frequency=440:duration=4"),
                 s(u"-ac"), s(u"2"), f(u"four.flac") }),
        "ffmpeg is needed, with its lavfi input");
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), f(u"four.mp3") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-af"), s(u"volume=0.5"), f(u"mine.mp3") }));
}

void ConvertPlaceTest::init()
{
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

QString ConvertPlaceTest::racer(const QByteArray &more)
{
    const QString path = m_work + QStringLiteral("/racer.sh");
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write("#!/bin/sh\nfor a in \"$@\"; do out=$a; done\ncp '"
            + QFile::encodeName(f(u"four.mp3")) + "' \"$out\" || exit 1\n" + more + "exit 0\n");
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    return path;
}

// Shell: another program's file is put at `path`, whole, by a rename (so it is never the file
// that was there before, whatever the disk does with the numbers of files).
QByteArray ConvertPlaceTest::puts(const QString &path) const
{
    const QByteArray name = "'" + QFile::encodeName(path) + "'";
    const QByteArray temp = "'" + QFile::encodeName(path) + ".theirs'";
    return "printf '%s\\n' 'a file that another program put here meanwhile' > " + temp + " && mv "
        + temp + " " + name + " || exit 1\n";
}

QStringList ConvertPlaceTest::leftovers() const
{
    QStringList names = QDir(m_work).entryList(QDir::Files | QDir::Hidden, QDir::Name);
    names.removeIf([](const QString &name) { return name.endsWith(QLatin1String(".sh")); });
    return names;
}

ConvertJob ConvertPlaceTest::job(ConvertJob::IfExists ifExists) const
{
    ConvertJob j;
    j.source = f(u"four.flac");
    j.settings = linuxbee::encoderPreset(s(u"mp3-320"))->settings;
    j.destination = m_work + s(u"/song.mp3");
    j.ifExists = ifExists;
    j.copyTags = false;
    return j;
}

void ConvertPlaceTest::keepBothAndTheFreeNameIsTaken()
{
    // C-EXISTS Rename: song.mp3 is there, so the result is to go to song_1.mp3, which is free.
    // While the encoder works another program puts a file at song_1.mp3. It is not replaced: the
    // result goes to the next free name.
    ConvertJob j = job(ConvertJob::IfExists::Rename);
    QVERIFY(QFile::copy(f(u"mine.mp3"), j.destination));
    const QByteArray mine = bytesOf(j.destination);
    j.ffmpegPath = racer(puts(m_work + s(u"/song_1.mp3")));
    const ConvertResult r = linuxbee::convertFile(j);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(bytesOf(m_work + s(u"/song_1.mp3")), kTheirs);
    QCOMPARE(r.outputPath, m_work + s(u"/song_2.mp3"));
    QCOMPARE(bytesOf(r.outputPath), bytesOf(f(u"four.mp3")));
    QCOMPARE(bytesOf(j.destination), mine);
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3"), s(u"song_1.mp3"), s(u"song_2.mp3") }));
}

void ConvertPlaceTest::keepBothAndTheNextNamesAreTakenToo()
{
    ConvertJob j = job(ConvertJob::IfExists::Rename);
    QVERIFY(QFile::copy(f(u"mine.mp3"), j.destination));
    j.ffmpegPath = racer(puts(m_work + s(u"/song_1.mp3")) + puts(m_work + s(u"/song_2.mp3")));
    const ConvertResult r = linuxbee::convertFile(j);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(r.outputPath, m_work + s(u"/song_3.mp3"));
    QCOMPARE(bytesOf(m_work + s(u"/song_1.mp3")), kTheirs);
    QCOMPARE(bytesOf(m_work + s(u"/song_2.mp3")), kTheirs);
    QCOMPARE(bytesOf(r.outputPath), bytesOf(f(u"four.mp3")));
}

void ConvertPlaceTest::skipAndAFileAppears()
{
    // C-EXISTS Skip: nothing was at the destination, so the conversion ran. By its end a file is
    // there. Skip never writes over a file: the job fails, the file is whole, nothing is left.
    ConvertJob j = job(ConvertJob::IfExists::Skip);
    j.ffmpegPath = racer(puts(j.destination));
    const ConvertResult r = linuxbee::convertFile(j);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kAppeared));
    QCOMPARE(bytesOf(j.destination), kTheirs);
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3") }));
}

void ConvertPlaceTest::overwriteAndAFileAppearsWhereNoneWas()
{
    // Overwrite means the file that was seen. None was: the one that came since was not looked
    // at (C-REPLACE), and is not replaced.
    ConvertJob j = job(ConvertJob::IfExists::Overwrite);
    j.ffmpegPath = racer(puts(j.destination));
    const ConvertResult r = linuxbee::convertFile(j);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kAppeared));
    QCOMPARE(bytesOf(j.destination), kTheirs);
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3") }));
}

void ConvertPlaceTest::overwriteAndTheFileIsAnotherOneByThen()
{
    // Overwrite, and a file was there. By the end it is another file under the same name. That
    // one was not the file seen, and is not replaced.
    ConvertJob j = job(ConvertJob::IfExists::Overwrite);
    QVERIFY(QFile::copy(f(u"mine.mp3"), j.destination));
    j.ffmpegPath = racer(puts(j.destination));
    const ConvertResult r = linuxbee::convertFile(j);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kAppeared));
    QCOMPARE(bytesOf(j.destination), kTheirs);
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3") }));
}

void ConvertPlaceTest::theControls_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("there");
    QTest::addColumn<QString>("goesTo");
    QTest::newRow("skip, nothing there")
        << int(ConvertJob::IfExists::Skip) << false << s(u"song.mp3");
    QTest::newRow("keep both, nothing there")
        << int(ConvertJob::IfExists::Rename) << false << s(u"song.mp3");
    QTest::newRow("keep both, a file there")
        << int(ConvertJob::IfExists::Rename) << true << s(u"song_1.mp3");
    QTest::newRow("overwrite, nothing there")
        << int(ConvertJob::IfExists::Overwrite) << false << s(u"song.mp3");
    QTest::newRow("overwrite, the file that was seen")
        << int(ConvertJob::IfExists::Overwrite) << true << s(u"song.mp3");
}

void ConvertPlaceTest::theControls()
{
    // With nobody else about, everything is as it was.
    QFETCH(int, mode);
    QFETCH(bool, there);
    QFETCH(QString, goesTo);
    ConvertJob j = job(ConvertJob::IfExists(mode));
    if (there)
        QVERIFY(QFile::copy(f(u"mine.mp3"), j.destination));
    const QByteArray mine = bytesOf(f(u"mine.mp3"));
    j.ffmpegPath = racer(QByteArray());
    const ConvertResult r = linuxbee::convertFile(j);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(r.outputPath, m_work + QLatin1Char('/') + goesTo);
    QCOMPARE(bytesOf(r.outputPath), bytesOf(f(u"four.mp3")));
    if (there && goesTo != s(u"song.mp3"))
        QCOMPARE(bytesOf(j.destination), mine);
    QStringList want { goesTo };
    if (there && goesTo != s(u"song.mp3"))
        want.prepend(s(u"song.mp3"));
    QCOMPARE(leftovers(), want);
}

QTEST_GUILESS_MAIN(ConvertPlaceTest)
#include "tst_convert_place.moc"
