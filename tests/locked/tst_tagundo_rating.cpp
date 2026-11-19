// LOCKED ACCEPTANCE TEST (tag undo: the rating frames come back whole). Written by the reviewer,
// not the builder. The builder makes these pass by changing src/, never by editing this file. A
// test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are UN-RATING in docs/spec/tag-undo.md. The faults this holds shut (the audit of
// undo, 2026-11-18, A1, A2, A6, B2): undo of a rating change wrote a fresh POPM frame from the
// stars. A play counter, an address, a second player's frame and the byte itself did not come back.
//
// The files are made here, byte by byte: an ID3v2 tag written out by hand in front of the sound of
// v1only.mp3. Afterwards the tag is read here too, byte by byte; the tag library is not asked what
// the frames hold. Every case works in a temporary folder.
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"
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

}

class TagUndoRatingTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void theFilesAreWhatTheySay_data() { versions(); }
    void theFilesAreWhatTheySay();
    void aCounterWithNoRating_data() { versions(); }
    void aCounterWithNoRating();
    void aRatingTakenOut_data() { versions(); }
    void aRatingTakenOut();
    void aRatingChanged_data() { versions(); }
    void aRatingChanged();
    void twoFramesAndAChange_data() { versions(); }
    void twoFramesAndAChange();
    void twoFramesTakenOut_data() { versions(); }
    void twoFramesTakenOut();
    void noFrameBefore_data() { versions(); }
    void noFrameBefore();
    void writtenTwiceInOneStep_data() { versions(); }
    void writtenTwiceInOneStep();
    void writtenTwiceBackToTheSameStars_data() { versions(); }
    void writtenTwiceBackToTheSameStars();
    void theRatingNotNamed_data() { versions(); }
    void theRatingNotNamed();
    void aRatingAndATitleTogether_data() { versions(); }
    void aRatingAndATitleTogether();
    void aWriterOfTheCaller_data() { versions(); }
    void aWriterOfTheCaller();
    void anotherFormatIsAsBefore();

private:
    void versions();
    QString make(const QList<Popm> &frames);
    // One step of one write, then undo. Returns what the file's POPM frames were in between.
    QStringList stepAndUndo(const QString &path, const TagChanges &change);
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void TagUndoRatingTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

void TagUndoRatingTest::versions()
{
    QTest::addColumn<int>("version");
    QTest::newRow("ID3v2.4") << 4;
    QTest::newRow("ID3v2.3") << 3;
}

QString TagUndoRatingTest::make(const QList<Popm> &frames)
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

QStringList TagUndoRatingTest::stepAndUndo(const QString &path, const TagChanges &change)
{
    TagUndo undo(m_work + s(u"/scratch"));
    undo.begin(s(u"Edit Tags"));
    const WriteResult written = undo.write(path, change);
    undo.end();
    if (written.status != WriteResult::Status::Ok)
        QTest::qFail("the change was not saved", __FILE__, __LINE__);
    const QStringList between = popmOf(path);
    const TagUndo::Outcome out = undo.undo();
    if (out.restored != QStringList({ path }) || !out.failed.isEmpty() || !out.changed.isEmpty())
        QTest::qFail("undo did not say the file was put back", __FILE__, __LINE__);
    return between;
}

void TagUndoRatingTest::theFilesAreWhatTheySay()
{
    // The control: the tag made by hand is read by the program as what it is meant to be, and this
    // test's own walk of it finds the frames that were put in.
    const QString a = make({ kPlayer, kSecond });
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|200|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(titleOf(a), s(u"made"));
    QCOMPARE(ratingOf(a), s(u"4")); // R-RATING: the first frame, and 200 is four stars
    const QString b = make({ kCounterOnly });
    QCOMPARE(ratingOf(b), QString()); // a byte of 0 is no rating
}

void TagUndoRatingTest::aCounterWithNoRating()
{
    // A1. A player kept its play counter in a frame with no rating. A rating is set and undone.
    // The frame is back as it was. (Undo took the frame out, and the counter with it.)
    const QString a = make({ kCounterOnly });
    const QStringList between = stepAndUndo(a, rating({ s(u"4") }));
    QCOMPARE(between, QStringList({ s(u"player@example.invalid|196|37") })); // W-RATING, as before
    QCOMPARE(popmOf(a), QStringList({ s(u"player@example.invalid|0|37") }));
    QCOMPARE(ratingOf(a), QString());
}

void TagUndoRatingTest::aRatingTakenOut()
{
    // A2. The rating is removed (the byte goes to 0 and the frame stays, for its counter:
    // W-RATING) and that is undone. The frame is back with its byte. (When the frame went, it
    // came back with no address, 196 and 0.)
    const QString a = make({ kPlayer });
    const QStringList between = stepAndUndo(a, rating({ }));
    QCOMPARE(between, QStringList({ s(u"Some Player 9|0|12") }));
    QCOMPARE(popmOf(a), QStringList({ s(u"Some Player 9|200|12") }));
    QCOMPARE(ratingOf(a), s(u"4"));
}

void TagUndoRatingTest::aRatingChanged()
{
    // B2. 200 is read as four stars; the byte the program writes for four stars is 196. Undo of a
    // change brings back 200.
    const QString a = make({ kPlayer });
    const QStringList between = stepAndUndo(a, rating({ s(u"2") }));
    QCOMPARE(between, QStringList({ s(u"Some Player 9|64|12") }));
    QCOMPARE(popmOf(a), QStringList({ s(u"Some Player 9|200|12") }));
}

void TagUndoRatingTest::twoFramesAndAChange()
{
    // A6. Two players each left a frame. A rating change is the byte of the first (W-RATING);
    // undo brings that byte back, and both are in their order.
    const QString a = make({ kPlayer, kSecond });
    const QStringList between = stepAndUndo(a, rating({ s(u"1") }));
    QCOMPARE(between, QStringList({ s(u"Some Player 9|1|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|200|12"), s(u"second@example.invalid|64|9") }));
}

void TagUndoRatingTest::twoFramesTakenOut()
{
    const QString a = make({ kSecond, kCounterOnly, kPlayer });
    const QStringList between = stepAndUndo(a, rating({ }));
    QCOMPARE(between,
        QStringList({ s(u"second@example.invalid|0|9"), s(u"player@example.invalid|0|37"),
            s(u"Some Player 9|200|12") }));
    QCOMPARE(popmOf(a),
        QStringList({ s(u"second@example.invalid|64|9"), s(u"player@example.invalid|0|37"),
            s(u"Some Player 9|200|12") }));
    QCOMPARE(ratingOf(a), s(u"2"));
}

void TagUndoRatingTest::noFrameBefore()
{
    // A file that had no rating frame has none after the undo.
    const QString a = make({ });
    const QStringList between = stepAndUndo(a, rating({ s(u"5") }));
    QCOMPARE(between, QStringList({ s(u"|255|0") }));
    QCOMPARE(popmOf(a), QStringList());
    QCOMPARE(ratingOf(a), QString());
    QCOMPARE(titleOf(a), s(u"made"));
}

void TagUndoRatingTest::writtenTwiceInOneStep()
{
    // UN-RECORD 4: what is kept is what the file held before the first write that named it.
    const QString a = make({ kPlayer, kSecond });
    TagUndo undo(m_work + s(u"/scratch"));
    undo.begin(s(u"Edit Tags"));
    QCOMPARE(undo.write(a, rating({ s(u"2") })).status, WriteResult::Status::Ok);
    QCOMPARE(undo.write(a, rating({ s(u"3") })).status, WriteResult::Status::Ok);
    undo.end();
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|128|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(undo.undo().restored, QStringList({ a }));
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|200|12"), s(u"second@example.invalid|64|9") }));
}

void TagUndoRatingTest::writtenTwiceBackToTheSameStars()
{
    // The step ends at four stars, which is what the file read before it. The frames are not what
    // they were (196, not 200), so undo still has work: it is not "unchanged".
    const QString a = make({ kPlayer, kSecond });
    TagUndo undo(m_work + s(u"/scratch"));
    undo.begin(s(u"Edit Tags"));
    QCOMPARE(undo.write(a, rating({ s(u"2") })).status, WriteResult::Status::Ok);
    QCOMPARE(undo.write(a, rating({ s(u"4") })).status, WriteResult::Status::Ok);
    undo.end();
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|196|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(ratingOf(a), s(u"4"));
    QCOMPARE(undo.undo().restored, QStringList({ a }));
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|200|12"), s(u"second@example.invalid|64|9") }));
}

void TagUndoRatingTest::theRatingNotNamed()
{
    // A change that does not name the rating does not touch the frames, and neither does its undo.
    const QString a = make({ kPlayer, kSecond });
    const QStringList were = popmOf(a);
    TagChanges c;
    c.fields.insert(s(u"Title"), { s(u"edited") });
    const QStringList between = stepAndUndo(a, c);
    QCOMPARE(between, were);
    QCOMPARE(popmOf(a), were);
    QCOMPARE(titleOf(a), s(u"made"));
}

void TagUndoRatingTest::aRatingAndATitleTogether()
{
    const QString a = make({ kPlayer, kSecond });
    TagChanges c = rating({ s(u"0.5") });
    c.fields.insert(s(u"Title"), { s(u"edited") });
    const QStringList between = stepAndUndo(a, c);
    QCOMPARE(
        between, QStringList({ s(u"Some Player 9|13|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|200|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(titleOf(a), s(u"made"));
}

void TagUndoRatingTest::aWriterOfTheCaller()
{
    // UN-RATING: the frames travel in the change undo hands to the writer. A caller's writer that
    // passes the change on to writeTags (the window's does, with its own options) brings them back.
    const QString a = make({ kPlayer, kSecond });
    const TagUndo::Writer writer = [](const QString &path, const TagChanges &changes) {
        linuxbee::WriteOptions options;
        options.keepModificationTime = false;
        return linuxbee::writeTags(path, changes, options);
    };
    TagUndo undo(m_work + s(u"/scratch"));
    undo.begin(s(u"Remove Tags"));
    QCOMPARE(undo.write(a, rating({ }), writer).status, WriteResult::Status::Ok);
    undo.end();
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|0|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(undo.undo(writer).restored, QStringList({ a }));
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|200|12"), s(u"second@example.invalid|64|9") }));
}

void TagUndoRatingTest::anotherFormatIsAsBefore()
{
    // Where the rating is a key like any other, undo is UN-RECORD as it was.
    const QString a = m_work + s(u"/a.flac");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), a));
    QFile::setPermissions(a, QFile::ReadOwner | QFile::WriteOwner);
    const QString was = ratingOf(a);
    TagUndo undo(m_work + s(u"/scratch"));
    undo.begin(s(u"Edit Tags"));
    QCOMPARE(undo.write(a, rating({ s(u"1.5") })).status, WriteResult::Status::Ok);
    undo.end();
    QCOMPARE(ratingOf(a), s(u"1.5"));
    QCOMPARE(undo.undo().restored, QStringList({ a }));
    QCOMPARE(ratingOf(a), was);
}

QTEST_GUILESS_MAIN(TagUndoRatingTest)
#include "tst_tagundo_rating.moc"
