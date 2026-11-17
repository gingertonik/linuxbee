// LOCKED ACCEPTANCE TEST (tag writing: a save keeps the file's own ID3v2 version). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-writing.md (W-ID3VER).
//   BYTES  the test looks at the file's raw bytes, as the ID3 documents lay them out
//   RT     a round trip through readTags, always beside a BYTES check
#include <QTemporaryDir>
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

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

// BYTES: an ID3v2 tag begins "ID3", then the major version as one byte.
int majorAt(const QByteArray &bytes, qsizetype at)
{
    return bytes.mid(at, 3) == "ID3" && bytes.size() > at + 3 ? int(static_cast<unsigned char>(bytes.at(at + 3))) : -1;
}

int mp3Major(const QString &path)
{
    return majorAt(bytesOf(path), 0);
}

// BYTES: in a WAV file the tag is the content of a chunk named "id3 " or "ID3 ", after its four size bytes.
int wavMajor(const QString &path)
{
    const QByteArray bytes = bytesOf(path);
    qsizetype at = bytes.indexOf("id3 ");
    if (at < 0)
        at = bytes.indexOf("ID3 ");
    return at < 0 ? -1 : majorAt(bytes, at + 8);
}

TagChanges album(const char16_t *value)
{
    TagChanges c;
    c.fields = { { s(u"Album"), { s(value) } } };
    return c;
}

// What ID3v2.3 has no frame for (or only a shorter one), put into a file as ID3v2.4.
TagChanges onlyInV24()
{
    TagChanges c;
    c.fields = {
        { s(u"Mood"), { s(u"Wistful") } },
        { s(u"Original Year"), { s(u"1998-11-02") } },
        { s(u"Year"), { s(u"2019-03") } },
    };
    c.other = {
        { s(u"DISCSUBTITLE"), { s(u"Set Subtitle") } },
        { s(u"RELEASEDATE"), { s(u"2019-03-08") } },
    };
    return c;
}

struct Box {
    QTemporaryDir dir;
    QString copy(const char *fixture, const char *as = nullptr)
    {
        const QString to = dir.path() + QLatin1Char('/') + QLatin1String(as ? as : fixture);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(fixture), to))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return to;
    }
};

WriteOptions as(WriteOptions::Id3Version version)
{
    WriteOptions o;
    o.id3Version = version;
    return o;
}

}

class TagWriterId3KeepTest : public QObject {
    Q_OBJECT
private slots:
    void theDefaultIsKeep();
    void aV24FileStaysV24AndLosesNothing();
    void aV23FileStaysV23();
    void aFileWithNoId3v2TagGetsV24();
    void aV22FileBecomesV24();
    void theSettingStillConverts();
    void wavToo();
    void whatId3v23CannotHoldIsNamed();
    void anOrdinarySaveNamesNothing();
};

void TagWriterId3KeepTest::theDefaultIsKeep()
{
    QVERIFY(WriteOptions().id3Version == WriteOptions::Id3Version::Keep);
}

void TagWriterId3KeepTest::aV24FileStaysV24AndLosesNothing()
{
    // W-ID3VER: the owner's case. A file tagged ID3v2.4 with a mood and full dates; one unrelated edit, default options.
    Box box;
    const QString path = box.copy("basic.mp3"); // GEN: ID3v2.4
    QCOMPARE(mp3Major(path), 4);
    QCOMPARE(linuxbee::writeTags(path, onlyInV24(), as(WriteOptions::Id3Version::V24)).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 4);
    QVERIFY(bytesOf(path).contains("TMOO") && bytesOf(path).contains("TDOR") && bytesOf(path).contains("TSST")); // BYTES

    QCOMPARE(linuxbee::writeTags(path, album(u"Another Album")).status, WriteResult::Status::Ok);
    const QByteArray b = bytesOf(path);
    QCOMPARE(mp3Major(path), 4); // BYTES
    for (const char *frame : { "TMOO", "TDOR", "TSST", "TDRL", "TDRC" })
        QVERIFY2(b.contains(frame), frame); // BYTES
    for (const char *frame : { "TYER", "TDAT", "TORY" })
        QVERIFY2(!b.contains(frame), frame); // BYTES: no ID3v2.3 date frame appeared
    QVERIFY(b.contains("1998-11-02")); // BYTES: the original date, whole
    const TrackTags t = linuxbee::readTags(path).tags; // RT
    QCOMPARE(t.first(s(u"Album")), s(u"Another Album"));
    QCOMPARE(t.first(s(u"Mood")), s(u"Wistful"));
    QCOMPARE(t.first(s(u"Original Year")), s(u"1998-11-02"));
    QCOMPARE(t.first(s(u"Year")), s(u"2019-03"));
    QCOMPARE(t.other.value(s(u"DISCSUBTITLE")), QStringList({ s(u"Set Subtitle") }));
    QCOMPARE(t.other.value(s(u"RELEASEDATE")), QStringList({ s(u"2019-03-08") }));
    // A second save, and one that changes nothing, keep it too.
    QCOMPARE(linuxbee::writeTags(path, album(u"A Third")).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 4);
    QCOMPARE(linuxbee::writeTags(path, album(u"A Third")).status, WriteResult::Status::Unchanged);
    QCOMPARE(mp3Major(path), 4);
}

void TagWriterId3KeepTest::aV23FileStaysV23()
{
    Box box;
    const QString path = box.copy("v23.mp3"); // GEN: ID3v2.3
    QCOMPARE(mp3Major(path), 3);
    QCOMPARE(linuxbee::writeTags(path, album(u"Another Album")).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 3); // BYTES
    QVERIFY(!bytesOf(path).contains("TDRC")); // BYTES: no ID3v2.4 date frame appeared
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Album")), s(u"Another Album"));
    QCOMPARE(linuxbee::readTags(path).tags.tagTypes, QStringList({ s(u"ID3v2.3"), s(u"ID3v1") }));
}

void TagWriterId3KeepTest::aFileWithNoId3v2TagGetsV24()
{
    // W-ID3VER: a new tag is ID3v2.4, in which nothing is lost.
    Box box;
    const QString path = box.copy("v1only.mp3"); // GEN: an ID3v1 tag only
    QCOMPARE(mp3Major(path), -1);
    QCOMPARE(linuxbee::writeTags(path, onlyInV24()).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 4); // BYTES
    QVERIFY(bytesOf(path).contains("TMOO"));
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Original Year")), s(u"1998-11-02"));
    // And it stays so.
    QCOMPARE(linuxbee::writeTags(path, album(u"Another Album")).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 4);
}

void TagWriterId3KeepTest::aV22FileBecomesV24()
{
    // W-ID3VER: ID3v2.2 cannot be written. BYTES: a tag made here by hand: "ID3", 2, 0, flags, a four-byte size,
    // then one frame: "TT2", a three-byte size, the encoding byte (0, Latin-1) and the text.
    Box box;
    const QString source = box.copy("v1only.mp3");
    const QByteArray text("Old Two");
    QByteArray frame("TT2");
    frame.append('\0').append('\0').append(char(text.size() + 1)).append('\0').append(text);
    QByteArray tag("ID3");
    tag.append('\x02').append('\0').append('\0').append('\0').append('\0').append('\0').append(char(frame.size())).append(frame);
    const QString path = box.dir.path() + QStringLiteral("/v22.mp3");
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(tag + bytesOf(source));
    out.close();
    QCOMPARE(mp3Major(path), 2);
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Title")), s(u"Old Two")); // the hand-made tag is a tag

    QCOMPARE(linuxbee::writeTags(path, album(u"Another Album")).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 4); // BYTES
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Title")), s(u"Old Two"));
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Album")), s(u"Another Album"));
}

void TagWriterId3KeepTest::theSettingStillConverts()
{
    // W-ID3VER: V23 and V24 mean what they meant: the tag is written in that version, whatever the file had.
    Box box;
    QString path = box.copy("basic.mp3");
    QCOMPARE(linuxbee::writeTags(path, album(u"Another Album"), as(WriteOptions::Id3Version::V23)).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 3);
    // Now it is an ID3v2.3 file, and the default keeps that.
    QCOMPARE(linuxbee::writeTags(path, album(u"A Third")).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 3);
    path = box.copy("v23.mp3");
    QCOMPARE(linuxbee::writeTags(path, album(u"Another Album"), as(WriteOptions::Id3Version::V24)).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 4);
    QCOMPARE(linuxbee::writeTags(path, album(u"A Third")).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 4);
    path = box.copy("v1only.mp3");
    QCOMPARE(linuxbee::writeTags(path, album(u"Another Album"), as(WriteOptions::Id3Version::V23)).status, WriteResult::Status::Ok);
    QCOMPARE(mp3Major(path), 3);
}

void TagWriterId3KeepTest::wavToo()
{
    Box box;
    QString path = box.copy("basic.wav"); // GEN: an ID3v2.4 chunk
    QCOMPARE(wavMajor(path), 4);
    QCOMPARE(linuxbee::writeTags(path, album(u"Another Album")).status, WriteResult::Status::Ok);
    QCOMPARE(wavMajor(path), 4); // BYTES
    QCOMPARE(linuxbee::writeTags(path, album(u"A Third"), as(WriteOptions::Id3Version::V23)).status, WriteResult::Status::Ok);
    QCOMPARE(wavMajor(path), 3);
    QCOMPARE(linuxbee::writeTags(path, album(u"A Fourth")).status, WriteResult::Status::Ok);
    QCOMPARE(wavMajor(path), 3); // kept
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Album")), s(u"A Fourth"));
}

void TagWriterId3KeepTest::whatId3v23CannotHoldIsNamed()
{
    // W-ID3VER, "not kept": a save that writes ID3v2.3 says which values have no place there.
    Box box;
    const QString path = box.copy("basic.mp3");
    QCOMPARE(linuxbee::writeTags(path, onlyInV24(), as(WriteOptions::Id3Version::V24)).notKept, QStringList());
    const WriteResult forced = linuxbee::writeTags(path, album(u"Another Album"), as(WriteOptions::Id3Version::V23));
    QCOMPARE(forced.status, WriteResult::Status::Ok); // it is saved all the same
    QCOMPARE(forced.notKept, QStringList({
                                 s(u"Disc Subtitle"),
                                 s(u"Mood"),
                                 s(u"Original Year: 1998-11-02 became 1998"),
                                 s(u"Release Date"),
                                 s(u"Year: 2019-03 became 2019"),
                             }));
    QCOMPARE(mp3Major(path), 3);
    QVERIFY(!bytesOf(path).contains("TMOO")); // BYTES: and they are indeed gone
    // The file is ID3v2.3 now. A value asked for that it cannot hold is named, under the default too.
    TagChanges c;
    c.fields = { { s(u"Mood"), { s(u"Bright"), s(u"Early") } }, { s(u"Title"), { s(u"Kept Title") } } };
    const WriteResult asked = linuxbee::writeTags(path, c);
    QCOMPARE(asked.status, WriteResult::Status::Ok);
    QCOMPARE(asked.notKept, QStringList({ s(u"Mood") }));
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Title")), s(u"Kept Title"));
    c.fields = { { s(u"Original Year"), { s(u"1997-06-05") } } };
    QCOMPARE(linuxbee::writeTags(path, c).notKept, QStringList({ s(u"Original Year: 1997-06-05 became 1997") }));
}

void TagWriterId3KeepTest::anOrdinarySaveNamesNothing()
{
    // W-ID3VER, "not kept": nothing is named when nothing is lost, in either version and in other formats.
    Box box;
    TagChanges c;
    c.fields = {
        { s(u"Title"), { s(u"New Title") } },
        { s(u"Artist"), { s(u"A One"), s(u"B Two") } },
        { s(u"Genre"), { s(u"Rock"), s(u"Folk") } },
        { s(u"Track#"), { s(u"3") } },
        { s(u"Track Count"), { s(u"9") } },
        { s(u"Year"), { s(u"1999-12-31") } },
        { s(u"Original Year"), { s(u"1998") } },
        { s(u"Rating"), { s(u"3.5") } },
        { s(u"Lyrics"), { s(u"x\ny") } },
        { s(u"Sort Artist"), { s(u"One, A") } },
        { s(u"Comment"), { s(u"a comment") } },
    };
    c.other = { { s(u"MY KEY"), { s(u"my value"), s(u"a second") } } };
    for (const char *fixture : { "v23.mp3", "basic.mp3", "v1only.mp3", "basic.flac", "basic.wav", "basic.m4a" }) {
        const QString path = box.copy(fixture);
        const WriteResult r = linuxbee::writeTags(path, c);
        QVERIFY2(r.status == WriteResult::Status::Ok, fixture);
        QVERIFY2(r.notKept.isEmpty(), qPrintable(QLatin1String(fixture) + QStringLiteral(": ") + r.notKept.join(QStringLiteral(" | "))));
    }
    // Forced to ID3v2.3 with nothing that ID3v2.3 lacks: nothing named.
    const QString path = box.copy("basic.mp3", "forced.mp3");
    const WriteResult r = linuxbee::writeTags(path, c, as(WriteOptions::Id3Version::V23));
    QVERIFY2(r.notKept.isEmpty(), qPrintable(r.notKept.join(QStringLiteral(" | "))));
    QCOMPARE(mp3Major(path), 3);
}

QTEST_MAIN(TagWriterId3KeepTest)
#include "tst_tagwriter_id3keep.moc"
