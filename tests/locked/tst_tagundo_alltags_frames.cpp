// LOCKED ACCEPTANCE TEST (tag undo: frames that a change hands come back, whether or not it names
// the rating). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason, and
// left alone.
//
// Expected values are UN-RATING in docs/spec/tag-undo.md and G-REMOVE in
// docs/spec/tagging-tools.md. The fault this holds shut (the review of the forward rating change):
// Remove Tags with "All tags" takes every POPM frame out of a file that reads as not rated, the
// change names no Rating, and undo kept nothing to put back. A play counter and another player's
// rating were lost for good.
//
// The files are made here, byte by byte, as in tst_tagundo_rating.cpp, and read here byte by byte.
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"
#include "tagtools/tagtools.h"
#include "tagundo/tagundo.h"

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

QString ratingOf(const QString &path)
{
    return linuxbee::readTags(path).tags.first(QStringLiteral("Rating"));
}

QString titleOf(const QString &path)
{
    return linuxbee::readTags(path).tags.first(QStringLiteral("Title"));
}

TagChanges rating(const QStringList &values)
{
    TagChanges c;
    c.fields.insert(QStringLiteral("Rating"), values);
    return c;
}

const Popm kCounterOnly { "player@example.invalid", 0, 37 };
const Popm kPlayer { "Some Player 9", 200, 12 };
const Popm kSecond { "second@example.invalid", 64, 9 };

// The bodies of the POPM frames of the file, in the order of the tag, each as hex: every byte of
// them, so that "back" means back to the byte.
QStringList popmBytesOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { QStringLiteral("<cannot open>") };
    const QByteArray data = file.readAll();
    if (!data.startsWith("ID3"))
        return { QStringLiteral("<no ID3v2 tag>") };
    const int version = uchar(data.at(3));
    const qsizetype end = 10 + qsizetype(number(data.mid(6, 4), 7));
    QStringList found;
    qsizetype at = 10;
    while (at + 10 <= end && data.at(at) != '\0') {
        const qsizetype size = qsizetype(number(data.mid(at + 4, 4), version == 4 ? 7 : 8));
        if (data.mid(at, 4) == "POPM")
            found << QString::fromLatin1(data.mid(at + 10, size).toHex());
        at += 10 + size;
    }
    return found;
}

// What Remove Tags with "All tags" asks of this file (G-REMOVE).
TagChanges allTagsOf(const QString &path)
{
    linuxbee::RemoveOptions options;
    options.allTags = true;
    return linuxbee::removeTags(linuxbee::readTags(path).tags, options);
}

}

class TagUndoAllTagsFramesTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void allTagsOfAFrameThatOnlyCounts_data() { versions(); }
    void allTagsOfAFrameThatOnlyCounts();
    void allTagsOfACounterAndARatingBehindIt_data() { versions(); }
    void allTagsOfACounterAndARatingBehindIt();
    void allTagsOfARatedFile_data() { versions(); }
    void allTagsOfARatedFile();
    void framesHandedAndNoRatingNamed_data() { versions(); }
    void framesHandedAndNoRatingNamed();
    void aTitleFirstThenAllTagsInOneStep_data() { versions(); }
    void aTitleFirstThenAllTagsInOneStep();
    void allTagsThenARatingInOneStep_data() { versions(); }
    void allTagsThenARatingInOneStep();

private:
    void versions();
    QString make(const QList<Popm> &frames);
    // "All tags" as one step, then undo. Checks that nothing of POPM was left in between, and
    // that undo said the file was put back.
    void allTagsAndUndo(const QString &path);
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void TagUndoAllTagsFramesTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

void TagUndoAllTagsFramesTest::versions()
{
    QTest::addColumn<int>("version");
    QTest::newRow("ID3v2.4") << 4;
    QTest::newRow("ID3v2.3") << 3;
}

QString TagUndoAllTagsFramesTest::make(const QList<Popm> &frames)
{
    QFETCH(int, version);
    QFile sound(QStringLiteral(LINUXBEE_TEST_DATA "/tags/v1only.mp3"));
    const QString path = m_work + QStringLiteral("/a.mp3");
    QFile out(path);
    if (!sound.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)
        || out.write(tag(version, frames) + sound.readAll()) < 0)
        QTest::qFail("could not make the file", __FILE__, __LINE__);
    return path;
}

void TagUndoAllTagsFramesTest::allTagsAndUndo(const QString &path)
{
    TagUndo undo(m_work + s(u"/scratch"));
    undo.begin(s(u"Remove Tags"));
    const WriteResult written = undo.write(path, allTagsOf(path));
    undo.end();
    QCOMPARE(written.status, WriteResult::Status::Ok);
    QVERIFY2(
        popmBytesOf(path).isEmpty() || popmBytesOf(path) == QStringList({ s(u"<no ID3v2 tag>") }),
        "All tags left a POPM frame");
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ path }));
    QVERIFY(out.failed.isEmpty());
    QVERIFY(out.changed.isEmpty());
}

void TagUndoAllTagsFramesTest::allTagsOfAFrameThatOnlyCounts()
{
    // The review's case: the file reads as not rated, so the change does not name Rating. It
    // hands the frames all the same, and what it takes out must come back.
    const QString a = make({ kCounterOnly });
    QCOMPARE(ratingOf(a), QString());
    QVERIFY(!allTagsOf(a).fields.contains(s(u"Rating")));
    const QStringList before = popmBytesOf(a);
    QCOMPARE(int(before.size()), 1);
    allTagsAndUndo(a);
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(popmOf(a), QStringList({ s(u"player@example.invalid|0|37") }));
    QCOMPARE(popmBytesOf(a), before);
    QCOMPARE(titleOf(a), s(u"made"));
}

void TagUndoAllTagsFramesTest::allTagsOfACounterAndARatingBehindIt()
{
    // The second frame's rating is never read as the file's, and is another player's own.
    const QString a = make({ kCounterOnly, kPlayer });
    QCOMPARE(ratingOf(a), QString());
    const QStringList before = popmBytesOf(a);
    QCOMPARE(int(before.size()), 2);
    allTagsAndUndo(a);
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(
        popmOf(a), QStringList({ s(u"player@example.invalid|0|37"), s(u"Some Player 9|200|12") }));
    QCOMPARE(popmBytesOf(a), before);
    QCOMPARE(titleOf(a), s(u"made"));
}

void TagUndoAllTagsFramesTest::allTagsOfARatedFile()
{
    // The control: a rated file, whose change names Rating as well.
    const QString a = make({ kPlayer, kSecond });
    QVERIFY(allTagsOf(a).fields.contains(s(u"Rating")));
    const QStringList before = popmBytesOf(a);
    allTagsAndUndo(a);
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|200|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(popmBytesOf(a), before);
    QCOMPARE(ratingOf(a), s(u"4"));
}

void TagUndoAllTagsFramesTest::framesHandedAndNoRatingNamed()
{
    // The rule by itself, whoever the caller is: a change that hands the frames and names no
    // field at all.
    const QString a = make({ kCounterOnly, kPlayer });
    const QStringList before = popmBytesOf(a);
    TagChanges change;
    change.ratingFrames = QList<linuxbee::RatingFrame>();
    TagUndo undo(m_work + s(u"/scratch"));
    undo.begin(s(u"Edit Tags"));
    QCOMPARE(undo.write(a, change).status, WriteResult::Status::Ok);
    undo.end();
    QCOMPARE(popmOf(a), QStringList());
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ a }));
    QCOMPARE(popmBytesOf(a), before);
}

void TagUndoAllTagsFramesTest::aTitleFirstThenAllTagsInOneStep()
{
    // UN-RECORD 4: the first write of the step kept no frames, since it named none. The second
    // hands them; what is kept for the file gains them then.
    const QString a = make({ kCounterOnly, kPlayer });
    const QStringList before = popmBytesOf(a);
    TagChanges title;
    title.fields.insert(s(u"Title"), { s(u"another") });
    TagUndo undo(m_work + s(u"/scratch"));
    undo.begin(s(u"Edit Tags"));
    QCOMPARE(undo.write(a, title).status, WriteResult::Status::Ok);
    QCOMPARE(undo.write(a, allTagsOf(a)).status, WriteResult::Status::Ok);
    undo.end();
    QVERIFY(popmBytesOf(a).isEmpty() || popmBytesOf(a) == QStringList({ s(u"<no ID3v2 tag>") }));
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ a }));
    QCOMPARE(popmBytesOf(a), before);
    QCOMPARE(titleOf(a), s(u"made"));
}

void TagUndoAllTagsFramesTest::allTagsThenARatingInOneStep()
{
    // UN-RECORD 4 the other way: the frames kept are those from before the first write, not the
    // none that the second found.
    const QString a = make({ kCounterOnly, kPlayer });
    const QStringList before = popmBytesOf(a);
    TagUndo undo(m_work + s(u"/scratch"));
    undo.begin(s(u"Edit Tags"));
    QCOMPARE(undo.write(a, allTagsOf(a)).status, WriteResult::Status::Ok);
    QCOMPARE(undo.write(a, rating({ s(u"3") })).status, WriteResult::Status::Ok);
    undo.end();
    QCOMPARE(ratingOf(a), s(u"3"));
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ a }));
    QCOMPARE(popmBytesOf(a), before);
    QCOMPARE(ratingOf(a), QString());
}

QTEST_MAIN(TagUndoAllTagsFramesTest)
#include "tst_tagundo_alltags_frames.moc"
