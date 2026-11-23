// LOCKED ACCEPTANCE TEST (long names: the part file and the safe save's copy, fix-c1). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/tag-safe-save.md (WS-NAME, NM-SCRATCH) and docs/spec/convert.md
// (C-CLEAN). Convert F7 of the audit: a file whose name is 241 to 255 bytes could not be
// converted, because the part file's name is 15 bytes longer than the destination's, and a name
// is at most 255. The safe save's copy is named the same way, so its tags could not be saved
// either. Every file is a copy of a fixture in a folder of this test's own.
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QtTest>

#include "convert/converter.h"
#include "tags/tagreader.h"
#include "tags/tagwriter.h"

using linuxbee::ConvertJob;
using linuxbee::ConvertResult;

namespace {

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/") + QLatin1String(name);
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

bool validUtf8(const QByteArray &bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    [[maybe_unused]] const QString decoded = decoder(bytes);
    return !decoder.hasError();
}

// A name of `bytes` bytes in UTF-8: `letter` repeated, then "x" to make up the count, then `ext`.
QString nameOf(int bytes, const QString &letter, const QString &ext)
{
    QString name;
    const int each = int(letter.toUtf8().size());
    const int room = bytes - int(ext.toUtf8().size());
    while (name.toUtf8().size() + each <= room)
        name += letter;
    while (name.toUtf8().size() < room)
        name += QLatin1Char('x');
    return name + ext;
}

}

class ConvertNamesLongTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void aLongNameIsConverted_data();
    void aLongNameIsConverted();
    void thePartFileOfALongName();
    void leftoversOfALongName();
    void tagsAreSavedOnALongName();
    void aSaveLeftoverOfALongName();

private:
    QString script(const char *name, const char *body);
    QString notedPartName(const QString &destination);
    QStringList names();
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void ConvertNamesLongTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

QString ConvertNamesLongTest::script(const char *name, const char *body)
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

// The name of the part file a conversion to `destination` writes to, as a stand-in for ffmpeg
// notes it (it then fails, and the conversion removes its part file).
QString ConvertNamesLongTest::notedPartName(const QString &destination)
{
    const QString noted = m_dir.filePath(QStringLiteral("noted.txt"));
    QFile::remove(noted);
    ConvertJob job;
    job.source = fixture("convert/tone.flac");
    job.destination = destination;
    job.settings = mp3();
    job.ffmpegPath = script("note.sh",
        (QByteArray("for a in \"$@\"; do out=$a; done\nprintf '%s' \"$out\" > '")
            + QFile::encodeName(noted) + "'\nexit 1\n")
            .constData());
    if (linuxbee::convertFile(job).status != ConvertResult::Status::Failed)
        return QString();
    return QFileInfo(QString::fromUtf8(bytesOf(noted))).fileName();
}

QStringList ConvertNamesLongTest::names()
{
    QStringList out = QDir(m_work).entryList(QDir::Files | QDir::Hidden);
    out.sort();
    return out;
}

void ConvertNamesLongTest::aLongNameIsConverted_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("241 bytes") << nameOf(241, QStringLiteral("a"), QStringLiteral(".mp3"));
    QTest::newRow("250 bytes") << nameOf(250, QStringLiteral("a"), QStringLiteral(".mp3"));
    QTest::newRow("255 bytes") << nameOf(255, QStringLiteral("a"), QStringLiteral(".mp3"));
    QTest::newRow("255 bytes, letters of three")
        << nameOf(255, QStringLiteral("\u65e5"), QStringLiteral(".mp3"));
    QTest::newRow("254 bytes, letters of four")
        << nameOf(254, QStringLiteral("\U0001F3B5"), QStringLiteral(".mp3"));
}

void ConvertNamesLongTest::aLongNameIsConverted()
{
    // Convert F7: with the tags carried (the default), so the safe save runs on the part file.
    QFETCH(QString, name);
    QVERIFY(name.toUtf8().size() > 240 && name.toUtf8().size() <= 255);
    ConvertJob job;
    job.source = fixture("convert/tone.flac");
    job.destination = m_work + QLatin1Char('/') + name;
    job.settings = mp3();
    const ConvertResult r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(names(), QStringList({ name })); // the file, and nothing left beside it
    QCOMPARE(linuxbee::readTags(job.destination).status, linuxbee::ReadResult::Status::Ok);
}

void ConvertNamesLongTest::thePartFileOfALongName()
{
    // NM-SCRATCH: the part file's name is at most 255 bytes of valid UTF-8, begins as C-CLEAN says,
    // keeps the destination's extension, and is chosen at random. Its name after the fifteen bytes
    // begins with the start of the destination's name.
    const QString name = nameOf(255, QStringLiteral("\u65e5"), QStringLiteral(".mp3"));
    const QString destination = m_work + QLatin1Char('/') + name;
    const QString first = notedPartName(destination);
    const QString second = notedPartName(destination);
    QVERIFY2(!first.isEmpty() && !second.isEmpty(), "the stand-in noted no part file");
    for (const QString &part : { first, second }) {
        const QByteArray bytes = part.toUtf8();
        QVERIFY2(bytes.size() <= 255, qPrintable(QString::number(bytes.size())));
        QVERIFY(validUtf8(bytes));
        QVERIFY2(
            QRegularExpression(QStringLiteral("^\\.lbpart-[a-z0-9]{6}-")).match(part).hasMatch(),
            qPrintable(part));
        QVERIFY2(part.endsWith(QStringLiteral(".mp3")), qPrintable(part));
        QVERIFY(part.mid(15).startsWith(name.left(20)));
    }
    QVERIFY2(first != second, "the same name twice: it is not chosen at random");
    QCOMPARE(names(), QStringList()); // nothing left
}

void ConvertNamesLongTest::leftoversOfALongName()
{
    // C-CLEAN: what a killed conversion of this destination left is cleared away by the next one.
    // The part file of another destination whose name begins with the same 240 bytes is not this
    // destination's, and stays.
    const QString name = nameOf(250, QStringLiteral("a"), QStringLiteral("1.mp3"));
    const QString other = nameOf(250, QStringLiteral("a"), QStringLiteral("2.mp3"));
    const QString mine = notedPartName(m_work + QLatin1Char('/') + name);
    const QString theirs = notedPartName(m_work + QLatin1Char('/') + other);
    QVERIFY2(!mine.isEmpty() && !theirs.isEmpty(), "the stand-in noted no part file");
    const QString left = QStringLiteral(".lbpart-abc123-") + mine.mid(15);
    const QString notMine = QStringLiteral(".lbpart-abc123-") + theirs.mid(15);
    QVERIFY(left != notMine);
    QVERIFY(put(m_work + QLatin1Char('/') + left, "half"));
    QVERIFY(put(m_work + QLatin1Char('/') + notMine, "not ours"));
    ConvertJob job;
    job.source = fixture("convert/tone.flac");
    job.destination = m_work + QLatin1Char('/') + name;
    job.settings = mp3();
    const ConvertResult r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QStringList want { name, notMine };
    want.sort();
    QCOMPARE(names(), want);
    QCOMPARE(bytesOf(m_work + QLatin1Char('/') + notMine), QByteArray("not ours"));
}

void ConvertNamesLongTest::tagsAreSavedOnALongName()
{
    // WS-NAME, NM-SCRATCH: a file of a 250-byte name has its tags saved, and nothing is left.
    const QString name = nameOf(250, QStringLiteral("b"), QStringLiteral(".flac"));
    const QString path = m_work + QLatin1Char('/') + name;
    QVERIFY(QFile::copy(fixture("tags/basic.flac"), path));
    QFile(path).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    linuxbee::TagChanges changes;
    changes.fields.insert(QStringLiteral("Title"), { QStringLiteral("A title saved") });
    QString copy;
    linuxbee::WriteOptions options;
    options.beforeReplace = [&copy](const QString &newVersion) {
        copy = QFileInfo(newVersion).fileName();
        return true;
    };
    const linuxbee::WriteResult r = linuxbee::writeTags(path, changes, options);
    QVERIFY2(r.status == linuxbee::WriteResult::Status::Ok,
        qPrintable(r.problems.join(QLatin1Char('\n'))));
    QCOMPARE(linuxbee::readTags(path).tags.fields.value(QStringLiteral("Title")),
        QStringList({ QStringLiteral("A title saved") }));
    QVERIFY2(copy.toUtf8().size() <= 255 && copy.startsWith(QStringLiteral(".lbsave-"))
            && copy.endsWith(QStringLiteral(".flac")),
        qPrintable(copy));
    QCOMPARE(names(), QStringList({ name }));
}

void ConvertNamesLongTest::aSaveLeftoverOfALongName()
{
    // WS-KILLED: the copy a killed save of this long name left is cleared by the next save; that of
    // another file whose name begins with the same 240 bytes stays.
    const QString name = nameOf(250, QStringLiteral("c"), QStringLiteral("1.flac"));
    const QString other = nameOf(250, QStringLiteral("c"), QStringLiteral("2.flac"));
    QString mine;
    QString theirs;
    for (const QString &one : { name, other }) {
        const QString path = m_work + QLatin1Char('/') + one;
        QVERIFY(QFile::copy(fixture("tags/basic.flac"), path));
        QFile(path).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        linuxbee::TagChanges changes;
        changes.fields.insert(QStringLiteral("Title"), { QStringLiteral("first") });
        linuxbee::WriteOptions options;
        QString *noted = one == name ? &mine : &theirs;
        options.beforeReplace = [noted](const QString &newVersion) {
            *noted = QFileInfo(newVersion).fileName();
            return true;
        };
        QCOMPARE(
            linuxbee::writeTags(path, changes, options).status, linuxbee::WriteResult::Status::Ok);
    }
    QVERIFY2(mine.size() > 15 && theirs.size() > 15, "no copy was noted");
    const QString left = QStringLiteral(".lbsave-abc123-") + mine.mid(15);
    const QString notMine = QStringLiteral(".lbsave-abc123-") + theirs.mid(15);
    QVERIFY(left != notMine);
    QVERIFY(put(m_work + QLatin1Char('/') + left, "half"));
    QVERIFY(put(m_work + QLatin1Char('/') + notMine, "not ours"));
    linuxbee::TagChanges changes;
    changes.fields.insert(QStringLiteral("Title"), { QStringLiteral("second") });
    QCOMPARE(linuxbee::writeTags(m_work + QLatin1Char('/') + name, changes).status,
        linuxbee::WriteResult::Status::Ok);
    QStringList want { name, other, notMine };
    want.sort();
    QCOMPARE(names(), want);
    QCOMPARE(bytesOf(m_work + QLatin1Char('/') + notMine), QByteArray("not ours"));
}

QTEST_APPLESS_MAIN(ConvertNamesLongTest)
#include "tst_convert_names_long.moc"
