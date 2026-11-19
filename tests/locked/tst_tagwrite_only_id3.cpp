// LOCKED ACCEPTANCE TEST (tag writing: a save changes only what it names, ID3v2). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are the rule W-ONLY in docs/spec/tag-writing.md.
//
// How it looks: this test reads the ID3v2 tag itself, as the ID3 documents lay it out (ten bytes
// of header; then frames of four letters, four bytes of size, two of flags and the content), and
// compares frames as bytes. The tag library is not asked what a file holds: it is what is being
// tested. The fixtures only24.mp3, only23.mp3 and onlyape.mp3 were each saved once by the tag
// library when they were made (tests/data/tags/make_only_fixtures.py says why), so a frame the tag
// library leaves alone is the same bytes before and after.
//   BYTES  compared or found in the file's raw bytes
//   RT     a round trip through readTags, beside a BYTES check
#include <algorithm>

#include <QFile>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

using linuxbee::Picture;
using linuxbee::TagChanges;
using linuxbee::TrackTags;
using linuxbee::WriteOptions;
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

struct Frame {
    QByteArray id;
    QByteArray whole; // header and content, as they are in the file
    QByteArray body; // the content
};

struct Tag {
    bool ok = false;
    int version = -1;
    qsizetype end = 0; // the first byte after the tag and its padding
    QList<Frame> frames;
};

qsizetype syncsafe(const uchar *b)
{
    return (b[0] & 0x7f) << 21 | (b[1] & 0x7f) << 14 | (b[2] & 0x7f) << 7 | (b[3] & 0x7f);
}

// BYTES: the ID3v2 tag that begins at `at`. Not ok when anything is not as the documents say, or
// uses what this test does not read: tag flags, frame flags, bytes after the frames that are not
// padding.
Tag tagAt(const QByteArray &d, qsizetype at)
{
    Tag t;
    if (at < 0 || d.size() < at + 10 || d.mid(at, 3) != "ID3")
        return t;
    const uchar *h = reinterpret_cast<const uchar *>(d.constData()) + at;
    t.version = h[3];
    if ((t.version != 3 && t.version != 4) || h[5] != 0)
        return t;
    t.end = at + 10 + syncsafe(h + 6);
    if (t.end > d.size())
        return t;
    qsizetype p = at + 10;
    while (p + 10 <= t.end && d.at(p) != '\0') {
        const uchar *f = reinterpret_cast<const uchar *>(d.constData()) + p;
        // The size of a frame is four bytes of seven bits in ID3v2.4 and a plain number in 2.3.
        const qsizetype n
            = t.version == 4 ? syncsafe(f + 4) : qsizetype(qFromBigEndian<quint32>(f + 4));
        if (p + 10 + n > t.end || f[8] != 0 || f[9] != 0)
            return t;
        t.frames.append(Frame { d.mid(p, 4), d.mid(p, 10 + n), d.mid(p + 10, n) });
        p += 10 + n;
    }
    for (qsizetype i = p; i < t.end; ++i) {
        if (d.at(i) != '\0')
            return t;
    }
    t.ok = true;
    return t;
}

// BYTES: an MP3 begins with its tag; in a WAV file the tag is the content of the chunk "id3 " or
// "ID3 ", after the chunk's four size bytes.
Tag tagOf(const QByteArray &d)
{
    if (!d.startsWith("RIFF"))
        return tagAt(d, 0);
    qsizetype at = d.indexOf("id3 ");
    if (at < 0)
        at = d.indexOf("ID3 ");
    return at < 0 ? Tag() : tagAt(d, at + 8);
}

// BYTES: the texts of a frame's content from `from` on. The byte at 0 says how they are written:
// 0 Latin-1, 1 UTF-16 with a byte order mark, 2 UTF-16 big-endian, 3 UTF-8. A zero (two, in
// UTF-16) stands between one text and the next.
QStringList texts(const QByteArray &body, qsizetype from)
{
    QStringList out;
    if (body.size() < from || body.isEmpty())
        return out;
    const int how = body.at(0);
    const QByteArray rest = body.mid(from);
    QList<QByteArray> parts;
    if (how == 1 || how == 2) {
        qsizetype start = 0;
        for (qsizetype i = 0; i + 1 < rest.size(); i += 2) {
            if (rest.at(i) == '\0' && rest.at(i + 1) == '\0') {
                parts.append(rest.mid(start, i - start));
                start = i + 2;
            }
        }
        parts.append(rest.mid(start));
    } else {
        parts = rest.split('\0');
    }
    for (const QByteArray &part : parts) {
        if (how == 1)
            out.append(QStringDecoder(QStringDecoder::Utf16).decode(part));
        else if (how == 2)
            out.append(QStringDecoder(QStringDecoder::Utf16BE).decode(part));
        else if (how == 3)
            out.append(QString::fromUtf8(part));
        else
            out.append(QString::fromLatin1(part));
    }
    return out;
}

bool hasLanguage(const Frame &f)
{
    return f.id == "COMM" || f.id == "USLT";
}

// What a frame is called here. A user text frame, a user link, a comment and lyrics are told
// apart by their description: "TXXX:Label", "COMM:" (no description), "COMM:iTunNORM". Every other
// frame is called by its four letters.
QString nameOf(const Frame &f)
{
    const QString id = QString::fromLatin1(f.id);
    if (f.id == "TXXX" || f.id == "WXXX")
        return id + QLatin1Char(':') + texts(f.body, 1).value(0);
    if (hasLanguage(f))
        return id + QLatin1Char(':') + texts(f.body, 4).value(0);
    return id;
}

QList<Frame> called(const Tag &t, const QStringList &names)
{
    QList<Frame> out;
    for (const Frame &f : t.frames) {
        if (names.contains(nameOf(f)))
            out.append(f);
    }
    return out;
}

QList<QByteArray> notCalled(const Tag &t, const QStringList &names)
{
    QList<QByteArray> out;
    for (const Frame &f : t.frames) {
        if (!names.contains(nameOf(f)))
            out.append(f.whole);
    }
    return out;
}

// BYTES: the three letters of a comment's or lyrics frame's language, and its text.
QString languageOf(const Frame &f)
{
    return QString::fromLatin1(f.body.mid(1, 3));
}

QString textOf(const Frame &f)
{
    return texts(f.body, 4).value(1);
}

QStringList languagesOf(const QList<Frame> &frames)
{
    QStringList out;
    for (const Frame &f : frames)
        out.append(languageOf(f));
    return out;
}

QStringList textsOf(const QList<Frame> &frames)
{
    QStringList out;
    for (const Frame &f : frames)
        out.append(textOf(f));
    return out;
}

// BYTES: the ID3v1 tag is the last 128 bytes of a file when they begin "TAG".
QByteArray id3v1Of(const QByteArray &d)
{
    return d.size() >= 128 && d.mid(d.size() - 128, 3) == "TAG" ? d.right(128) : QByteArray();
}

// BYTES: an APEv2 tag as the tag library writes it begins and ends with 32 bytes that begin
// "APETAGEX".
QByteArray apeOf(const QByteArray &d)
{
    const qsizetype first = d.indexOf("APETAGEX");
    const qsizetype last = d.lastIndexOf("APETAGEX");
    return first < 0 || last == first ? QByteArray() : d.mid(first, last + 32 - first);
}

// BYTES: the sound of an MP3 fixture: what stands after the ID3v2 tag and before the APEv2 tag
// and the ID3v1 tag.
QByteArray soundOf(const QByteArray &d)
{
    const Tag t = tagOf(d);
    qsizetype end = d.size() - id3v1Of(d).size();
    const qsizetype ape = d.indexOf("APETAGEX");
    if (ape >= 0)
        end = ape;
    return d.mid(t.end, end - t.end);
}

QString listed(const QList<QByteArray> &frames)
{
    QStringList out;
    for (const QByteArray &f : frames)
        out.append(QString::fromLatin1(f.left(4)) + QLatin1Char('(') + QString::number(f.size())
            + QLatin1Char(')'));
    return out.join(QLatin1Char(' '));
}

struct Step {
    WriteResult result;
    QByteArray was;
    QByteArray is;
    Tag before;
    Tag after;
};

// One save, with the file read as bytes before it and after it.
Step step(const QString &path, const TagChanges &changes, const WriteOptions &options = { })
{
    Step st;
    st.was = bytesOf(path);
    st.result = linuxbee::writeTags(path, changes, options);
    st.is = bytesOf(path);
    st.before = tagOf(st.was);
    st.after = tagOf(st.is);
    return st;
}

// W-ONLY. Empty when the save was made and every frame that is not named is, after it, the bytes
// it was, in the order it had among the frames not named; otherwise what is wrong.
QString whatElse(const Step &st, const QStringList &named)
{
    if (st.result.status != WriteResult::Status::Ok)
        return QStringLiteral("the save was not made: status %1").arg(int(st.result.status));
    if (!st.before.ok || !st.after.ok)
        return QStringLiteral("a tag could not be read as ID3v2 frames (before: %1, after: %2)")
            .arg(st.before.ok)
            .arg(st.after.ok);
    if (st.before.version != st.after.version)
        return QStringLiteral("ID3v2.%1 became ID3v2.%2")
            .arg(st.before.version)
            .arg(st.after.version);
    const QList<QByteArray> a = notCalled(st.before, named);
    const QList<QByteArray> b = notCalled(st.after, named);
    if (a == b)
        return { };
    QList<QByteArray> gone;
    QList<QByteArray> rest = b;
    for (const QByteArray &f : a) {
        if (!rest.removeOne(f))
            gone.append(f);
    }
    if (gone.isEmpty() && rest.isEmpty())
        return QStringLiteral("frames that were not named stand in another order");
    return QStringLiteral(
        "not named, and no longer there as they were: [%1]; not named, and new or different: [%2]")
        .arg(listed(gone), listed(rest));
}

#define ONLY(st, named)                                                                            \
    do {                                                                                           \
        const QString somethingElse = whatElse(st, named);                                         \
        QVERIFY2(somethingElse.isEmpty(), qPrintable(somethingElse));                              \
    } while (false)

struct Box {
    QTemporaryDir dir;
    QString copy(const QString &fixture)
    {
        const QString to = dir.path() + QLatin1Char('/') + fixture;
        QFile::remove(to);
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + fixture, to))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return to;
    }
};

TagChanges field(const char16_t *name, const QStringList &values)
{
    TagChanges c;
    c.fields.insert(s(name), values);
    return c;
}

TagChanges other(const char16_t *key, const QStringList &values)
{
    TagChanges c;
    c.other.insert(s(key), values);
    return c;
}

// Picture bytes that are in no fixture.
Picture cover(int seed)
{
    Picture p;
    p.type = QStringLiteral("Front Cover");
    p.mimeType = QStringLiteral("image/jpeg");
    p.data = QByteArray::fromHex("ffd8ffe000104a46494600");
    for (int i = 0; i < 700; ++i)
        p.data.append(char((i * 7 + seed * 13) % 251));
    p.data.append(QByteArray::fromHex("ffd9"));
    p.byteCount = p.data.size();
    return p;
}

// BYTES: the pairs of a list of people (TIPL, TMCL, and IPLS in ID3v2.3): role, name, role, name.
struct Pair {
    QString role;
    QString name;
    bool operator==(const Pair &o) const { return role == o.role && name == o.name; }
};

QList<Pair> pairsOf(const QList<Frame> &frames)
{
    QList<Pair> out;
    for (const Frame &f : frames) {
        const QStringList parts = texts(f.body, 1);
        for (qsizetype i = 0; i + 1 < parts.size(); i += 2)
            out.append(Pair { parts.at(i), parts.at(i + 1) });
    }
    return out;
}

QList<Pair> withRole(const QList<Pair> &pairs, const QString &role, bool with)
{
    QList<Pair> out;
    for (const Pair &p : pairs) {
        if ((p.role.compare(role, Qt::CaseInsensitive) == 0) == with)
            out.append(p);
    }
    return out;
}

QString shown(const QList<Pair> &pairs)
{
    QStringList out;
    for (const Pair &p : pairs)
        out.append(p.role + QLatin1Char('=') + p.name);
    return out.join(QStringLiteral(" | "));
}

void bothVersions()
{
    QTest::addColumn<QString>("fixture");
    QTest::newRow("ID3v2.4") << QStringLiteral("only24.mp3");
    QTest::newRow("ID3v2.3") << QStringLiteral("only23.mp3");
}

}

class TagWriteOnlyId3Test : public QObject {
    Q_OBJECT
private slots:
    void theFixturesAreAsThisTestTakesThem();
    void aTitle_data() { bothVersions(); }
    void aTitle();
    void aFieldOfTwoValues_data() { bothVersions(); }
    void aFieldOfTwoValues();
    void aNumber_data() { bothVersions(); }
    void aNumber();
    void theYear_data() { bothVersions(); }
    void theYear();
    void aComment_data() { bothVersions(); }
    void aComment();
    void threeComments_data() { bothVersions(); }
    void threeComments();
    void lyricsWhereThereWereNone_data() { bothVersions(); }
    void lyricsWhereThereWereNone();
    void theLyrics_data() { bothVersions(); }
    void theLyrics();
    void lyricsOfTwoValues_data() { bothVersions(); }
    void lyricsOfTwoValues();
    void theRating_data() { bothVersions(); }
    void theRating();
    void love_data() { bothVersions(); }
    void love();
    void anotherTag_data() { bothVersions(); }
    void anotherTag();
    void aFieldTakenOut_data() { bothVersions(); }
    void aFieldTakenOut();
    void aFieldTheFileHoldsTwice_data() { bothVersions(); }
    void aFieldTheFileHoldsTwice();
    void onePersonOfAList_data() { bothVersions(); }
    void onePersonOfAList();
    void oneMusicianOfAList_data() { bothVersions(); }
    void oneMusicianOfAList();
    void aPersonHeldInAUserTextFrame_data() { bothVersions(); }
    void aPersonHeldInAUserTextFrame();
    void thePictures_data() { bothVersions(); }
    void thePictures();
    void aSaveAndTheSaveThatTakesItBack_data() { bothVersions(); }
    void aSaveAndTheSaveThatTakesItBack();
    void theApeTagOfAnMp3IsLeftAsItIs();
    void aFileWithNoId3v2TagGetsItsValues();
    void aFileReadFromItsApeTagGetsItsValues_data();
    void aFileReadFromItsApeTagGetsItsValues();
    void afterAnotherVersionWasAskedFor();
    void aWavFileToo();
};

void TagWriteOnlyId3Test::theFixturesAreAsThisTestTakesThem()
{
    // Not a rule of the program: what the cases below rest on. If a fixture is made again and
    // differs, this says so before they do.
    struct Want {
        const char *fixture;
        int version;
        int frames;
    };
    for (const Want &want : { Want { "only24.mp3", 4, 47 }, Want { "only23.mp3", 3, 44 },
             Want { "onlyape.mp3", 4, 47 } }) {
        const QByteArray d = bytesOf(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/")
            + QLatin1String(want.fixture));
        const Tag t = tagOf(d);
        QVERIFY2(t.ok, want.fixture);
        QCOMPARE(t.version, want.version);
        QCOMPARE(int(t.frames.size()), want.frames);
        QCOMPARE(languagesOf(called(t, { s(u"COMM:") })), QStringList({ s(u"eng"), s(u"deu") }));
        QCOMPARE(textsOf(called(t, { s(u"COMM:") })),
            QStringList({ s(u"English comment"), s(u"Deutscher Kommentar") }));
        QCOMPARE(languagesOf(called(t, { s(u"USLT:") })), QStringList({ s(u"fra"), s(u"eng") }));
        QCOMPARE(textsOf(called(t, { s(u"USLT:") })),
            QStringList({ s(u"Paroles francaises"), s(u"English lyrics line") }));
        QCOMPARE(int(called(t, { s(u"COMM:iTunNORM") }).size()), 1);
        QCOMPARE(int(called(t, { s(u"POPM") }).size()), 2);
        QCOMPARE(int(called(t, { s(u"APIC") }).size()), 2);
        QCOMPARE(int(called(t, { s(u"TPUB"), s(u"TXXX:Label") }).size()), 2);
        QCOMPARE(int(called(t, { s(u"TIT1"), s(u"TXXX:WORK") }).size()), 2);
        QCOMPARE(int(called(t, { s(u"TXXX:MOOD") }).size()), 1);
        QCOMPARE(int(called(t, { s(u"TXXX:My Custom Thing") }).size()), 1);
        QCOMPARE(int(id3v1Of(d).size()), 128);
        QVERIFY2(soundOf(d).size() > 4000, want.fixture);
        QCOMPARE(apeOf(d).isEmpty(), QByteArray(want.fixture) != "onlyape.mp3");
    }
    const Tag v24
        = tagOf(bytesOf(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/only24.mp3")));
    QCOMPARE(shown(pairsOf(called(v24, { s(u"TIPL") }))),
        s(u"producer=A Producer | engineer=An Engineer | tea=A Tea Maker"));
    QCOMPARE(shown(pairsOf(called(v24, { s(u"TMCL") }))), s(u"guitar=A Guitarist"));
    const Tag v23
        = tagOf(bytesOf(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/only23.mp3")));
    QCOMPARE(shown(pairsOf(called(v23, { s(u"IPLS") }))),
        s(u"tea=A Tea Maker | guitar=A Guitarist | producer=A Producer | engineer=An Engineer"));
}

void TagWriteOnlyId3Test::aTitle()
{
    // W-ONLY: the finding itself. A title, and the two comments, the two lyrics, the label held
    // twice and the list of people are what they were.
    QFETCH(QString, fixture);
    Box box;
    const Step st = step(box.copy(fixture), field(u"Title", { s(u"New Title") }));
    ONLY(st, QStringList({ s(u"TIT2") }));
    QCOMPARE(st.after.frames.size(), st.before.frames.size());
    const QList<Frame> title = called(st.after, { s(u"TIT2") });
    QCOMPARE(int(title.size()), 1);
    QCOMPARE(texts(title.value(0).body, 1), QStringList({ s(u"New Title") })); // BYTES
    QCOMPARE(soundOf(st.is), soundOf(st.was)); // W-KEEP
    // W-ID3V1: the ID3v1 tag holds the current values.
    QVERIFY(id3v1Of(st.is).startsWith(QByteArray("TAGNew Title\0", 13)));
    QCOMPARE(
        linuxbee::readTags(box.dir.path() + QLatin1Char('/') + fixture).tags.first(s(u"Title")),
        s(u"New Title")); // RT
}

void TagWriteOnlyId3Test::aFieldOfTwoValues()
{
    QFETCH(QString, fixture);
    Box box;
    const Step st = step(box.copy(fixture), field(u"Artist", { s(u"One"), s(u"Two") }));
    ONLY(st, QStringList({ s(u"TPE1") }));
    QCOMPARE(int(called(st.after, { s(u"TPE1") }).size()), 1);
    QCOMPARE(soundOf(st.is), soundOf(st.was));
}

void TagWriteOnlyId3Test::aNumber()
{
    // W-NUMBERS: the number and its count are one frame.
    QFETCH(QString, fixture);
    Box box;
    const Step st = step(box.copy(fixture), field(u"Track#", { s(u"4") }));
    ONLY(st, QStringList({ s(u"TRCK") }));
    const QList<Frame> track = called(st.after, { s(u"TRCK") });
    QCOMPARE(int(track.size()), 1);
    QCOMPARE(texts(track.value(0).body, 1), QStringList({ s(u"4/12") })); // BYTES
}

void TagWriteOnlyId3Test::theYear()
{
    // The date is TDRC in ID3v2.4 and TYER with TDAT (and TIME) in ID3v2.3.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const Step st = step(path, field(u"Year", { s(u"2002-06-07") }));
    ONLY(st, QStringList({ s(u"TDRC"), s(u"TYER"), s(u"TDAT"), s(u"TIME") }));
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Year")), s(u"2002-06-07")); // RT
}

void TagWriteOnlyId3Test::aComment()
{
    // W-ONLY: the comments with no description are the field's; the one with a description is
    // another thing and stays. The new frame is a comment frame in the language of the first.
    QFETCH(QString, fixture);
    Box box;
    const Step st = step(box.copy(fixture), field(u"Comment", { s(u"One new comment") }));
    ONLY(st, QStringList({ s(u"COMM:") }));
    const QList<Frame> comments = called(st.after, { s(u"COMM:") });
    QCOMPARE(textsOf(comments), QStringList({ s(u"One new comment") })); // BYTES
    QCOMPARE(languagesOf(comments), QStringList({ s(u"eng") }));
    QCOMPARE(int(called(st.after, { s(u"COMM:iTunNORM") }).size()), 1);
}

void TagWriteOnlyId3Test::threeComments()
{
    // W-ONLY: a comment frame a value, never a user text frame, while each value has the place of
    // a frame that was there, and takes its language. ID3v2 allows one comment for a language
    // and a description, so the third value gets no frame of its own: it is a new line of the
    // last frame.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const Step st = step(path, field(u"Comment", { s(u"a"), s(u"b"), s(u"c") }));
    ONLY(st, QStringList({ s(u"COMM:") }));
    const QList<Frame> comments = called(st.after, { s(u"COMM:") });
    QCOMPARE(textsOf(comments), QStringList({ s(u"a"), s(u"b\nc") })); // BYTES
    QCOMPARE(languagesOf(comments), QStringList({ s(u"eng"), s(u"deu") }));
    QCOMPARE(linuxbee::readTags(path).tags.fields.value(s(u"Comment")),
        QStringList({ s(u"a"), s(u"b\nc") })); // RT
}

void TagWriteOnlyId3Test::lyricsWhereThereWereNone()
{
    // W-ONLY: with no frame to take the place of, two values are one lyrics frame of two lines.
    // Two frames would have one language and one description, which ID3v2 forbids and which
    // another tagger reads as one frame, losing the other.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const QStringList lyrics({ s(u"USLT:"), s(u"USLT:LYRICS") });
    Step st = step(path, field(u"Lyrics", { }));
    ONLY(st, lyrics);
    QCOMPARE(int(called(st.after, lyrics).size()), 0);
    st = step(path, field(u"Lyrics", { s(u"p"), s(u"q") }));
    ONLY(st, lyrics);
    QCOMPARE(textsOf(called(st.after, lyrics)), QStringList({ s(u"p\nq") })); // BYTES
    QCOMPARE(linuxbee::readTags(path).tags.fields.value(s(u"Lyrics")),
        QStringList({ s(u"p\nq") })); // RT
}

void TagWriteOnlyId3Test::theLyrics()
{
    // W-ONLY: lyrics are written as lyrics (USLT), where a player looks for them.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const Step st = step(path, field(u"Lyrics", { s(u"New lyrics") }));
    ONLY(st, QStringList({ s(u"USLT:") }));
    const QList<Frame> lyrics = called(st.after, { s(u"USLT:") });
    QCOMPARE(textsOf(lyrics), QStringList({ s(u"New lyrics") })); // BYTES
    QCOMPARE(languagesOf(lyrics), QStringList({ s(u"fra") }));
    QCOMPARE(linuxbee::readTags(path).tags.fields.value(s(u"Lyrics")),
        QStringList({ s(u"New lyrics") })); // RT
}

void TagWriteOnlyId3Test::lyricsOfTwoValues()
{
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const Step st = step(path, field(u"Lyrics", { s(u"p"), s(u"q") }));
    ONLY(st, QStringList({ s(u"USLT:") }));
    const QList<Frame> lyrics = called(st.after, { s(u"USLT:") });
    QCOMPARE(textsOf(lyrics), QStringList({ s(u"p"), s(u"q") })); // BYTES
    QCOMPARE(languagesOf(lyrics), QStringList({ s(u"fra"), s(u"eng") }));
    QCOMPARE(linuxbee::readTags(path).tags.fields.value(s(u"Lyrics")),
        QStringList({ s(u"p"), s(u"q") })); // RT
}

void TagWriteOnlyId3Test::theRating()
{
    // W-RATING: the first rating frame takes the byte, and keeps its address and its counter; the
    // other one is the bytes it was. BYTES: the address, a zero, the rating byte, four bytes of
    // counter.
    QFETCH(QString, fixture);
    Box box;
    const Step st = step(box.copy(fixture), field(u"Rating", { s(u"3.5") }));
    ONLY(st, QStringList({ s(u"POPM") }));
    const QList<Frame> rating = called(st.after, { s(u"POPM") });
    QCOMPARE(int(rating.size()), 2);
    QCOMPARE(rating.value(0).body, QByteArray("MusicBee\0\xba\0\0\0\x07", 14));
    QCOMPARE(rating.value(1).body, called(st.before, { s(u"POPM") }).value(1).body);
}

void TagWriteOnlyId3Test::love()
{
    // A field the file does not have: one frame more, and the ID3v1 tag, which has no place for
    // it, is the bytes it was.
    QFETCH(QString, fixture);
    Box box;
    const Step st = step(box.copy(fixture), field(u"Love", { s(u"L") }));
    ONLY(st, QStringList({ s(u"TXXX:LOVE RATING") }));
    QCOMPARE(int(called(st.after, { s(u"TXXX:LOVE RATING") }).size()), 1);
    QCOMPARE(st.after.frames.size(), st.before.frames.size() + 1);
    QCOMPARE(id3v1Of(st.is), id3v1Of(st.was));
    QCOMPARE(soundOf(st.is), soundOf(st.was));
}

void TagWriteOnlyId3Test::anotherTag()
{
    // W-FIELDS: a tag outside the catalogue, changed and then a new one. The file writes its
    // description in small letters; the key is the same key.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const QStringList custom({ s(u"TXXX:My Custom Thing"), s(u"TXXX:MY CUSTOM THING") });
    Step st = step(path, other(u"MY CUSTOM THING", { s(u"changed") }));
    ONLY(st, custom);
    QCOMPARE(int(called(st.after, custom).size()), 1);
    QCOMPARE(linuxbee::readTags(path).tags.other.value(s(u"MY CUSTOM THING")),
        QStringList({ s(u"changed") })); // RT
    st = step(path, other(u"A NEW KEY", { s(u"its value") }));
    ONLY(st, QStringList({ s(u"TXXX:A NEW KEY") }));
    QCOMPARE(int(called(st.after, { s(u"TXXX:A NEW KEY") }).size()), 1);
    QCOMPARE(id3v1Of(st.is), id3v1Of(st.was));
}

void TagWriteOnlyId3Test::aFieldTakenOut()
{
    // W-ONEHOME: the mood is in its own frame and in a user text frame; both are the field's.
    // (ID3v2.3 has only the second.)
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const Step st = step(path, field(u"Mood", { }));
    const QStringList mood({ s(u"TMOO"), s(u"TXXX:MOOD") });
    ONLY(st, mood);
    QCOMPARE(int(called(st.after, mood).size()), 0);
    QVERIFY(linuxbee::readTags(path).tags.fields.value(s(u"Mood")).isEmpty()); // RT
}

void TagWriteOnlyId3Test::aFieldTheFileHoldsTwice()
{
    // W-ONEHOME: the publisher is in TPUB and in a user text frame "Label". Naming the field
    // makes them one; aTitle shows that not naming it leaves both.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const QStringList label({ s(u"TPUB"), s(u"TXXX:Label"), s(u"TXXX:LABEL") });
    const Step st = step(path, field(u"Publisher", { s(u"One Label") }));
    ONLY(st, label);
    const QList<Frame> now = called(st.after, label);
    QCOMPARE(int(now.size()), 1);
    QCOMPARE(now.value(0).id, QByteArray("TPUB"));
    QCOMPARE(texts(now.value(0).body, 1), QStringList({ s(u"One Label") })); // BYTES
}

void TagWriteOnlyId3Test::onePersonOfAList()
{
    // W-ONLY: one frame lists many people. Naming the producer changes the producer's pair; the
    // engineer and the role no table knows are the pairs they were, in their order.
    QFETCH(QString, fixture);
    Box box;
    const QStringList lists({ s(u"TIPL"), s(u"TMCL"), s(u"IPLS") });
    const Step st = step(box.copy(fixture), other(u"PRODUCER", { s(u"New Producer") }));
    ONLY(st, lists);
    const QList<Pair> before = pairsOf(called(st.before, lists));
    const QList<Pair> after = pairsOf(called(st.after, lists));
    QVERIFY2(withRole(after, s(u"producer"), false) == withRole(before, s(u"producer"), false),
        qPrintable(shown(after)));
    QCOMPARE(shown(withRole(after, s(u"producer"), true)).toLower(), s(u"producer=new producer"));
    QCOMPARE(int(before.size()), 4);
}

void TagWriteOnlyId3Test::oneMusicianOfAList()
{
    QFETCH(QString, fixture);
    Box box;
    const QStringList lists({ s(u"TIPL"), s(u"TMCL"), s(u"IPLS") });
    const Step st = step(box.copy(fixture), other(u"PERFORMER:GUITAR", { s(u"New Guitarist") }));
    ONLY(st, lists);
    const QList<Pair> before = pairsOf(called(st.before, lists));
    const QList<Pair> after = pairsOf(called(st.after, lists));
    QVERIFY2(withRole(after, s(u"guitar"), false) == withRole(before, s(u"guitar"), false),
        qPrintable(shown(after)));
    QCOMPARE(shown(withRole(after, s(u"guitar"), true)).toLower(), s(u"guitar=new guitarist"));
    // Taken out again: the pair goes and the others stay. In ID3v2.4 the musicians' list held
    // that one pair, and a list with no pair is not kept.
    const Step out
        = step(box.dir.path() + QLatin1Char('/') + fixture, other(u"PERFORMER:GUITAR", { }));
    ONLY(out, lists);
    const QList<Pair> left = pairsOf(called(out.after, lists));
    QVERIFY2(left == withRole(before, s(u"guitar"), false), qPrintable(shown(left)));
    QCOMPARE(int(called(out.after, { s(u"TMCL") }).size()), 0);
}

void TagWriteOnlyId3Test::aPersonHeldInAUserTextFrame()
{
    // W-ONLY: "the frames of a key are the frames the tag library reads that key from". Some
    // taggers write the arranger as a user text frame; the tag library reads ARRANGER from it as
    // it does from a pair of the list. BYTES: such a frame is put into the fixture's padding
    // here: "TXXX", its size, no flags, then Latin-1 (0), the description, a zero, the value.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const QByteArray content("\0ARRANGER\0Old Arranger", 22);
    QByteArray frame("TXXX");
    frame.append(QByteArray("\0\0\0\x16\0\0", 6)).append(content);
    QByteArray d = bytesOf(path);
    qsizetype end = 10;
    for (const Frame &f : tagOf(d).frames)
        end += f.whole.size();
    QVERIFY(tagOf(d).end - end >= frame.size());
    d.replace(end, frame.size(), frame);
    const QStringList held({ s(u"TXXX:ARRANGER") });
    const QStringList named({ s(u"TXXX:ARRANGER"), s(u"TIPL"), s(u"IPLS") });
    for (const bool takenOut : { false, true }) {
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(out.write(d), d.size());
        out.close();
        QVERIFY(tagOf(bytesOf(path)).ok);
        QCOMPARE(linuxbee::readTags(path).tags.other.value(s(u"ARRANGER")),
            QStringList({ s(u"Old Arranger") })); // RT
        const QStringList to = takenOut ? QStringList() : QStringList({ s(u"New Arranger") });
        const Step st = step(path, other(u"ARRANGER", to));
        ONLY(st, named);
        QCOMPARE(int(called(st.before, held).size()), 1);
        QCOMPARE(int(called(st.after, held).size()),
            0); // BYTES: the old value is not left beside the new
        const QList<Pair> before = pairsOf(called(st.before, { s(u"TIPL"), s(u"IPLS") }));
        const QList<Pair> after = pairsOf(called(st.after, { s(u"TIPL"), s(u"IPLS") }));
        QVERIFY2(withRole(after, s(u"arranger"), false) == before, qPrintable(shown(after)));
        QCOMPARE(shown(withRole(after, s(u"arranger"), true)).toLower(),
            takenOut ? QString() : s(u"arranger=new arranger"));
    }
}

void TagWriteOnlyId3Test::thePictures()
{
    // W-PICTURES: the picture frames are the pictures'; the binary object beside them is not.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    TagChanges c;
    c.pictures = QList<Picture>({ cover(1) });
    Step st = step(path, c);
    ONLY(st, QStringList({ s(u"APIC") }));
    const QList<Frame> pictures = called(st.after, { s(u"APIC") });
    QCOMPARE(int(pictures.size()), 1);
    QVERIFY(pictures.value(0).body.endsWith(cover(1).data)); // BYTES
    QCOMPARE(soundOf(st.is), soundOf(st.was));
    c.pictures = QList<Picture>();
    st = step(path, c);
    ONLY(st, QStringList({ s(u"APIC") }));
    QCOMPARE(int(called(st.after, { s(u"APIC") }).size()), 0);
    QCOMPARE(st.after.frames.size(), st.before.frames.size() - 1);
}

void TagWriteOnlyId3Test::aSaveAndTheSaveThatTakesItBack()
{
    // W-ONLY: what Undo does. Five fields changed, none in its number of values, and then given
    // their old values again: every frame of the file is the bytes it was at the start.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const QStringList named({ s(u"TIT2"), s(u"COMM:"), s(u"USLT:"), s(u"TRCK"), s(u"TDRC"),
        s(u"TYER"), s(u"TDAT"), s(u"TIME") });
    TagChanges c;
    c.fields = {
        { s(u"Title"), { s(u"Another Title") } },
        { s(u"Comment"), { s(u"x"), s(u"y") } },
        { s(u"Lyrics"), { s(u"p"), s(u"q") } },
        { s(u"Track#"), { s(u"9") } },
        { s(u"Year"), { s(u"1990-01-02") } },
    };
    const Step there = step(path, c);
    ONLY(there, named);
    c.fields = {
        { s(u"Title"), { s(u"Old Title") } },
        { s(u"Comment"), { s(u"English comment"), s(u"Deutscher Kommentar") } },
        { s(u"Lyrics"), { s(u"Paroles francaises"), s(u"English lyrics line") } },
        { s(u"Track#"), { s(u"3") } },
        { s(u"Year"), { s(u"2001-05-12") } },
    };
    const Step back = step(path, c);
    ONLY(back, named);
    QList<QByteArray> first = notCalled(there.before, { });
    QList<QByteArray> last = notCalled(back.after, { });
    std::sort(first.begin(), first.end());
    std::sort(last.begin(), last.end());
    QVERIFY2(first == last,
        "after the second save the frames are not, one for one, the bytes they were at the start");
    QCOMPARE(soundOf(back.is), soundOf(there.was));
    QCOMPARE(id3v1Of(back.is), id3v1Of(there.was));
}

void TagWriteOnlyId3Test::theApeTagOfAnMp3IsLeftAsItIs()
{
    // W-ID3V1: "An APEv2 tag in an MP3 is left as it is", whatever is saved: a picture too.
    Box box;
    const QString path = box.copy(QStringLiteral("onlyape.mp3"));
    const QByteArray ape = apeOf(bytesOf(path));
    QCOMPARE(int(ape.size()), 422);
    Step st = step(path, field(u"Title", { s(u"New Title") }));
    ONLY(st, QStringList({ s(u"TIT2") }));
    QVERIFY2(apeOf(st.is) == ape, "a title");
    TagChanges c;
    c.pictures = QList<Picture>({ cover(2) });
    st = step(path, c);
    ONLY(st, QStringList({ s(u"APIC") }));
    QVERIFY2(apeOf(st.is) == ape,
        qPrintable(
            QStringLiteral("a picture: the APEv2 tag is %1 bytes").arg(apeOf(st.is).size())));
    QVERIFY(called(st.after, { s(u"APIC") }).value(0).body.endsWith(cover(2).data));
    c.pictures = QList<Picture>();
    st = step(path, c);
    ONLY(st, QStringList({ s(u"APIC") }));
    QVERIFY2(apeOf(st.is) == ape, "no pictures");
    st = step(path, other(u"A NEW KEY", { s(u"its value") }));
    ONLY(st, QStringList({ s(u"TXXX:A NEW KEY") }));
    QVERIFY2(apeOf(st.is) == ape, "another tag");
    QCOMPARE(soundOf(st.is),
        soundOf(bytesOf(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/onlyape.mp3"))));
    QCOMPARE(int(id3v1Of(st.is).size()), 128);
}

void TagWriteOnlyId3Test::aFileWithNoId3v2TagGetsItsValues()
{
    // W-ONLY, last sentence: v1only.mp3 has an ID3v1 tag and nothing else. A title is saved and
    // the ID3v1 tag is not wanted: the new ID3v2 tag holds the artist and the album too, or they
    // would be gone.
    Box box;
    const QString path = box.copy(QStringLiteral("v1only.mp3"));
    QVERIFY(!tagOf(bytesOf(path)).ok);
    WriteOptions noV1;
    noV1.writeId3v1 = false;
    const Step st = step(path, field(u"Title", { s(u"New Title") }), noV1);
    QCOMPARE(st.result.status, WriteResult::Status::Ok);
    QVERIFY(st.after.ok);
    QCOMPARE(st.after.version, 4);
    QCOMPARE(int(id3v1Of(st.is).size()), 0);
    QCOMPARE(int(called(st.after, { s(u"TIT2") }).size()), 1);
    QCOMPARE(int(called(st.after, { s(u"TPE1") }).size()), 1);
    QCOMPARE(int(called(st.after, { s(u"TALB") }).size()), 1);
    const TrackTags t = linuxbee::readTags(path).tags; // RT
    QCOMPARE(t.first(s(u"Title")), s(u"New Title"));
    QCOMPARE(t.first(s(u"Artist")), s(u"V1 Artist"));
    QCOMPARE(t.first(s(u"Album")), s(u"V1 Album"));
}

void TagWriteOnlyId3Test::aFileReadFromItsApeTagGetsItsValues_data()
{
    QTest::addColumn<bool>("withId3v1");
    QTest::addColumn<bool>("writeId3v1");
    QTest::newRow("an APEv2 tag alone, no ID3v1 wanted") << false << false;
    QTest::newRow("an APEv2 tag alone") << false << true;
    QTest::newRow("an APEv2 tag and an ID3v1 tag, no ID3v1 wanted") << true << false;
    QTest::newRow("an APEv2 tag and an ID3v1 tag") << true << true;
}

void TagWriteOnlyId3Test::aFileReadFromItsApeTagGetsItsValues()
{
    // W-ONLY, "a file read from another tag": an MP3 with an APEv2 tag and no ID3v2 tag is read
    // from the APEv2 tag. Once it has an ID3v2 tag it is read from that. So the ID3v2 tag a title
    // save makes holds the artist and the two gain values too, or they would be in the file and
    // no longer shown. BYTES: the file is onlyape.mp3 without its ID3v2 tag.
    QFETCH(bool, withId3v1);
    QFETCH(bool, writeId3v1);
    Box box;
    const QString path = box.copy(QStringLiteral("onlyape.mp3"));
    QByteArray d = bytesOf(path);
    d = d.mid(tagOf(d).end);
    if (!withId3v1)
        d.chop(128);
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(out.write(d), d.size());
    out.close();
    const TrackTags was = linuxbee::readTags(path).tags;
    QCOMPARE(was.first(s(u"Title")), s(u"Ape Title"));
    QCOMPARE(was.first(s(u"Artist")), s(u"Ape Artist"));
    QCOMPARE(was.other.value(s(u"REPLAYGAIN_TRACK_GAIN")), QStringList({ s(u"-7.50 dB") }));
    QCOMPARE(was.other.value(s(u"MP3GAIN_MINMAX")), QStringList({ s(u"089,210") }));

    WriteOptions options;
    options.writeId3v1 = writeId3v1;
    const Step st = step(path, field(u"Title", { s(u"New Title") }), options);
    QCOMPARE(st.result.status, WriteResult::Status::Ok);
    QVERIFY(st.after.ok);
    QCOMPARE(int(called(st.after, { s(u"TIT2") }).size()), 1); // BYTES
    QCOMPARE(int(called(st.after, { s(u"TPE1") }).size()), 1);
    QCOMPARE(int(called(st.after, { s(u"TXXX:REPLAYGAIN_TRACK_GAIN") }).size()), 1);
    QCOMPARE(int(called(st.after, { s(u"TXXX:MP3GAIN_MINMAX") }).size()), 1);
    QCOMPARE(apeOf(st.is), apeOf(st.was)); // W-ID3V1: and the APEv2 tag is left as it is
    QCOMPARE(int(apeOf(st.is).size()), 422);
    QCOMPARE(int(id3v1Of(st.is).size()), writeId3v1 ? 128 : 0);
    const TrackTags is = linuxbee::readTags(path).tags; // RT
    QCOMPARE(is.first(s(u"Title")), s(u"New Title"));
    QCOMPARE(is.first(s(u"Artist")), s(u"Ape Artist"));
    QCOMPARE(is.other, was.other);
    QCOMPARE(is.pictures.size(), was.pictures.size());
}

void TagWriteOnlyId3Test::afterAnotherVersionWasAskedFor()
{
    // W-ID3VER makes an ID3v2.4 tag an ID3v2.3 tag when asked, and that changes many frames. The
    // save after it may still put the pairs of the one list of people in another order (W-ONLY
    // says so); from then on the tag is under W-ONLY as any other.
    Box box;
    const QString path = box.copy(QStringLiteral("only24.mp3"));
    WriteOptions v23;
    v23.id3Version = WriteOptions::Id3Version::V23;
    QCOMPARE(step(path, field(u"Title", { s(u"A") }), v23).after.version, 3);
    const Step second = step(path, field(u"Title", { s(u"B") }));
    ONLY(second, QStringList({ s(u"TIT2"), s(u"IPLS") }));
    const QStringList people({ s(u"IPLS") });
    QList<Pair> was = pairsOf(called(second.before, people));
    QList<Pair> is = pairsOf(called(second.after, people));
    QCOMPARE(int(was.size()), 4);
    std::sort(was.begin(), was.end(), [](const Pair &a, const Pair &b) { return a.role < b.role; });
    std::sort(is.begin(), is.end(), [](const Pair &a, const Pair &b) { return a.role < b.role; });
    QVERIFY2(was == is, qPrintable(shown(is))); // no pair is lost by it
    const Step st = step(path, field(u"Title", { s(u"C") }));
    ONLY(st, QStringList({ s(u"TIT2") }));
    QCOMPARE(st.after.version, 3);
    QVERIFY(st.after.frames.size() > 40);
}

void TagWriteOnlyId3Test::aWavFileToo()
{
    // W-WAV: the same tag in a chunk of a WAV file. The first save gives it two comments, which
    // with no two frames to take the place of are one comment frame of two lines; the second
    // names the title, and the comment frame is as it was.
    Box box;
    const QString path = box.copy(QStringLiteral("basic.wav"));
    Step st = step(path, field(u"Comment", { s(u"a"), s(u"b") }));
    QCOMPARE(st.result.status, WriteResult::Status::Ok);
    QCOMPARE(textsOf(called(st.after, { s(u"COMM:") })), QStringList({ s(u"a\nb") }));
    st = step(path, field(u"Title", { s(u"New Title") }));
    ONLY(st, QStringList({ s(u"TIT2") }));
    QCOMPARE(textsOf(called(st.after, { s(u"COMM:") })), QStringList({ s(u"a\nb") }));
}

QTEST_MAIN(TagWriteOnlyId3Test)
#include "tst_tagwrite_only_id3.moc"
