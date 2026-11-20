// LOCKED ACCEPTANCE TEST (the converter: what replaces a file is played through first). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are C-WHOLE and C-REPLACE in docs/spec/convert.md. The faults this holds shut
// (the review of fix-85, 2026-11-18, N1 to N5): a result whose header states a whole length and
// which plays short still replaced the user's file; so did the whole conversion of a source that
// had itself been cut off; and a source of a second or less was not compared at all.
//
// The sound is made here with ffmpeg; ffprobe is needed too (C-REPLACE counts with it). The
// encoders that misbehave are shell scripts. Everything is in a temporary folder.
#include <QProcess>
#include <QStandardPaths>
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

bool put(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

bool ffmpeg(const QStringList &arguments, QByteArray *standardOutput = nullptr)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"),
        QStringList({ s(u"-nostdin"), s(u"-v"), s(u"error"), s(u"-y") }) + arguments);
    const bool ended = p.waitForFinished(120000) && p.exitStatus() == QProcess::NormalExit;
    if (standardOutput)
        *standardOutput = p.readAllStandardOutput();
    return ended && p.exitCode() == 0;
}

int lengthOf(const QString &path)
{
    return linuxbee::readTags(path).tags.audio.durationMs;
}

const char16_t *kShorterThanSource = u"the converted file is shorter than the source";
const char16_t *kShorterThanThere = u"the converted file is shorter than the file it would "
                                    u"replace; remove that file first if this is meant";
const char16_t *kNotCounted = u"the converted file could not be played through to check it";

}

class ConvertReplaceTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void theFilesAreWhatTheySay();
    void aResultThatPlaysShort_data();
    void aResultThatPlaysShort();
    void aRealConversionOfASourceWithAHole_data();
    void aRealConversionOfASourceWithAHole();
    void aSourceThatWasCutOff();
    void aWholeResultReplaces_data();
    void aWholeResultReplaces();
    void aLongerResultReplacesAShorterFile();
    void aFileThatIsNotSoundIsReplacedAsBefore();
    void nothingToCountWith();
    void aNewFileIsNotCounted();
    void aSourceOfUnderASecond();
    void theSecondFromAbove_data();
    void theSecondFromAbove();
    void aSourceOfNoKnownLength();

private:
    QString f(const char16_t *name) const { return m_dir.filePath(s(name)); }
    QString script(const char *name, const QByteArray &body);
    QByteArray copies(const QString &file) const;
    QStringList leftovers() const;
    ConvertJob onto(const QString &source, const QString &preset, const QString &theirs);
    QTemporaryDir m_dir;
    QString m_work;
    QByteArray m_cp; // cp, by its whole path: one case runs with an empty PATH
    QByteArray m_path;
    int m_counter = 0;
};

void ConvertReplaceTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_cp = QFile::encodeName(QStandardPaths::findExecutable(s(u"cp")));
    QVERIFY(!m_cp.isEmpty());
    QVERIFY2(!QStandardPaths::findExecutable(s(u"ffprobe")).isEmpty(), "ffprobe is needed");
    m_path = qgetenv("PATH");

    // Twelve seconds of noise (a tone packs to nothing, and a hole in nothing is no hole).
    QVERIFY2(
        ffmpeg({ s(u"-f"), s(u"lavfi"), s(u"-i"), s(u"anoisesrc=d=12:c=pink:r=44100:a=0.3:seed=1"),
            s(u"-ac"), s(u"2"), f(u"noise.flac") }),
        "ffmpeg is needed, with its lavfi input");
    // The same with a quarter of its bytes zeroed, from a third in: a file with a bad stretch.
    QByteArray bytes = bytesOf(f(u"noise.flac"));
    bytes.replace(bytes.size() / 3, bytes.size() / 4, QByteArray(bytes.size() / 4, '\0'));
    QVERIFY(put(f(u"hole.flac"), bytes));
    // What a real ffmpeg makes of it, ending well: files whose headers state the whole length.
    QVERIFY(ffmpeg({ s(u"-i"), f(u"hole.flac"), s(u"-c:a"), s(u"libopus"), f(u"hole.opus") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"hole.flac"), s(u"-c:a"), s(u"aac"), f(u"hole.m4a") }));
    // Good conversions of the whole sound: the files of the user's that are to be replaced.
    QVERIFY(ffmpeg({ s(u"-i"), f(u"noise.flac"), f(u"whole.mp3") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"noise.flac"), s(u"-c:a"), s(u"libopus"), f(u"whole.opus") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"noise.flac"), s(u"-c:a"), s(u"aac"), f(u"whole.m4a") }));
    // A finished MP3 cut off afterwards: its header still states twelve seconds.
    bytes = bytesOf(f(u"whole.mp3"));
    QVERIFY(put(f(u"half.mp3"), bytes.left(bytes.size() / 2)));
    // An MP3 of constant bitrate with no length header, cut off: its length is read from its size.
    QVERIFY(ffmpeg({ s(u"-i"), f(u"noise.flac"), s(u"-c:a"), s(u"libmp3lame"), s(u"-b:a"),
        s(u"128k"), s(u"-write_xing"), s(u"0"), f(u"cbr.mp3") }));
    bytes = bytesOf(f(u"cbr.mp3"));
    QVERIFY(put(f(u"cbrcut.mp3"), bytes.left(bytes.size() * 3 / 10)));
    // Four seconds, and results of it of other lengths.
    QVERIFY(ffmpeg({ s(u"-f"), s(u"lavfi"), s(u"-i"), s(u"sine=frequency=440:duration=4"),
        s(u"-ac"), s(u"2"), f(u"four.flac") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-t"), s(u"2.5"), f(u"r25.mp3") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-t"), s(u"3.2"), f(u"r32.mp3") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), f(u"four.mp3") }));
    // Under a second.
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-t"), s(u"0.8"), f(u"tiny.flac") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-t"), s(u"0.8"), f(u"tiny.mp3") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-t"), s(u"0.13"), f(u"bit.mp3") }));
    // A FLAC written to a pipe has no length in its header.
    QByteArray piped;
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-f"), s(u"flac"), s(u"-") }, &piped));
    QVERIFY(put(f(u"nolength.flac"), piped));
}

void ConvertReplaceTest::init()
{
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

void ConvertReplaceTest::cleanup()
{
    qputenv("PATH", m_path);
}

QString ConvertReplaceTest::script(const char *name, const QByteArray &body)
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

// The body of an encoder that ends well and has written `file`.
QByteArray ConvertReplaceTest::copies(const QString &file) const
{
    return m_cp + " '" + QFile::encodeName(file) + "' \"$out\"\nexit 0\n";
}

// Everything in the work folder but the scripts, sorted.
QStringList ConvertReplaceTest::leftovers() const
{
    QStringList names = QDir(m_work).entryList(QDir::Files | QDir::Hidden, QDir::Name);
    names.removeIf([](const QString &name) { return name.endsWith(QLatin1String(".sh")); });
    return names;
}

// A job with Overwrite whose destination is there already: a copy of `theirs`.
ConvertJob ConvertReplaceTest::onto(
    const QString &source, const QString &preset, const QString &theirs)
{
    ConvertJob job;
    job.source = source;
    job.settings = linuxbee::encoderPreset(preset)->settings;
    job.destination = m_work + s(u"/song.") + linuxbee::fileExtension(job.settings.format);
    job.ifExists = ConvertJob::IfExists::Overwrite;
    job.copyTags = false;
    job.ffmpegPath = QStringLiteral("ffmpeg");
    if (!theirs.isEmpty() && !QFile::copy(theirs, job.destination))
        QTest::qFail("the file to be replaced could not be put there", __FILE__, __LINE__);
    return job;
}

void ConvertReplaceTest::theFilesAreWhatTheySay()
{
    // What the cases below stand on: as the program reads them (from their headers), the files
    // with a hole and the file cut in half are whole, and the cut-off MP3 is simply a short file.
    QCOMPARE(lengthOf(f(u"noise.flac")), 12000);
    QCOMPARE(lengthOf(f(u"hole.flac")), 12000);
    QVERIFY2(
        lengthOf(f(u"hole.opus")) >= 11900, qPrintable(QString::number(lengthOf(f(u"hole.opus")))));
    QVERIFY2(
        lengthOf(f(u"hole.m4a")) >= 11900, qPrintable(QString::number(lengthOf(f(u"hole.m4a")))));
    QVERIFY2(
        lengthOf(f(u"half.mp3")) >= 11900, qPrintable(QString::number(lengthOf(f(u"half.mp3")))));
    const int cut = lengthOf(f(u"cbrcut.mp3"));
    QVERIFY2(cut > 3000 && cut < 4200, qPrintable(QString::number(cut)));
    QCOMPARE(lengthOf(f(u"nolength.flac")), 0);
    QVERIFY2(lengthOf(f(u"bit.mp3")) < 300, qPrintable(QString::number(lengthOf(f(u"bit.mp3")))));
    QCOMPARE(lengthOf(f(u"tiny.flac")), 800);
}

void ConvertReplaceTest::aResultThatPlaysShort_data()
{
    QTest::addColumn<QString>("preset");
    QTest::addColumn<QString>("result");
    QTest::addColumn<QString>("theirs");
    QTest::newRow("N2: opus of a source with a hole")
        << s(u"opus-128") << s(u"hole.opus") << s(u"whole.opus");
    QTest::newRow("N2: aac of a source with a hole")
        << s(u"aac-256") << s(u"hole.m4a") << s(u"whole.m4a");
    QTest::newRow("N3: an mp3 cut off after it was finished")
        << s(u"mp3-320") << s(u"half.mp3") << s(u"whole.mp3");
}

void ConvertReplaceTest::aResultThatPlaysShort()
{
    // C-REPLACE: the encoder ends well and the result's header states the whole length, so the
    // look at the header passes it. Played through it is seconds short. The user's file stays.
    QFETCH(QString, preset);
    QFETCH(QString, result);
    QFETCH(QString, theirs);
    ConvertJob job = onto(f(u"noise.flac"), preset, m_dir.filePath(theirs));
    job.ffmpegPath = script("lies.sh", copies(m_dir.filePath(result)));
    const QByteArray was = bytesOf(job.destination);
    const ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kShorterThanSource));
    QCOMPARE(bytesOf(job.destination), was);
    QCOMPARE(leftovers(), QStringList({ QFileInfo(job.destination).fileName() }));
}

void ConvertReplaceTest::aRealConversionOfASourceWithAHole_data()
{
    QTest::addColumn<QString>("preset");
    QTest::addColumn<QString>("theirs");
    QTest::newRow("to opus") << s(u"opus-128") << s(u"whole.opus");
    QTest::newRow("to aac") << s(u"aac-256") << s(u"whole.m4a");
    QTest::newRow("to mp3") << s(u"mp3-320") << s(u"whole.mp3");
}

void ConvertReplaceTest::aRealConversionOfASourceWithAHole()
{
    // N2 with nothing stood in: the real ffmpeg converts the source with the bad stretch and ends
    // well. Whatever the format written, the good file that is there is not replaced.
    QFETCH(QString, preset);
    QFETCH(QString, theirs);
    const ConvertJob job = onto(f(u"hole.flac"), preset, m_dir.filePath(theirs));
    const QByteArray was = bytesOf(job.destination);
    const ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kShorterThanSource));
    QCOMPARE(bytesOf(job.destination), was);
    QCOMPARE(leftovers(), QStringList({ QFileInfo(job.destination).fileName() }));
}

void ConvertReplaceTest::aSourceThatWasCutOff()
{
    // N1. The source itself was cut off, and nothing in it says so: it reads as under four
    // seconds, and its conversion is whole. The file that is there is twelve seconds. A result
    // shorter than the file it would replace does not replace it.
    const ConvertJob job = onto(f(u"cbrcut.mp3"), s(u"opus-128"), f(u"whole.opus"));
    const QByteArray was = bytesOf(job.destination);
    const ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kShorterThanThere));
    QCOMPARE(bytesOf(job.destination), was);
    QCOMPARE(leftovers(), QStringList({ s(u"song.opus") }));
}

void ConvertReplaceTest::aWholeResultReplaces_data()
{
    QTest::addColumn<QString>("preset");
    QTest::addColumn<QString>("theirs");
    QTest::newRow("opus") << s(u"opus-128") << s(u"whole.opus");
    QTest::newRow("aac") << s(u"aac-256") << s(u"whole.m4a");
    QTest::newRow("mp3") << s(u"mp3-320") << s(u"whole.mp3");
    QTest::newRow("flac") << s(u"flac") << s(u"noise.flac");
    QTest::newRow("ogg") << s(u"ogg-q5") << QString();
}

void ConvertReplaceTest::aWholeResultReplaces()
{
    // The control: a good conversion replaces a file of the same length, as it always did.
    QFETCH(QString, preset);
    QFETCH(QString, theirs);
    ConvertJob job
        = onto(f(u"noise.flac"), preset, theirs.isEmpty() ? QString() : m_dir.filePath(theirs));
    if (theirs.isEmpty()) { // an ogg of the user's, made here
        QVERIFY(
            ffmpeg({ s(u"-i"), f(u"noise.flac"), s(u"-c:a"), s(u"libvorbis"), job.destination }));
    }
    const QByteArray was = bytesOf(job.destination);
    const ConvertResult r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QVERIFY(bytesOf(job.destination) != was);
    const int length = lengthOf(job.destination);
    QVERIFY2(length >= 11900 && length <= 12200, qPrintable(QString::number(length)));
    QCOMPARE(leftovers(), QStringList({ QFileInfo(job.destination).fileName() }));
}

void ConvertReplaceTest::aLongerResultReplacesAShorterFile()
{
    const ConvertJob job = onto(f(u"noise.flac"), s(u"mp3-320"), f(u"four.mp3"));
    const ConvertResult r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QVERIFY(lengthOf(job.destination) >= 11900);
}

void ConvertReplaceTest::aFileThatIsNotSoundIsReplacedAsBefore()
{
    // A file at the destination that has no length to read is not compared with (C-EXISTS,
    // Overwrite, as it was).
    ConvertJob job = onto(f(u"noise.flac"), s(u"mp3-320"), QString());
    QVERIFY(put(job.destination, QByteArray("words, under the name of a song")));
    const ConvertResult r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QVERIFY(lengthOf(job.destination) >= 11900);
}

void ConvertReplaceTest::nothingToCountWith()
{
    // C-REPLACE: where the result cannot be played through (here: no ffprobe to be found), the
    // file that is there is not replaced on the header's word alone.
    ConvertJob job = onto(f(u"noise.flac"), s(u"mp3-320"), f(u"whole.mp3"));
    job.ffmpegPath = script("good.sh", copies(f(u"whole.mp3")));
    QVERIFY(put(job.destination, bytesOf(f(u"four.mp3"))));
    const QByteArray was = bytesOf(job.destination);
    qputenv("PATH", QFile::encodeName(m_work + s(u"/nothing-here")));
    const ConvertResult r = linuxbee::convertFile(job);
    qputenv("PATH", m_path);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kNotCounted));
    QCOMPARE(bytesOf(job.destination), was);
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3") }));
}

void ConvertReplaceTest::aNewFileIsNotCounted()
{
    // The cost is paid only where a file would be replaced: a conversion to a new name needs no
    // ffprobe and is not played through.
    ConvertJob job = onto(f(u"noise.flac"), s(u"mp3-320"), QString());
    job.ffmpegPath = script("good.sh", copies(f(u"whole.mp3")));
    qputenv("PATH", QFile::encodeName(m_work + s(u"/nothing-here")));
    const ConvertResult r = linuxbee::convertFile(job);
    qputenv("PATH", m_path);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(bytesOf(job.destination), bytesOf(f(u"whole.mp3")));
}

void ConvertReplaceTest::aSourceOfUnderASecond()
{
    // N4. A source of 0.8 s: the second that is allowed is more than all of it, so nothing was
    // compared. Now a result may be short by half the source's length at most, where that is
    // under a second.
    ConvertJob job = onto(f(u"tiny.flac"), s(u"mp3-320"), QString());
    job.ffmpegPath = script("bit.sh", copies(f(u"bit.mp3")));
    ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kShorterThanSource));
    QCOMPARE(leftovers(), QStringList());
    // And its whole conversion is taken, by the real encoder and by a stand-in.
    job.ffmpegPath = QStringLiteral("ffmpeg");
    r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QVERIFY(QFile::remove(job.destination));
    job.ffmpegPath = script("tiny.sh", copies(f(u"tiny.mp3")));
    r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
}

void ConvertReplaceTest::theSecondFromAbove_data()
{
    QTest::addColumn<QString>("result");
    QTest::addColumn<bool>("taken");
    QTest::newRow("N5: 1.5 s short of 4 s") << s(u"r25.mp3") << false;
    QTest::newRow("0.8 s short of 4 s") << s(u"r32.mp3") << true;
}

void ConvertReplaceTest::theSecondFromAbove()
{
    // C-WHOLE: the second is a second, held from both sides.
    QFETCH(QString, result);
    QFETCH(bool, taken);
    ConvertJob job = onto(f(u"four.flac"), s(u"mp3-320"), QString());
    job.ffmpegPath = script("some.sh", copies(m_dir.filePath(result)));
    const ConvertResult r = linuxbee::convertFile(job);
    if (taken) {
        QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    } else {
        QCOMPARE(r.status, ConvertResult::Status::Failed);
        QCOMPARE(r.message, s(kShorterThanSource));
        QCOMPARE(leftovers(), QStringList());
    }
}

void ConvertReplaceTest::aSourceOfNoKnownLength()
{
    // C-WHOLE: a source whose length is not known is not compared; a result that is not sound is
    // refused all the same, and here only the look at whether it is sound can do it.
    ConvertJob job = onto(f(u"nolength.flac"), s(u"mp3-320"), QString());
    job.ffmpegPath = script("words.sh", "echo not audio > \"$out\"\nexit 0\n");
    ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(u"the converted file could not be read"));
    QCOMPARE(leftovers(), QStringList());
    job.ffmpegPath = QStringLiteral("ffmpeg");
    r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QVERIFY(lengthOf(job.destination) >= 3900);
}

QTEST_GUILESS_MAIN(ConvertReplaceTest)
#include "tst_convert_replace.moc"
