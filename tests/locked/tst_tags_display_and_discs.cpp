// LOCKED ACCEPTANCE TEST (tags: Display Artist is read from its own tag; Vorbis writes TOTALDISCS).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/track-fields.md F-DISPLAY; docs/spec/tag-writing.md W-TABLE;
// docs/spec/tag-reading.md R-CATALOGUE.
//   W-SF  MusicBee wiki, "Special Fields": Display Artist "is saved to your files in its own frame"
//   W-TG  MusicBee wiki, "Tagging": Display Artist is TXXX/DISPLAY ARTIST (ID3v2) and DISPLAY
//         ARTIST (Vorbis); Total Discs is TOTALDISCS (Vorbis)
//   F-x   a linuxBee decision in the spec (the public pages are silent)
// Every file is a copy of a fixture in a folder of this test's own.
#include <QtTest>

#include "fields/trackfields.h"
#include "tags/tagwriter.h"

using linuxbee::TagChanges;
using linuxbee::TrackFieldSource;
using linuxbee::TrackTags;
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

QString displayOf(const TrackTags &tags)
{
    return TrackFieldSource(tags, s(u"/m/x.flac"))
        .value(s(u"Display Artist"))
        .value_or(s(u"<none>"));
}

TrackTags withArtists(const QStringList &artists)
{
    TrackTags t;
    t.fields.insert(s(u"Artist"), artists);
    return t;
}

}

class TagsDisplayAndDiscsTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void displayFromTheTag();
    void displayFallsBack();
    void displayInFiles_data();
    void displayInFiles();
    void totalDiscsWritten_data();
    void totalDiscsWritten();
    void anotherFieldLeavesThem();
    void clearedCount();
    void bothRead();

private:
    QString copy(const char *fixture);
    QTemporaryDir m_dir;
    int m_counter = 0;
};

void TagsDisplayAndDiscsTest::init()
{
    QVERIFY(m_dir.isValid());
}

QString TagsDisplayAndDiscsTest::copy(const char *fixture)
{
    const QString source = QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(fixture);
    const QString target
        = m_dir.filePath(QStringLiteral("%1-%2").arg(++m_counter).arg(QLatin1String(fixture)));
    if (!QFile::copy(source, target))
        return { };
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    return target;
}

void TagsDisplayAndDiscsTest::displayFromTheTag()
{
    // W-SF: the stored value is what shows.
    TrackTags t = withArtists({ s(u"Justin Timberlake"), s(u"Ciara") });
    t.other.insert(s(u"DISPLAY ARTIST"), { s(u"Justin Timberlake ft. Ciara") });
    QCOMPARE(displayOf(t), s(u"Justin Timberlake ft. Ciara"));
    // F-DISPLAY: two values of the tag are joined as the artists are.
    t.other.insert(s(u"DISPLAY ARTIST"), { s(u"One"), s(u"Two") });
    QCOMPARE(displayOf(t), s(u"One; Two"));
    // F-DISPLAY: spaces round it are kept out, as everywhere a field is read.
    t.other.insert(s(u"DISPLAY ARTIST"), { s(u"  Jay-Z & Kanye  ") });
    QCOMPARE(displayOf(t), s(u"Jay-Z & Kanye"));
}

void TagsDisplayAndDiscsTest::displayFallsBack()
{
    // F-DISPLAY: no tag, or a blank one: the Artist values joined, as before.
    TrackTags t = withArtists({ s(u"Justin Timberlake"), s(u"Ciara") });
    QCOMPARE(displayOf(t), s(u"Justin Timberlake; Ciara"));
    t.other.insert(s(u"DISPLAY ARTIST"), { s(u"   ") });
    QCOMPARE(displayOf(t), s(u"Justin Timberlake; Ciara"));
    QCOMPARE(displayOf(TrackTags()), QString());
    // Another key is not it.
    TrackTags other = withArtists({ s(u"Adele") });
    other.other.insert(s(u"DISPLAYARTIST"), { s(u"Not this") });
    other.other.insert(s(u"DISPLAY COMPOSER"), { s(u"Nor this") });
    QCOMPARE(displayOf(other), s(u"Adele"));
}

void TagsDisplayAndDiscsTest::displayInFiles_data()
{
    QTest::addColumn<QString>("fixture");
    for (const char *name :
        { "basic.mp3", "basic.flac", "basic.ogg", "basic.opus", "basic.m4a", "basic.wv" })
        QTest::newRow(name) << QString::fromLatin1(name);
}

void TagsDisplayAndDiscsTest::displayInFiles()
{
    // W-TG: the tag as a file holds it (TXXX in ID3v2, a comment in Vorbis, a free item elsewhere),
    // written and read back through linuxBee's own reader.
    QFETCH(QString, fixture);
    const QString path = copy(qPrintable(fixture));
    QVERIFY(!path.isEmpty());
    TagChanges c;
    c.fields = { { s(u"Artist"), { s(u"Justin Timberlake"), s(u"Ciara") } } };
    c.other = { { s(u"DISPLAY ARTIST"), { s(u"Justin Timberlake ft. Ciara") } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    const linuxbee::ReadResult read = linuxbee::readTags(path);
    QVERIFY(read.status == linuxbee::ReadResult::Status::Ok);
    QCOMPARE(TrackFieldSource(read.tags, path).value(s(u"Display Artist")),
        std::optional<QString>(s(u"Justin Timberlake ft. Ciara")));
    QCOMPARE(read.tags.fields.value(s(u"Artist")),
        QStringList({ s(u"Justin Timberlake"), s(u"Ciara") }));
}

void TagsDisplayAndDiscsTest::totalDiscsWritten_data()
{
    QTest::addColumn<QString>("fixture");
    QTest::addColumn<bool>("hadDiscTotal");
    QTest::newRow("basic.flac") << s(u"basic.flac") << true; // DISCNUMBER=2, DISCTOTAL=3
    QTest::newRow("basic.ogg") << s(u"basic.ogg") << false; // DISCNUMBER=1, TOTALDISCS=1
    QTest::newRow("basic.opus") << s(u"basic.opus") << false; // neither
}

void TagsDisplayAndDiscsTest::totalDiscsWritten()
{
    // W-TG: Vorbis Total Discs is TOTALDISCS. W-DISCTOTAL (linuxBee's own): a DISCTOTAL the file
    // has is given the same value, never removed; none is ever made.
    QFETCH(QString, fixture);
    QFETCH(bool, hadDiscTotal);
    const QString path = copy(qPrintable(fixture));
    TagChanges c;
    c.fields = { { s(u"Disc#"), { s(u"2") } }, { s(u"Disc Count"), { s(u"4") } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    const QByteArray b = bytesOf(path);
    QVERIFY(b.contains("TOTALDISCS=4"));
    QCOMPARE(b.contains("DISCTOTAL=4"), hadDiscTotal);
    QVERIFY(!b.contains("DISCTOTAL=3"));
    QCOMPARE(b.count("TOTALDISCS="), 1);
    QVERIFY(b.contains("DISCNUMBER=2"));
    const linuxbee::ReadResult read = linuxbee::readTags(path);
    QCOMPARE(read.tags.fields.value(s(u"Disc Count")), QStringList({ s(u"4") }));
    // R-OTHER: two keys that agree are one tag, not a stray one beside the field.
    QVERIFY(!read.tags.other.contains(s(u"DISCTOTAL")));
    QVERIFY(!read.tags.other.contains(s(u"TOTALDISCS")));
    // Only the count moves it: a second write keeps them equal.
    TagChanges again;
    again.fields = { { s(u"Disc Count"), { s(u"5") } } };
    QCOMPARE(linuxbee::writeTags(path, again).status, WriteResult::Status::Ok);
    QVERIFY(bytesOf(path).contains("TOTALDISCS=5"));
    QCOMPARE(bytesOf(path).contains("DISCTOTAL=5"), hadDiscTotal);
    QVERIFY(!bytesOf(path).contains("DISCTOTAL=4"));
    // Tracks are not changed by the ruling: TRACKTOTAL as before (W-TG gives both).
    TagChanges t;
    t.fields = { { s(u"Track#"), { s(u"3") } }, { s(u"Track Count"), { s(u"9") } } };
    QCOMPARE(linuxbee::writeTags(path, t).status, WriteResult::Status::Ok);
    QVERIFY(bytesOf(path).contains("TRACKTOTAL=9"));
}

void TagsDisplayAndDiscsTest::anotherFieldLeavesThem()
{
    // W-DISCTOTAL: a save that names neither Disc# nor Disc Count leaves both keys as they are.
    const QString path = copy("basic.flac");
    TagChanges title;
    title.fields = { { s(u"Title"), { s(u"Another") } } };
    QCOMPARE(linuxbee::writeTags(path, title).status, WriteResult::Status::Ok);
    QByteArray b = bytesOf(path);
    QVERIFY(b.contains("DISCTOTAL=3"));
    QVERIFY(!b.contains("TOTALDISCS="));
    QVERIFY(b.contains("DISCNUMBER=2"));
    const QString ogg = copy("basic.ogg");
    QCOMPARE(linuxbee::writeTags(ogg, title).status, WriteResult::Status::Ok);
    b = bytesOf(ogg);
    QVERIFY(b.contains("TOTALDISCS=1"));
    QVERIFY(!b.contains("DISCTOTAL="));
}

void TagsDisplayAndDiscsTest::clearedCount()
{
    // W-DISCTOTAL: removing Disc Count removes both, or the old count would be read back.
    const QString path = copy("basic.flac");
    TagChanges c;
    c.fields = { { s(u"Disc Count"), { } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    const QByteArray b = bytesOf(path);
    QVERIFY(!b.contains("DISCTOTAL="));
    QVERIFY(!b.contains("TOTALDISCS="));
    QVERIFY(!linuxbee::readTags(path).tags.fields.contains(s(u"Disc Count")));
}

void TagsDisplayAndDiscsTest::bothRead()
{
    // R-CATALOGUE: both spellings are still read. basic.ogg holds TOTALDISCS, basic.flac DISCTOTAL.
    QCOMPARE(linuxbee::readTags(copy("basic.ogg")).tags.fields.value(s(u"Disc Count")),
        QStringList({ s(u"1") }));
    const linuxbee::ReadResult flac = linuxbee::readTags(copy("basic.flac"));
    QCOMPARE(flac.tags.fields.value(s(u"Disc Count")), QStringList({ s(u"3") }));
    QVERIFY(!flac.tags.other.contains(s(u"DISCTOTAL")));
}

QTEST_GUILESS_MAIN(TagsDisplayAndDiscsTest)
#include "tst_tags_display_and_discs.moc"
