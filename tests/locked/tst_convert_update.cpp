// LOCKED ACCEPTANCE TEST (Convert's "Update its tags only" keeps what the user added). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/convert.md (C-EXISTS UpdateTags, C-UPDATE). The converted
// file is the fixture tone.mp3 (tone.flac converted); ffmpeg is never run here.
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "convert/converter.h"
#include "tags/tagreader.h"
#include "tags/tagwriter.h"

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

// A 1x1 PNG.
QByteArray png()
{
    return QByteArray::fromHex(
        "89504e470d0a1a0a0000000d4948445200000001000000010806000000"
        "1f15c4890000000d49444154789c6360000002000154a24f5d0000000049454e44ae426082");
}

linuxbee::TrackTags read(const QString &path)
{
    linuxbee::ReadOptions o;
    o.pictures = true;
    o.pictureData = true;
    return linuxbee::readTags(path, o).tags;
}

}

class ConvertUpdateTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void keepsWhatTheOriginalLacks();
    void theOriginalsPicturesStillGoOn();
    void aFileThatIsNotItsConversionIsRefused();

private:
    QTemporaryDir m_dir;
    int m_n = 0;
    QString m_work;
    QString copy(const char *from, const char *name);
    ConvertJob job(const QString &source, const QString &destination);
};

void ConvertUpdateTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_n));
    QVERIFY(QDir().mkpath(m_work));
}

QString ConvertUpdateTest::copy(const char *from, const char *name)
{
    const QString to = m_work + QLatin1Char('/') + QLatin1String(name);
    if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/") + QLatin1String(from), to))
        QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner);
    return to;
}

ConvertJob ConvertUpdateTest::job(const QString &source, const QString &destination)
{
    ConvertJob j;
    j.source = source;
    j.destination = destination;
    j.settings = linuxbee::encoderPreset(QStringLiteral("mp3-128"))->settings;
    j.ifExists = ConvertJob::IfExists::UpdateTags;
    j.copyTags = true;
    j.copyPictures = true;
    j.ffmpegPath = m_work + QStringLiteral("/no-ffmpeg-here");
    return j;
}

void ConvertUpdateTest::keepsWhatTheOriginalLacks()
{
    // C-UPDATE: the converted file takes every field and tag the original has. What the original
    // lacks (the user's lyrics, rating, comment, a key of their own, the cover) stays as it was.
    const QString source = copy("convert/tone.flac", "song.flac");
    const QString target = copy("convert/tone.mp3", "song.mp3");
    linuxbee::TagChanges mine;
    mine.fields = { { s(u"Lyrics"), { s(u"the words") } },
        { s(u"Rating"), { linuxbee::ratingToString(4) } },
        { s(u"Comment"), { s(u"my comment") } } };
    mine.other = { { s(u"MY OWN"), { s(u"kept") } } };
    linuxbee::Picture cover;
    cover.type = s(u"Front Cover");
    cover.mimeType = s(u"image/png");
    cover.data = png();
    mine.pictures = QList<linuxbee::Picture> { cover };
    QCOMPARE(linuxbee::writeTags(target, mine).status, linuxbee::WriteResult::Status::Ok);
    const linuxbee::TrackTags before = read(target);
    QCOMPARE(before.pictures.size(), 1);

    // The original lacks what the user added: its comment, rating and cover are taken off first.
    linuxbee::TagChanges theirs;
    theirs.fields = { { s(u"Title"), { s(u"Changed") } }, { s(u"Comment"), { } },
        { s(u"Rating"), { } }, { s(u"Lyrics"), { } } };
    theirs.pictures = QList<linuxbee::Picture>();
    QCOMPARE(linuxbee::writeTags(source, theirs).status, linuxbee::WriteResult::Status::Ok);
    const linuxbee::TrackTags original = read(source);
    for (const char16_t *field : { u"Lyrics", u"Rating", u"Comment" })
        QVERIFY2(!original.fields.contains(s(field)), "the fixture has changed");
    QVERIFY(!original.other.contains(s(u"MY OWN")));
    QVERIFY(original.pictures.isEmpty());

    const ConvertResult r = linuxbee::convertFile(job(source, target));
    QVERIFY2(r.status == ConvertResult::Status::TagsUpdated, qPrintable(r.message));
    const linuxbee::TrackTags after = read(target);
    QCOMPARE(after.fields.value(s(u"Title")), QStringList { s(u"Changed") });
    for (const char16_t *field : { u"Lyrics", u"Rating", u"Comment" })
        QCOMPARE(after.fields.value(s(field)), before.fields.value(s(field)));
    QCOMPARE(after.other.value(s(u"MY OWN")), QStringList { s(u"kept") });
    QCOMPARE(after.pictures.size(), 1);
    QCOMPARE(after.pictures.at(0).data, png());
    for (auto it = original.fields.cbegin(); it != original.fields.cend(); ++it)
        QCOMPARE(after.fields.value(it.key()), it.value());
}

void ConvertUpdateTest::theOriginalsPicturesStillGoOn()
{
    // C-UPDATE: an original that has pictures gives them to the converted file (tone.flac has a
    // cover; it is replaced here by one this test knows).
    const QString source = copy("convert/tone.flac", "song.flac");
    const QString target = copy("convert/tone.mp3", "song.mp3");
    linuxbee::Picture cover;
    cover.type = s(u"Front Cover");
    cover.mimeType = s(u"image/png");
    cover.data = png();
    linuxbee::TagChanges theirs;
    theirs.pictures = QList<linuxbee::Picture> { cover };
    QCOMPARE(linuxbee::writeTags(source, theirs).status, linuxbee::WriteResult::Status::Ok);
    const ConvertResult r = linuxbee::convertFile(job(source, target));
    QVERIFY2(r.status == ConvertResult::Status::TagsUpdated, qPrintable(r.message));
    const linuxbee::TrackTags after = read(target);
    QCOMPARE(after.pictures.size(), 1);
    QCOMPARE(after.pictures.at(0).data, png());
}

void ConvertUpdateTest::aFileThatIsNotItsConversionIsRefused()
{
    // C-UPDATE: a file under the name whose length differs from the original's by more than half a
    // second is not its conversion. Nothing is written to it, and the job fails, saying why.
    const QString source = copy("convert/tone.flac", "song.flac");
    const QString target = copy("tags/basic.mp3", "song.mp3");
    const int a = read(source).audio.durationMs;
    const int b = read(target).audio.durationMs;
    QVERIFY2(qAbs(a - b) > 500, qPrintable(QStringLiteral("%1 %2").arg(a).arg(b)));
    const QByteArray bytes = bytesOf(target);
    const ConvertResult r = linuxbee::convertFile(job(source, target));
    QVERIFY(r.status == ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(u"the file there is not a conversion of this one: their lengths differ"));
    QCOMPARE(bytesOf(target), bytes);
}

QTEST_APPLESS_MAIN(ConvertUpdateTest)
#include "tst_convert_update.moc"
