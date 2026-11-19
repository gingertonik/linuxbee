// LOCKED ACCEPTANCE TEST (the tag writer: a rating changes one byte of one frame). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are W-RATING in docs/spec/tag-writing.md, and G-REMOVE in
// docs/spec/tagging-tools.md for the last five cases (Remove Tags, "All tags", still takes every
// rating frame out, one that only counts plays too: ruled 2026-11-19). The fault this holds shut
// (the audit of undo, 2026-11-18, and the ruling of 2026-11-18): rating a file took every POPM
// frame out and wrote one. Another player's frame was lost, and taking a rating out lost the play
// counter with it. linuxBee is used beside a player that reads these frames.
//
// The files are made here, byte by byte: an ID3v2 tag written out by hand in front of the sound of
// v1only.mp3. Afterwards the tag is read here too, byte by byte; the tag library is not asked what
// the frames hold. Every case works in a temporary folder.
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"
#include "tagtools/tagtools.h"

using linuxbee::TagChanges;
using linuxbee::WriteResult;

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

TagChanges rating(const QStringList &values)
{
    TagChanges c;
    c.fields.insert(QStringLiteral("Rating"), values);
    return c;
}

const Popm kPlayer { "Some Player 9", 200, 12 };
const Popm kSecond { "second@example.invalid", 64, 9 };
const Popm kCounterOnly { "player@example.invalid", 0, 37 };
const Popm kBare { "bare@example.invalid", 128, 0 };

}

class TagWriteRatingFramesTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void oneFrame_data() { versions(); }
    void oneFrame();
    void theFirstOfTwo_data() { versions(); }
    void theFirstOfTwo();
    void theFirstOfThree_data() { versions(); }
    void theFirstOfThree();
    void theFirstHasNoRatingYet_data() { versions(); }
    void theFirstHasNoRatingYet();
    void everyStep_data();
    void everyStep();
    void noFrameYet_data() { versions(); }
    void noFrameYet();
    void noTagYet();
    void takenOutALoneFrameWithNoCounter_data() { versions(); }
    void takenOutALoneFrameWithNoCounter();
    void takenOutAFrameWithACounter_data() { versions(); }
    void takenOutAFrameWithACounter();
    void takenOutTheFirstOfTwo_data() { versions(); }
    void takenOutTheFirstOfTwo();
    void takenOutWhereThereIsNone_data() { versions(); }
    void takenOutWhereThereIsNone();
    void theSameStarsAgain_data() { versions(); }
    void theSameStarsAgain();
    void anotherFieldLeavesThem_data() { versions(); }
    void anotherFieldLeavesThem();
    void aRatingAndATitleTogether_data() { versions(); }
    void aRatingAndATitleTogether();
    void removeTagsAllTagsTakesEveryFrame_data() { versions(); }
    void removeTagsAllTagsTakesEveryFrame();
    void removeTagsAllTagsTakesAFrameThatOnlyCounts_data() { versions(); }
    void removeTagsAllTagsTakesAFrameThatOnlyCounts();
    void removeTagsAllTagsOfAFileWithNoTag();
    void removeTagsTheRatingAloneKeepsTheFrames_data() { versions(); }
    void removeTagsTheRatingAloneKeepsTheFrames();
    void removeTagsAllTagsOfAnotherFormat();

private:
    void versions();
    QString make(const QList<Popm> &frames);
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void TagWriteRatingFramesTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

void TagWriteRatingFramesTest::versions()
{
    QTest::addColumn<int>("version");
    QTest::newRow("ID3v2.4") << 4;
    QTest::newRow("ID3v2.3") << 3;
}

QString TagWriteRatingFramesTest::make(const QList<Popm> &frames)
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

void TagWriteRatingFramesTest::oneFrame()
{
    // The frame stays the frame it is: its address and its counter. Its byte is the new rating.
    const QString a = make({ kPlayer });
    QCOMPARE(linuxbee::writeTags(a, rating({ s(u"3.5") })).status, WriteResult::Status::Ok);
    QCOMPARE(popmOf(a), QStringList({ s(u"Some Player 9|186|12") }));
    QCOMPARE(ratingOf(a), s(u"3.5"));
}

void TagWriteRatingFramesTest::theFirstOfTwo()
{
    // The rating is read from the first frame, so that is the one that takes the byte. The other
    // player's frame is what it was, and where it was.
    const QString a = make({ kPlayer, kSecond });
    QCOMPARE(linuxbee::writeTags(a, rating({ s(u"5") })).status, WriteResult::Status::Ok);
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|255|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(ratingOf(a), s(u"5"));
}

void TagWriteRatingFramesTest::theFirstOfThree()
{
    const QString a = make({ kSecond, kPlayer, kCounterOnly });
    QCOMPARE(linuxbee::writeTags(a, rating({ s(u"1") })).status, WriteResult::Status::Ok);
    QCOMPARE(popmOf(a),
        QStringList({ s(u"second@example.invalid|1|9"), s(u"Some Player 9|200|12"),
            s(u"player@example.invalid|0|37") }));
    QCOMPARE(ratingOf(a), s(u"1"));
}

void TagWriteRatingFramesTest::theFirstHasNoRatingYet()
{
    // The first frame counts plays and rates nothing (a byte of 0), and a later one has a rating.
    // The file reads as not rated (R-RATING: the first frame). Rating it fills the first frame.
    const QString a = make({ kCounterOnly, kPlayer });
    QCOMPARE(ratingOf(a), QString());
    QCOMPARE(linuxbee::writeTags(a, rating({ s(u"4") })).status, WriteResult::Status::Ok);
    QCOMPARE(popmOf(a),
        QStringList({ s(u"player@example.invalid|196|37"), s(u"Some Player 9|200|12") }));
    QCOMPARE(ratingOf(a), s(u"4"));
}

void TagWriteRatingFramesTest::everyStep_data()
{
    QTest::addColumn<int>("version");
    QTest::addColumn<QString>("stars");
    QTest::addColumn<int>("byte");
    const QList<QPair<const char *, int>> table { { "0.5", 13 }, { "1", 1 }, { "1.5", 54 },
        { "2", 64 }, { "2.5", 118 }, { "3", 128 }, { "3.5", 186 }, { "4", 196 }, { "4.5", 242 },
        { "5", 255 } };
    for (const auto &row : table) {
        QTest::addRow("ID3v2.%d, %s", 3 + (row.second & 1), row.first)
            << 3 + (row.second & 1) << QString::fromLatin1(row.first) << row.second;
    }
}

void TagWriteRatingFramesTest::everyStep()
{
    QFETCH(QString, stars);
    QFETCH(int, byte);
    const QString a = make({ kCounterOnly, kPlayer });
    QCOMPARE(linuxbee::writeTags(a, rating({ stars })).status, WriteResult::Status::Ok);
    QCOMPARE(popmOf(a),
        QStringList({ QStringLiteral("player@example.invalid|%1|37").arg(byte),
            s(u"Some Player 9|200|12") }));
    QCOMPARE(ratingOf(a), stars);
}

void TagWriteRatingFramesTest::noFrameYet()
{
    // A new frame: no address, no plays.
    const QString a = make({ });
    QCOMPARE(linuxbee::writeTags(a, rating({ s(u"3") })).status, WriteResult::Status::Ok);
    QCOMPARE(popmOf(a), QStringList({ s(u"|128|0") }));
    QCOMPARE(ratingOf(a), s(u"3"));
}

void TagWriteRatingFramesTest::noTagYet()
{
    const QString a = m_work + s(u"/a.mp3");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/v1only.mp3"), a));
    QVERIFY(!bytesOf(a).startsWith("ID3"));
    QCOMPARE(linuxbee::writeTags(a, rating({ s(u"3") })).status, WriteResult::Status::Ok);
    QCOMPARE(popmOf(a), QStringList({ s(u"|128|0") }));
}

void TagWriteRatingFramesTest::takenOutALoneFrameWithNoCounter()
{
    // The frame says nothing once its rating is gone: it goes.
    const QString a = make({ kBare });
    QCOMPARE(linuxbee::writeTags(a, rating({ })).status, WriteResult::Status::Ok);
    QCOMPARE(popmOf(a), QStringList());
    QCOMPARE(ratingOf(a), QString());
}

void TagWriteRatingFramesTest::takenOutAFrameWithACounter()
{
    // The plays are not the rating's to take: the byte becomes 0 and the frame stays.
    const QString a = make({ kPlayer });
    QCOMPARE(linuxbee::writeTags(a, rating({ })).status, WriteResult::Status::Ok);
    QCOMPARE(popmOf(a), QStringList({ s(u"Some Player 9|0|12") }));
    QCOMPARE(ratingOf(a), QString());
}

void TagWriteRatingFramesTest::takenOutTheFirstOfTwo()
{
    // The first frame has no counter, and it stays all the same, with a byte of 0: were it taken
    // out, the second player's frame would be first, and the file would read as rated two stars.
    const QString a = make({ kBare, kSecond });
    QCOMPARE(linuxbee::writeTags(a, rating({ })).status, WriteResult::Status::Ok);
    QCOMPARE(popmOf(a),
        QStringList({ s(u"bare@example.invalid|0|0"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(ratingOf(a), QString());
}

void TagWriteRatingFramesTest::takenOutWhereThereIsNone()
{
    // W-UNCHANGED: nothing to take out, with frames that rate nothing and with none.
    const QString a = make({ kCounterOnly, kPlayer });
    const QByteArray was = bytesOf(a);
    QCOMPARE(linuxbee::writeTags(a, rating({ })).status, WriteResult::Status::Unchanged);
    QCOMPARE(bytesOf(a), was);
    QFile::remove(a);
    const QString b = make({ });
    const QByteArray wasB = bytesOf(b);
    QCOMPARE(linuxbee::writeTags(b, rating({ })).status, WriteResult::Status::Unchanged);
    QCOMPARE(bytesOf(b), wasB);
}

void TagWriteRatingFramesTest::theSameStarsAgain()
{
    const QString a = make({ kPlayer, kSecond });
    const QByteArray was = bytesOf(a);
    QCOMPARE(linuxbee::writeTags(a, rating({ s(u"4") })).status, WriteResult::Status::Unchanged);
    QCOMPARE(bytesOf(a), was);
}

void TagWriteRatingFramesTest::anotherFieldLeavesThem()
{
    TagChanges c;
    c.fields.insert(s(u"Title"), { s(u"another") });
    const QString a = make({ kPlayer, kSecond, kCounterOnly });
    QCOMPARE(linuxbee::writeTags(a, c).status, WriteResult::Status::Ok);
    QCOMPARE(popmOf(a),
        QStringList({ s(u"Some Player 9|200|12"), s(u"second@example.invalid|64|9"),
            s(u"player@example.invalid|0|37") }));
}

void TagWriteRatingFramesTest::aRatingAndATitleTogether()
{
    TagChanges c = rating({ s(u"2") });
    c.fields.insert(s(u"Title"), { s(u"another") });
    const QString a = make({ kPlayer, kSecond });
    QCOMPARE(linuxbee::writeTags(a, c).status, WriteResult::Status::Ok);
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|64|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(linuxbee::readTags(a).tags.first(s(u"Title")), s(u"another"));
}

void TagWriteRatingFramesTest::removeTagsAllTagsTakesEveryFrame()
{
    // G-REMOVE: "All tags" on a file with an ID3v2 tag. The change it makes carries an empty list
    // of rating frames, and the writer takes that as it is (W-RATING, "the frames whole"): every
    // frame goes, the counter and the other player's frame with them. "All" means all.
    const QString a = make({ kPlayer, kSecond });
    linuxbee::RemoveOptions options;
    options.allTags = true;
    const TagChanges c = linuxbee::removeTags(linuxbee::readTags(a).tags, options);
    QVERIFY(c.ratingFrames.has_value());
    QCOMPARE(int(c.ratingFrames->size()), 0);
    QVERIFY(bytesOf(a).contains("POPM"));
    QCOMPARE(linuxbee::writeTags(a, c).status, WriteResult::Status::Ok);
    // Nothing is left for the tag to hold, so the tag itself may be gone: the bytes are asked.
    QVERIFY(!bytesOf(a).contains("POPM"));
    QCOMPARE(ratingOf(a), QString());
    QCOMPARE(linuxbee::readTags(a).tags.first(s(u"Title")), QString());
}

void TagWriteRatingFramesTest::removeTagsAllTagsTakesAFrameThatOnlyCounts()
{
    // The file reads as not rated: its one rating frame counts plays and rates nothing. "All
    // tags" takes that frame out too. So it does where a second frame has a rating that is
    // never read.
    for (const QList<Popm> &frames :
        { QList<Popm> { kCounterOnly }, QList<Popm> { kCounterOnly, kPlayer } }) {
        const QString a = make(frames);
        QCOMPARE(ratingOf(a), QString());
        linuxbee::RemoveOptions options;
        options.allTags = true;
        const TagChanges c = linuxbee::removeTags(linuxbee::readTags(a).tags, options);
        QVERIFY(c.ratingFrames.has_value());
        QCOMPARE(int(c.ratingFrames->size()), 0);
        QVERIFY(!c.fields.contains(s(u"Rating"))); // there is no rating to take out
        QVERIFY(bytesOf(a).contains("POPM"));
        QCOMPARE(linuxbee::writeTags(a, c).status, WriteResult::Status::Ok);
        QVERIFY(!bytesOf(a).contains("POPM"));
        QVERIFY(QFile::remove(a));
    }
}

void TagWriteRatingFramesTest::removeTagsAllTagsOfAFileWithNoTag()
{
    // Nothing is there, so nothing is asked for: the change is empty, and writing it is
    // `Unchanged` and leaves every byte.
    QFile sound(QStringLiteral(LINUXBEE_TEST_DATA "/tags/v1only.mp3"));
    QVERIFY(sound.open(QIODevice::ReadOnly));
    QByteArray bare = sound.readAll();
    if (bare.right(128).startsWith("TAG"))
        bare.chop(128); // the ID3v1 tag at the end
    QVERIFY(!bare.startsWith("ID3") && !bare.right(128).startsWith("TAG"));
    const QString a = m_work + s(u"/a.mp3");
    {
        QFile out(a);
        QVERIFY(out.open(QIODevice::WriteOnly) && out.write(bare) == bare.size());
    }
    QCOMPARE(linuxbee::readTags(a).tags.tagTypes, QStringList());
    linuxbee::RemoveOptions options;
    options.allTags = true;
    const TagChanges c = linuxbee::removeTags(linuxbee::readTags(a).tags, options);
    QVERIFY(c.isEmpty());
    QCOMPARE(linuxbee::writeTags(a, c).status, WriteResult::Status::Unchanged);
    QCOMPARE(bytesOf(a), bare);
}

void TagWriteRatingFramesTest::removeTagsTheRatingAloneKeepsTheFrames()
{
    // G-REMOVE with the field named: that is taking the rating out, no more (W-RATING).
    const QString a = make({ kPlayer, kSecond });
    linuxbee::RemoveOptions options;
    options.fields = QStringList({ s(u"Rating") });
    const TagChanges c = linuxbee::removeTags(linuxbee::readTags(a).tags, options);
    QVERIFY(!c.ratingFrames.has_value());
    QCOMPARE(linuxbee::writeTags(a, c).status, WriteResult::Status::Ok);
    QCOMPARE(
        popmOf(a), QStringList({ s(u"Some Player 9|0|12"), s(u"second@example.invalid|64|9") }));
    QCOMPARE(linuxbee::readTags(a).tags.first(s(u"Title")), s(u"made"));
}

void TagWriteRatingFramesTest::removeTagsAllTagsOfAnotherFormat()
{
    // The empty list means nothing to a file with no ID3v2 tag family, and its rating goes as a
    // key like any other.
    const QString a = m_work + s(u"/a.flac");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), a));
    QCOMPARE(linuxbee::writeTags(a, rating({ s(u"3") })).status, WriteResult::Status::Ok);
    QCOMPARE(ratingOf(a), s(u"3"));
    linuxbee::RemoveOptions options;
    options.allTags = true;
    const TagChanges c = linuxbee::removeTags(linuxbee::readTags(a).tags, options);
    QVERIFY(!c.ratingFrames.has_value());
    QCOMPARE(linuxbee::writeTags(a, c).status, WriteResult::Status::Ok);
    QCOMPARE(ratingOf(a), QString());
}

QTEST_GUILESS_MAIN(TagWriteRatingFramesTest)
#include "tst_tagwrite_rating_frames.moc"
