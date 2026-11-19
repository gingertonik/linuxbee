// LOCKED ACCEPTANCE TEST (tag undo: what is kept for a file left in the step is whole). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are UN-KEEP and UN-RATING in docs/spec/tag-undo.md. These two cases were asked
// for by the review of fix-91 (2026-11-18): the code was right, and nothing held it. A tidy written
// file by file would delete a cover that a file still in the step shares with one that left; and
// nothing asked whether a kept file's rating frames are still there for the second try.
//
// Every case works in a temporary folder. The MP3 of the second case is made here, byte by byte.
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"
#include "tagundo/tagundo.h"

using linuxbee::Picture;
using linuxbee::TagChanges;
using linuxbee::TagUndo;
using linuxbee::WriteResult;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// One POPM frame: the address, the rating byte, the play counter.
struct Popm {
    QByteArray address;
    int rating = 0;
    quint32 counter = 0;
};

QByteArray plain(quint32 n)
{
    QByteArray out(4, '\0');
    for (int i = 3; i >= 0; --i, n >>= 8)
        out[i] = char(n & 0xff);
    return out;
}

QByteArray sevenBits(quint32 n)
{
    QByteArray out(4, '\0');
    for (int i = 3; i >= 0; --i, n >>= 7)
        out[i] = char(n & 0x7f);
    return out;
}

quint32 number(const QByteArray &bytes, int bits)
{
    quint32 n = 0;
    for (const char c : bytes)
        n = (n << bits) | quint32(uchar(c));
    return n;
}

// BYTES: a frame is its four letters, the size of its body (in ID3v2.4 seven bits to the byte),
// two flag bytes, the body.
QByteArray frame(int version, const char *id, const QByteArray &body)
{
    return QByteArray(id, 4)
        + (version == 4 ? sevenBits(quint32(body.size())) : plain(quint32(body.size())))
        + QByteArray(2, '\0') + body;
}

// BYTES: a tag is "ID3", the version, a zero, a flag byte, the size of what follows (seven bits to
// the byte), the frames. The title is there so that the tag is never empty.
QByteArray tag(int version, const QList<Popm> &frames)
{
    QByteArray all = frame(version, "TIT2", QByteArray(1, '\0') + QByteArray("made"));
    for (const Popm &p : frames) {
        all += frame(version, "POPM",
            p.address + QByteArray(1, '\0') + QByteArray(1, char(p.rating)) + plain(p.counter));
    }
    return QByteArray("ID3") + QByteArray(1, char(version)) + QByteArray(2, '\0')
        + sevenBits(quint32(all.size())) + all;
}

// The POPM frames of the file, in the order they stand in the tag, as "address|byte|counter".
// One line that says why, when the tag cannot be walked.
QStringList popmOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { QStringLiteral("<cannot open>") };
    const QByteArray data = file.readAll();
    if (!data.startsWith("ID3"))
        return { QStringLiteral("<no ID3v2 tag>") };
    const int version = uchar(data.at(3));
    if ((version != 3 && version != 4) || uchar(data.at(5)) != 0)
        return { QStringLiteral("<a tag this test cannot walk: version %1, flags %2>")
                .arg(version)
                .arg(uchar(data.at(5))) };
    const qsizetype end = 10 + qsizetype(number(data.mid(6, 4), 7));
    QStringList found;
    qsizetype at = 10;
    while (at + 10 <= end && data.at(at) != '\0') {
        const QByteArray id = data.mid(at, 4);
        const qsizetype size = qsizetype(number(data.mid(at + 4, 4), version == 4 ? 7 : 8));
        if (uchar(data.at(at + 9)) != 0)
            return { QStringLiteral("<a frame this test cannot walk>") };
        const QByteArray body = data.mid(at + 10, size);
        if (id == "POPM") {
            const qsizetype zero = body.indexOf('\0');
            found << QStringLiteral("%1|%2|%3")
                         .arg(QString::fromLatin1(body.left(zero)))
                         .arg(uchar(body.at(zero + 1)))
                         .arg(number(body.mid(zero + 2), 8));
        }
        at += 10 + size;
    }
    return found;
}

QList<Picture> picturesOf(const QString &path)
{
    linuxbee::ReadOptions options;
    options.pictures = true;
    options.pictureData = true;
    return linuxbee::readTags(path, options).tags.pictures;
}

// A writer that fails for the paths in `refuse` and saves the others.
TagUndo::Writer refusing(const QStringList &refuse)
{
    return [refuse](const QString &path, const TagChanges &changes) {
        if (refuse.contains(path)) {
            WriteResult failed;
            failed.status = WriteResult::Status::Failed;
            return failed;
        }
        return linuxbee::writeTags(path, changes);
    };
}

}

class TagUndoKeepWholeTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void aCoverSharedWithAFileThatLeft_data();
    void aCoverSharedWithAFileThatLeft();
    void theRatingFramesOfAKeptFile();

private:
    QString copy(const char *fixture, const char *as);
    QTemporaryDir m_dir;
    QString m_work;
    QString m_scratch;
    int m_counter = 0;
};

void TagUndoKeepWholeTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    m_scratch = m_work + QStringLiteral("/scratch");
    QVERIFY(QDir().mkpath(m_work));
}

QString TagUndoKeepWholeTest::copy(const char *fixture, const char *as)
{
    const QString target = m_work + QLatin1Char('/') + QLatin1String(as);
    if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(fixture), target))
        QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    return target;
}

void TagUndoKeepWholeTest::aCoverSharedWithAFileThatLeft_data()
{
    QTest::addColumn<bool>("keptFirst");
    QTest::newRow("the file that left came first in the step") << false;
    QTest::newRow("the file that is kept came first in the step") << true;
}

void TagUndoKeepWholeTest::aCoverSharedWithAFileThatLeft()
{
    // Two files have the same cover; the step that removes it keeps one picture file for both
    // (UN-RECORD 5). At the undo one file is put back and one is not. The picture file is still
    // needed, by the one that stays: it is there, and the second try brings the cover back.
    QFETCH(bool, keptFirst);
    const QString a = copy("basic.flac", "a.flac");
    const QString b = copy("basic.mp3", "b.mp3");
    Picture cover;
    cover.type = QStringLiteral("Front Cover");
    cover.mimeType = QStringLiteral("image/png");
    cover.data = QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray(300, 'c');
    TagChanges give;
    give.pictures = QList<Picture> { cover };
    TagChanges take;
    take.pictures = QList<Picture> { };
    QVERIFY(linuxbee::writeTags(a, give).ok() && linuxbee::writeTags(b, give).ok());
    QCOMPARE(picturesOf(a).size(), 1);
    QCOMPARE(picturesOf(b).size(), 1);

    const QString kept = keptFirst ? a : b;
    const QString left = keptFirst ? b : a;
    TagUndo undo(m_scratch);
    undo.begin(s(u"Remove Tags"));
    QCOMPARE(undo.write(a, take).status, WriteResult::Status::Ok);
    QCOMPARE(undo.write(b, take).status, WriteResult::Status::Ok);
    undo.end();
    QCOMPARE(QDir(m_scratch).entryList(QDir::Files | QDir::Hidden).size(), 1); // one file for both

    const TagUndo::Outcome first = undo.undo(refusing({ kept }));
    QCOMPARE(first.restored, QStringList({ left }));
    QCOMPARE(first.failed, QStringList({ kept }));
    QCOMPARE(picturesOf(left).size(), 1);
    QCOMPARE(QDir(m_scratch).entryList(QDir::Files | QDir::Hidden).size(), 1); // still needed

    const TagUndo::Outcome second = undo.undo();
    QCOMPARE(second.restored, QStringList({ kept }));
    QCOMPARE(second.failed, QStringList());
    const QList<Picture> back = picturesOf(kept);
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.at(0).data, cover.data);
    QCOMPARE(QDir(m_scratch).entryList(QDir::Files | QDir::Hidden).size(), 0); // and now it goes
}

void TagUndoKeepWholeTest::theRatingFramesOfAKeptFile()
{
    // UN-RATING with UN-KEEP: the rating is removed from a file with two rating frames; the undo
    // cannot write the file; the second try can. The frames are back as they were, both.
    const QString a = m_work + s(u"/a.mp3");
    {
        QFile sound(QStringLiteral(LINUXBEE_TEST_DATA "/tags/v1only.mp3"));
        QFile out(a);
        QVERIFY(sound.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly));
        QVERIFY(
            out.write(tag(4, { { "Some Player 9", 200, 12 }, { "second@example.invalid", 64, 9 } })
                + sound.readAll())
            > 0);
    }
    const QStringList were = popmOf(a);
    QCOMPARE(were, QStringList({ s(u"Some Player 9|200|12"), s(u"second@example.invalid|64|9") }));
    TagChanges c;
    c.fields.insert(s(u"Rating"), { });
    TagUndo undo(m_scratch);
    undo.begin(s(u"Remove Tags"));
    QCOMPARE(undo.write(a, c).status, WriteResult::Status::Ok);
    undo.end();
    QCOMPARE(popmOf(a), QStringList());

    QCOMPARE(undo.undo(refusing({ a })).failed, QStringList({ a }));
    QCOMPARE(popmOf(a), QStringList());
    QCOMPARE(undo.undo(refusing({ a })).failed, QStringList({ a })); // and a second time
    QCOMPARE(undo.undo().restored, QStringList({ a }));
    QCOMPARE(popmOf(a), were);
}

QTEST_GUILESS_MAIN(TagUndoKeepWholeTest)
#include "tst_tagundo_keep_whole.moc"
