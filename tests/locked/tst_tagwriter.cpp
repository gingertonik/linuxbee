// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// How these tests avoid grading the writer with the program's own reader:
//   BYTES  the test looks for the frame or comment in the file's raw bytes,
//          as the ID3, Vorbis comment and APE documents lay it out
//   GEN    the value was put in the fixture by tests/data/tags/make_fixtures.py
//   RT     a round trip through readTags. It shows the writer and the reader
//          agree; on its own it does not show either is right. Every RT check
//          sits beside a BYTES check here or an ffprobe check in
//          tests/locked/cli_tagwrite.sh.
//   W-x    a rule in docs/spec/tag-writing.md
#include <QtTest>

#include "tags/tagwriter.h"

using linuxbee::ReadResult;
using linuxbee::TagChanges;
using linuxbee::TrackTags;
using linuxbee::WriteOptions;
using linuxbee::WriteResult;
using Fields = QMap<QString, QStringList>;

Q_DECLARE_METATYPE(linuxbee::TagChanges)

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

QByteArray png()
{
    return QByteArray::fromHex(
        "89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de"
        "0000000c49444154789c63f8cfc0000003010100c9fe92ef0000000049454e44ae426082");
}

const char *const kAll[] = { "basic.mp3", "v23.mp3", "v1only.mp3", "basic.flac", "empty.flac",
    "basic.ogg", "basic.opus", "basic.m4a", "basic.wav", "basic.wv" };

// The changes most tests apply.
TagChanges usualChanges()
{
    TagChanges c;
    c.fields = {
        { s(u"Title"), { s(u"New Title") } },
        { s(u"Artist"), { s(u"A One"), s(u"B Two") } },
        { s(u"Track#"), { s(u"3") } },
        { s(u"Track Count"), { s(u"9") } },
        { s(u"Disc#"), { s(u"2") } },
        { s(u"Disc Count"), { s(u"4") } },
        { s(u"Year"), { s(u"1999-12-31") } },
        { s(u"Rating"), { s(u"3.5") } },
        { s(u"Grouping"), { s(u"New Group") } },
        { s(u"Publisher"), { s(u"New Label") } },
        { s(u"Lyrics"), { s(u"x\ny") } },
        { s(u"Album"), { } },
    };
    c.other = { { s(u"MY KEY"), { s(u"my value") } } };
    return c;
}

}

class TagWriterTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void roundTrip_data();
    void roundTrip();
    void id3v23Bytes();
    void id3v24Bytes();
    void id3v1();
    void popmBytes();
    void vorbisBytes();
    void apeBytes();
    void wavKeepsOneTag();
    void unchanged();
    void unicode_data();
    void unicode();
    void removeFields();
    void numbers();
    void pictures_data();
    void pictures();
    void invalidChanges_data();
    void invalidChanges();
    void invalidWritesNothing();
    void statuses();
    void modificationTime();
    void popmFromRating_data();
    void popmFromRating();

private:
    QString copy(const char *fixture);
    QTemporaryDir m_dir;
    int m_counter = 0;
};

void TagWriterTest::init()
{
    QVERIFY(m_dir.isValid());
}

// A fresh, writable copy of a fixture. The fixtures themselves are never opened for writing.
QString TagWriterTest::copy(const char *fixture)
{
    const QString source = QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(fixture);
    const QString target
        = m_dir.filePath(QStringLiteral("%1-%2").arg(++m_counter).arg(QLatin1String(fixture)));
    if (!QFile::copy(source, target))
        return { };
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    return target;
}

void TagWriterTest::roundTrip_data()
{
    QTest::addColumn<QString>("fixture");
    for (const char *name : kAll)
        QTest::newRow(name) << QString::fromLatin1(name);
}

void TagWriterTest::roundTrip()
{
    QFETCH(QString, fixture);
    const QString path = copy(qPrintable(fixture));
    const TrackTags before = linuxbee::readTags(path).tags;

    const WriteResult result = linuxbee::writeTags(path, usualChanges());
    QCOMPARE(result.status, WriteResult::Status::Ok);
    QVERIFY(result.ok());
    QVERIFY(result.problems.isEmpty());

    const ReadResult read = linuxbee::readTags(path);
    QCOMPARE(read.status, ReadResult::Status::Ok);
    const TrackTags &after = read.tags;

    // RT, W-FIELDS: the named fields have the new values, the rest are untouched.
    Fields expected = before.fields;
    const TagChanges changes = usualChanges();
    for (auto it = changes.fields.constBegin(); it != changes.fields.constEnd(); ++it) {
        if (it.value().isEmpty())
            expected.remove(it.key());
        else
            expected.insert(it.key(), it.value());
    }
    QCOMPARE(after.fields, expected);
    Fields expectedOther = before.other;
    expectedOther.insert(s(u"MY KEY"), { s(u"my value") });
    QCOMPARE(after.other, expectedOther);
    QCOMPARE(after.unsupported, QStringList());

    // W-KEEP: pictures and audio are not part of the change.
    QCOMPARE(after.pictures.size(), before.pictures.size());
    if (!after.pictures.isEmpty())
        QCOMPARE(after.pictures.first().data, png()); // GEN
    QCOMPARE(after.format, before.format);
    QCOMPARE(after.audio.durationMs, before.audio.durationMs);
    QCOMPARE(after.audio.sampleRateHz, before.audio.sampleRateHz);
    QCOMPARE(after.audio.channels, before.audio.channels);
    QCOMPARE(after.audio.bitsPerSample, before.audio.bitsPerSample);

    // W-UNCHANGED: the same changes again write nothing.
    const QByteArray saved = bytesOf(path);
    QCOMPARE(linuxbee::writeTags(path, usualChanges()).status, WriteResult::Status::Unchanged);
    QCOMPARE(bytesOf(path), saved);
}

void TagWriterTest::id3v23Bytes()
{
    const QString path = copy("basic.mp3"); // GEN: an ID3v2.4 tag
    WriteOptions asV23; // W-ID3VER: asked for; the default keeps the file's version
    asV23.id3Version = WriteOptions::Id3Version::V23;
    QCOMPARE(linuxbee::writeTags(path, usualChanges(), asV23).status, WriteResult::Status::Ok);
    const QByteArray b = bytesOf(path);
    // BYTES, W-ID3VER: "ID3", major version 3, revision 0.
    QVERIFY(b.startsWith(QByteArray("ID3\x03\x00", 5)));
    // BYTES: ID3v2.3 has no TDRC. The date is TYER plus TDAT.
    QVERIFY(b.contains("TYER"));
    QVERIFY(b.contains("TDAT"));
    QVERIFY(!b.contains("TDRC"));
    // BYTES, W-TABLE: Grouping is TIT1, Publisher is TPUB, Lyrics is USLT.
    QVERIFY(b.contains("TIT1"));
    QVERIFY(b.contains("TPUB"));
    QVERIFY(b.contains("USLT"));
    QVERIFY(!b.contains("TALB")); // Album was removed
    // RT: the reader sees the version too.
    QCOMPARE(linuxbee::readTags(path).tags.tagTypes, QStringList({ s(u"ID3v2.3"), s(u"ID3v1") }));
}

void TagWriterTest::id3v24Bytes()
{
    const QString path = copy("v23.mp3"); // GEN: an ID3v2.3 tag
    WriteOptions options;
    options.id3Version = WriteOptions::Id3Version::V24;
    QCOMPARE(linuxbee::writeTags(path, usualChanges(), options).status, WriteResult::Status::Ok);
    const QByteArray b = bytesOf(path);
    QVERIFY(b.startsWith(QByteArray("ID3\x04\x00", 5))); // BYTES
    QVERIFY(b.contains("TDRC"));
    QVERIFY(!b.contains("TYER"));
    QVERIFY(!b.contains("TDAT"));
    QVERIFY(b.contains("1999-12-31")); // BYTES: ASCII survives in any ID3v2.4 encoding but UTF-16
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Year")), s(u"1999-12-31"));
}

void TagWriterTest::id3v1()
{
    // W-ID3V1 on: the last 128 bytes are an ID3v1 tag holding the new title.
    QString path = copy("v1only.mp3");
    QCOMPARE(linuxbee::writeTags(path, usualChanges()).status, WriteResult::Status::Ok);
    QByteArray b = bytesOf(path);
    // BYTES: "TAG", then the title in 30 bytes padded with zeros.
    QCOMPARE(b.right(128).left(33), QByteArray("TAGNew Title").leftJustified(33, '\0'));
    QVERIFY(b.startsWith("ID3")); // W-ID3V1: an ID3v2 tag was created in front
    QCOMPARE(linuxbee::readTags(path).tags.tagTypes, QStringList({ s(u"ID3v2.4"), s(u"ID3v1") })); // W-ID3VER: a new tag

    // A file that had none gets one.
    path = copy("basic.mp3");
    QVERIFY(!bytesOf(path).right(128).startsWith("TAG")); // GEN
    QCOMPARE(linuxbee::writeTags(path, usualChanges()).status, WriteResult::Status::Ok);
    QVERIFY(bytesOf(path).right(128).startsWith("TAGNew Title"));

    // W-ID3V1 off: an existing ID3v1 tag is removed.
    path = copy("v23.mp3");
    QVERIFY(bytesOf(path).right(128).startsWith("TAG")); // GEN
    WriteOptions options;
    options.writeId3v1 = false;
    QCOMPARE(linuxbee::writeTags(path, usualChanges(), options).status, WriteResult::Status::Ok);
    QVERIFY(!bytesOf(path).right(128).startsWith("TAG"));
    QCOMPARE(linuxbee::readTags(path).tags.tagTypes, QStringList({ s(u"ID3v2.3") }));
}

void TagWriterTest::popmBytes()
{
    // GEN: basic.mp3 has POPM with this address, rating 196 and counter 7.
    QString path = copy("basic.mp3");
    TagChanges c;
    c.fields = { { s(u"Rating"), { s(u"3.5") } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    // BYTES (ID3, POPM): address, zero byte, rating byte, counter. W-RATING:
    // 3.5 stars is 186 (0xBA); the address and the counter are kept.
    QVERIFY(bytesOf(path).contains(QByteArray("someone@example.invalid\0\xBA\0\0\0\x07", 29)));

    // W-RATING: a file with no POPM gets one with an empty address.
    path = copy("v1only.mp3");
    c.fields = { { s(u"Rating"), { s(u"5") } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    // BYTES: frame id, size 6 (one zero byte, the rating, a 4-byte counter), flags.
    QVERIFY(bytesOf(path).contains(QByteArray("POPM\0\0\0\x06\0\0\0\xFF\0\0\0\0", 16)));
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Rating")), s(u"5"));

    // Removing the rating removes the frame.
    c.fields = { { s(u"Rating"), { } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    QVERIFY(!bytesOf(path).contains("POPM"));
}

void TagWriterTest::vorbisBytes()
{
    for (const char *name : { "basic.flac", "basic.ogg", "basic.opus" }) {
        const QString path = copy(name);
        QVERIFY2(linuxbee::writeTags(path, usualChanges()).status == WriteResult::Status::Ok, name);
        const QByteArray b = bytesOf(path);
        // BYTES: a Vorbis comment is stored as NAME=value in UTF-8. W-TABLE names.
        QVERIFY2(b.contains("TITLE=New Title"), name);
        QVERIFY2(b.contains("ARTIST=A One"), name);
        QVERIFY2(b.contains("ARTIST=B Two"), name); // W-MULTI: one comment per value
        QVERIFY2(b.contains("TRACKNUMBER=3"), name);
        QVERIFY2(b.contains("TRACKTOTAL=9"), name);
        QVERIFY2(b.contains("TOTALDISCS=4"), name); // W-TG
        QVERIFY2(b.contains("CONTENTGROUP=New Group"), name);
        QVERIFY2(b.contains("ORGANIZATION=New Label"), name);
        QVERIFY2(b.contains("UNSYNCEDLYRICS=x\ny"), name);
        QVERIFY2(b.contains("DATE=1999-12-31"), name);
        QVERIFY2(b.contains("RATING=70"), name); // W-RATING: stars x 20
        QVERIFY2(b.contains("MY KEY=my value"), name);
        QVERIFY2(!b.contains("ALBUM="), name);
        // W-ONEHOME: the other spellings of a written field are gone.
        QVERIFY2(!b.contains("TOTALTRACKS="), name); // GEN: basic.ogg had it
        QVERIFY2(!b.contains("DISCTOTAL=") || b.contains("DISCTOTAL=4"), name); // W-DISCTOTAL: kept, the same
    }
}

void TagWriterTest::apeBytes()
{
    const QString path = copy("basic.wv");
    QCOMPARE(linuxbee::writeTags(path, usualChanges()).status, WriteResult::Status::Ok);
    const QByteArray b = bytesOf(path).toUpper();
    // BYTES: an APE item is its key, a zero byte, then the value. W-NUMBERS:
    // number and count share one item.
    QVERIFY(b.contains(QByteArray("TRACK\0" "3/9", 9)));
    QVERIFY(b.contains(QByteArray("DISC\0" "2/4", 8)));
    QVERIFY(b.contains(QByteArray("TITLE\0NEW TITLE", 15)));
}

void TagWriterTest::wavKeepsOneTag()
{
    // W-WAV: no RIFF Info chunk is created beside the ID3 chunk.
    const QString path = copy("basic.wav");
    QCOMPARE(linuxbee::writeTags(path, usualChanges()).status, WriteResult::Status::Ok);
    QCOMPARE(linuxbee::readTags(path).tags.tagTypes, QStringList({ s(u"ID3v2.4") })); // W-ID3VER: GEN's chunk is ID3v2.4, kept
    QVERIFY(!bytesOf(path).contains("LIST")); // BYTES: an Info chunk lives in a LIST chunk
}

void TagWriterTest::unchanged()
{
    for (const char *name : kAll) {
        const QString path = copy(name);
        const QByteArray original = bytesOf(path);
        const TrackTags now = linuxbee::readTags(path).tags;

        // W-UNCHANGED: no changes at all.
        QVERIFY2(linuxbee::writeTags(path, { }).status == WriteResult::Status::Unchanged, name);
        // W-UNCHANGED: everything set to what it already is, pictures included.
        TagChanges same;
        same.fields = now.fields;
        same.other = now.other;
        same.pictures = now.pictures;
        same.fields.insert(s(u"Conductor"), { }); // removing what is not there
        QVERIFY2(linuxbee::writeTags(path, same).status == WriteResult::Status::Unchanged, name);
        QVERIFY2(bytesOf(path) == original, name);
    }
    // W-UNCHANGED comes before W-STATUS: a read-only file that needs no change is fine.
    const QString path = copy("basic.flac");
    QFile::setPermissions(path, QFile::ReadOwner);
    TagChanges c;
    c.fields = { { s(u"Title"), { s(u"Flac Title") } } }; // GEN
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Unchanged);
}

void TagWriterTest::unicode_data()
{
    QTest::addColumn<QString>("fixture");
    QTest::addColumn<bool>("v24");
    for (const char *name : kAll) {
        QTest::addRow("%s", name) << QString::fromLatin1(name) << false;
        if (QByteArray(name).endsWith(".mp3") || QByteArray(name).endsWith(".wav"))
            QTest::addRow("%s-v24", name) << QString::fromLatin1(name) << true;
    }
}

void TagWriterTest::unicode()
{
    QFETCH(QString, fixture);
    QFETCH(bool, v24);
    const QString path = copy(qPrintable(fixture));
    const QString title = s(u"\u65e5\u672c\u8a9e \u00e9 \U0001F600");
    TagChanges c;
    c.fields = { { s(u"Title"), { title } }, { s(u"Comment"), { s(u"na\u00efve") } } };
    WriteOptions options;
    options.id3Version = v24 ? WriteOptions::Id3Version::V24 : WriteOptions::Id3Version::V23;
    options.writeId3v1 = false; // ID3v1 cannot hold this text
    QCOMPARE(linuxbee::writeTags(path, c, options).status, WriteResult::Status::Ok);
    const TrackTags t = linuxbee::readTags(path).tags;
    QCOMPARE(t.first(s(u"Title")), title); // RT
    QCOMPARE(t.first(s(u"Comment")), s(u"na\u00efve"));
    // BYTES: the title is in the file as UTF-8 or as UTF-16 (ID3v2.3).
    const QByteArray b = bytesOf(path);
    const QByteArray utf16(reinterpret_cast<const char *>(title.utf16()), title.size() * 2);
    QVERIFY(b.contains(title.toUtf8()) || b.contains(utf16));
}

void TagWriterTest::removeFields()
{
    const QString path = copy("basic.flac");
    TagChanges c;
    // W-FIELDS: an empty list removes; so does a list of blank values. Names
    // ignore case.
    c.fields = { { s(u"genre"), { } }, { s(u"Publisher"), { s(u"  "), QString() } } };
    c.other = { { s(u"replaygain_track_gain"), { } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    const TrackTags t = linuxbee::readTags(path).tags;
    QVERIFY(!t.fields.contains(s(u"Genre")));
    QVERIFY(!t.fields.contains(s(u"Publisher")));
    QVERIFY(t.other.isEmpty());
    QCOMPARE(t.first(s(u"Title")), s(u"Flac Title")); // GEN: untouched
    const QByteArray b = bytesOf(path);
    QVERIFY(!b.contains("GENRE=")); // BYTES
    QVERIFY(!b.contains("ORGANIZATION="));
    QVERIFY(!b.contains("REPLAYGAIN_TRACK_GAIN="));
}

void TagWriterTest::numbers()
{
    // W-NUMBERS: changing only the count keeps the number, and the other way round.
    // GEN: basic.mp3 has TRCK "11/12" and TPOS "1/2".
    QString path = copy("basic.mp3");
    TagChanges c;
    c.fields = { { s(u"Track Count"), { s(u"20") } }, { s(u"Disc#"), { s(u"2") } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    TrackTags t = linuxbee::readTags(path).tags;
    QCOMPARE(t.first(s(u"Track#")), s(u"11"));
    QCOMPARE(t.first(s(u"Track Count")), s(u"20"));
    QCOMPARE(t.first(s(u"Disc#")), s(u"2"));
    QCOMPARE(t.first(s(u"Disc Count")), s(u"2"));

    // Removing the count leaves a bare number. BYTES in a Vorbis comment.
    path = copy("basic.flac"); // GEN: TRACKNUMBER 5, TRACKTOTAL 10
    c.fields = { { s(u"Track Count"), { } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    QByteArray b = bytesOf(path);
    QVERIFY(b.contains("TRACKNUMBER=5"));
    QVERIFY(!b.contains("TRACKTOTAL="));

    // GEN: basic.opus stores TRACKNUMBER "2/8". Writing either half splits it. W-NUMBERS.
    path = copy("basic.opus");
    c.fields = { { s(u"Track#"), { s(u"7") } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    b = bytesOf(path);
    QVERIFY(b.contains("TRACKNUMBER=7"));
    QVERIFY(!b.contains("TRACKNUMBER=7/"));
    QVERIFY(b.contains("TRACKTOTAL=8"));

    // MPEG-4, RT only: number and count travel together.
    path = copy("basic.m4a"); // GEN: trkn (3, 9)
    c.fields = { { s(u"Track#"), { s(u"4") } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    t = linuxbee::readTags(path).tags;
    QCOMPARE(t.first(s(u"Track#")), s(u"4"));
    QCOMPARE(t.first(s(u"Track Count")), s(u"9"));
}

void TagWriterTest::pictures_data()
{
    QTest::addColumn<QString>("fixture");
    for (const char *name : { "basic.mp3", "basic.flac", "basic.ogg", "basic.m4a", "empty.flac",
             "basic.opus", "v1only.mp3", "basic.wav", "basic.wv" })
        QTest::newRow(name) << QString::fromLatin1(name);
}

void TagWriterTest::pictures()
{
    QFETCH(QString, fixture);
    const QString path = copy(qPrintable(fixture));
    const TrackTags before = linuxbee::readTags(path).tags;

    // W-PICTURES: the list replaces whatever was there. Type and MIME type are
    // filled in when left empty (R-MIME in docs/spec/tag-reading.md).
    linuxbee::Picture jpeg;
    jpeg.data = QByteArray::fromHex("ffd8ffe000104a46494600010100000100010000ffd9") + "linuxbee-test";
    linuxbee::Picture back;
    back.data = png();
    back.type = s(u"Back Cover");
    back.mimeType = s(u"image/png");
    back.description = s(u"the back");
    TagChanges c;
    c.pictures = QList<linuxbee::Picture> { jpeg, back };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);

    TrackTags t = linuxbee::readTags(path).tags;
    QCOMPARE(t.pictures.size(), 2);
    QCOMPARE(t.pictures.at(0).data, jpeg.data);
    QCOMPARE(t.pictures.at(0).mimeType, s(u"image/jpeg"));
    QCOMPARE(t.pictures.at(0).type, s(u"Front Cover"));
    QCOMPARE(t.pictures.at(1).data, png());
    QCOMPARE(t.pictures.at(1).mimeType, s(u"image/png"));
    if (fixture != s(u"basic.m4a")) { // MPEG-4 stores neither a type nor a description
        QCOMPARE(t.pictures.at(1).type, s(u"Back Cover"));
        QCOMPARE(t.pictures.at(1).description, s(u"the back"));
    }
    QCOMPARE(t.fields, before.fields); // W-KEEP
    // BYTES: the picture is in the file. FLAC, ID3 and MPEG-4 store it raw;
    // Ogg stores it base64-encoded, so those two are RT only.
    if (fixture != s(u"basic.ogg") && fixture != s(u"basic.opus"))
        QVERIFY(bytesOf(path).contains(jpeg.data));

    // The same list again changes nothing.
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Unchanged);

    // An empty list removes every picture.
    c.pictures = QList<linuxbee::Picture> { };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    t = linuxbee::readTags(path).tags;
    QVERIFY(t.pictures.isEmpty());
    QVERIFY(!bytesOf(path).contains("linuxbee-test"));
    QCOMPARE(t.fields, before.fields);
}

void TagWriterTest::invalidChanges_data()
{
    QTest::addColumn<TagChanges>("changes");
    QTest::addColumn<QString>("startsWith");
    auto field = [](const QString &name, const QStringList &values) {
        TagChanges c;
        c.fields = { { name, values } };
        return c;
    };
    auto other = [](const QString &key) {
        TagChanges c;
        c.other = { { key, { QStringLiteral("x") } } };
        return c;
    };
    // W-INVALID
    QTest::newRow("unknown-field") << field(s(u"Nope"), { s(u"x") }) << s(u"Nope");
    QTest::newRow("rating-6") << field(s(u"Rating"), { s(u"6") }) << s(u"Rating");
    QTest::newRow("rating-0") << field(s(u"Rating"), { s(u"0") }) << s(u"Rating");
    QTest::newRow("rating-quarter") << field(s(u"Rating"), { s(u"3.25") }) << s(u"Rating");
    QTest::newRow("rating-words") << field(s(u"Rating"), { s(u"good") }) << s(u"Rating");
    QTest::newRow("rating-two") << field(s(u"Rating"), { s(u"3"), s(u"4") }) << s(u"Rating");
    QTest::newRow("track-slash") << field(s(u"Track#"), { s(u"3/9") }) << s(u"Track#");
    QTest::newRow("track-two") << field(s(u"Track#"), { s(u"3"), s(u"4") }) << s(u"Track#");
    QTest::newRow("disc-count-slash") << field(s(u"Disc Count"), { s(u"1/2") }) << s(u"Disc Count");
    QTest::newRow("other-is-field") << other(s(u"TITLE")) << s(u"TITLE");
    QTest::newRow("other-is-field-lower") << other(s(u"tracktotal")) << s(u"tracktotal");
    QTest::newRow("other-empty") << other(QString()) << QString();
    QTest::newRow("other-equals") << other(s(u"A=B")) << s(u"A=B");
    QTest::newRow("other-non-ascii") << other(s(u"CL\u00c9")) << s(u"CL\u00c9");
    QTest::newRow("other-picture") << other(s(u"METADATA_BLOCK_PICTURE"))
                                   << s(u"METADATA_BLOCK_PICTURE");
    TagChanges emptyPicture;
    emptyPicture.pictures = QList<linuxbee::Picture> { linuxbee::Picture { } };
    QTest::newRow("picture-no-data") << emptyPicture << s(u"picture 1");
}

void TagWriterTest::invalidChanges()
{
    QFETCH(TagChanges, changes);
    QFETCH(QString, startsWith);
    const QString path = copy("basic.flac");
    const QByteArray original = bytesOf(path);
    const WriteResult result = linuxbee::writeTags(path, changes);
    QCOMPARE(result.status, WriteResult::Status::InvalidChange);
    QVERIFY(!result.ok());
    QCOMPARE(result.problems.size(), 1);
    QVERIFY2(result.problems.first().startsWith(startsWith + s(u":")),
        qPrintable(result.problems.first()));
    QCOMPARE(bytesOf(path), original);
}

void TagWriterTest::invalidWritesNothing()
{
    // W-INVALID: one bad entry refuses the whole change; every bad entry is listed.
    const QString path = copy("basic.mp3");
    const QByteArray original = bytesOf(path);
    TagChanges c = usualChanges();
    c.fields.insert(s(u"Nope"), { s(u"x") });
    c.fields.insert(s(u"Rating"), { s(u"11") });
    const WriteResult result = linuxbee::writeTags(path, c);
    QCOMPARE(result.status, WriteResult::Status::InvalidChange);
    QCOMPARE(result.problems.size(), 2);
    QCOMPARE(bytesOf(path), original);
}

void TagWriterTest::statuses()
{
    // W-STATUS
    QCOMPARE(linuxbee::writeTags(m_dir.filePath(s(u"nope.mp3")), usualChanges()).status,
        WriteResult::Status::NotFound);
    QString path = copy("notaudio.mp3");
    QByteArray original = bytesOf(path);
    QCOMPARE(linuxbee::writeTags(path, usualChanges()).status, WriteResult::Status::NotAudio);
    QCOMPARE(bytesOf(path), original);

    path = copy("basic.mp3");
    original = bytesOf(path);
    QFile::setPermissions(path, QFile::ReadOwner);
    const WriteResult result = linuxbee::writeTags(path, usualChanges());
    QCOMPARE(result.status, WriteResult::Status::NotWritable);
    QVERIFY(!result.ok());
    QCOMPARE(bytesOf(path), original);
}

void TagWriterTest::modificationTime()
{
    const QDateTime old = QDateTime(QDate(2001, 2, 3), QTime(4, 5, 6), QTimeZone::UTC);
    auto prepare = [&](const char *name) {
        const QString path = copy(name);
        QFile file(path);
        if (file.open(QIODevice::ReadWrite))
            file.setFileTime(old, QFileDevice::FileModificationTime);
        return path;
    };
    for (const char *name : { "basic.mp3", "basic.flac", "basic.m4a" }) {
        // W-MTIME on: the time is put back.
        QString path = prepare(name);
        QVERIFY2(QFileInfo(path).lastModified(QTimeZone::UTC) == old, name);
        WriteOptions keep;
        keep.keepModificationTime = true;
        QVERIFY2(linuxbee::writeTags(path, usualChanges(), keep).status == WriteResult::Status::Ok, name);
        QVERIFY2(QFileInfo(path).lastModified(QTimeZone::UTC) == old, name);
        QVERIFY2(linuxbee::readTags(path).tags.first(s(u"Title")) == s(u"New Title"), name);

        // W-MTIME off: saving moves the time forward.
        path = prepare(name);
        QVERIFY2(linuxbee::writeTags(path, usualChanges()).status == WriteResult::Status::Ok, name);
        QVERIFY2(QFileInfo(path).lastModified(QTimeZone::UTC) > old.addYears(10), name);

        // W-UNCHANGED: nothing written, so the time stays whatever the option.
        path = prepare(name);
        QVERIFY2(linuxbee::writeTags(path, { }).status == WriteResult::Status::Unchanged, name);
        QVERIFY2(QFileInfo(path).lastModified(QTimeZone::UTC) == old, name);
    }
}

void TagWriterTest::popmFromRating_data()
{
    QTest::addColumn<double>("stars");
    QTest::addColumn<int>("popm");
    // W-RATING: the anchors of R-RATING in docs/spec/tag-reading.md, the other way.
    QTest::newRow("0.5") << 0.5 << 13;
    QTest::newRow("1") << 1.0 << 1;
    QTest::newRow("1.5") << 1.5 << 54;
    QTest::newRow("2") << 2.0 << 64;
    QTest::newRow("2.5") << 2.5 << 118;
    QTest::newRow("3") << 3.0 << 128;
    QTest::newRow("3.5") << 3.5 << 186;
    QTest::newRow("4") << 4.0 << 196;
    QTest::newRow("4.5") << 4.5 << 242;
    QTest::newRow("5") << 5.0 << 255;
    QTest::newRow("0") << 0.0 << 0;
    QTest::newRow("5.5") << 5.5 << 0;
    QTest::newRow("3.2") << 3.2 << 0;
    QTest::newRow("-1") << -1.0 << 0;
}

void TagWriterTest::popmFromRating()
{
    QFETCH(double, stars);
    QFETCH(int, popm);
    QCOMPARE(linuxbee::popmFromRating(stars), popm);
    if (popm != 0) // the two tables agree
        QCOMPARE(linuxbee::ratingFromPopm(popm).value_or(-1), stars);
}

QTEST_APPLESS_MAIN(TagWriterTest)
#include "tst_tagwriter.moc"
