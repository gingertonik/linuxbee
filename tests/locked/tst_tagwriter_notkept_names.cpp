// LOCKED ACCEPTANCE TEST (tag writing: the lines of "not kept" use readable names, and a value meant to be empty is never named).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-writing.md (W-ID3VER, "Not kept").
#include <QTemporaryDir>
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

using linuxbee::TagChanges;
using linuxbee::WriteOptions;
using linuxbee::WriteResult;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

WriteOptions as(WriteOptions::Id3Version version)
{
    WriteOptions o;
    o.id3Version = version;
    return o;
}

struct Box {
    QTemporaryDir dir;
    QString copy(const char *fixture)
    {
        const QString to = dir.path() + QLatin1Char('/') + QLatin1String(fixture);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(fixture), to))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return to;
    }
};

}

class TagWriterNotKeptNamesTest : public QObject {
    Q_OBJECT
private slots:
    void readableNames();
    void aValueMeantToBeEmptyIsNotNamed();
};

void TagWriterNotKeptNamesTest::readableNames()
{
    // W-ID3VER, not kept: the four tags outside the catalogue that ID3v2.3 has no frame for are named as a person would.
    Box box;
    const QString path = box.copy("basic.mp3"); // GEN: ID3v2.4
    TagChanges c;
    c.fields = { { s(u"Mood"), { s(u"Wistful") } } };
    c.other = {
        { s(u"DISCSUBTITLE"), { s(u"Set Subtitle") } },
        { s(u"RELEASEDATE"), { s(u"2019-03-08") } },
        { s(u"TAGGINGDATE"), { s(u"2021-02-03T04:05:06") } },
        { s(u"PRODUCEDNOTICE"), { s(u"2019 Produced") } },
        { s(u"MY OWN KEY"), { s(u"kept in any version") } },
    };
    QCOMPARE(linuxbee::writeTags(path, c, as(WriteOptions::Id3Version::V24)).notKept, QStringList());
    QCOMPARE(linuxbee::readTags(path).tags.other.value(s(u"PRODUCEDNOTICE")), QStringList({ s(u"2019 Produced") }));
    TagChanges title;
    title.fields = { { s(u"Title"), { s(u"Another") } } };
    const WriteResult r = linuxbee::writeTags(path, title, as(WriteOptions::Id3Version::V23));
    QCOMPARE(r.status, WriteResult::Status::Ok);
    QCOMPARE(r.notKept, QStringList({ s(u"Disc Subtitle"), s(u"Mood"), s(u"Produced Notice"), s(u"Release Date"), s(u"Tagging Date") }));
}

void TagWriterNotKeptNamesTest::aValueMeantToBeEmptyIsNotNamed()
{
    // W-ID3VER, not kept: only a value the save meant the file to hold can be "not kept".
    Box box;
    QString path = box.copy("v23.mp3");
    TagChanges c;
    c.fields = { { s(u"Mood"), { } }, { s(u"Original Year"), { } }, { s(u"Title"), { s(u"Another") } } };
    c.other = { { s(u"DISCSUBTITLE"), { } } };
    WriteResult r = linuxbee::writeTags(path, c);
    QCOMPARE(r.status, WriteResult::Status::Ok);
    QCOMPARE(r.notKept, QStringList());
    // The same on a file that had them: removing them while going to ID3v2.3 loses nothing the save meant to keep.
    path = box.copy("basic.mp3");
    TagChanges set;
    set.fields = { { s(u"Mood"), { s(u"Wistful") } } };
    set.other = { { s(u"DISCSUBTITLE"), { s(u"Set Subtitle") } }, { s(u"RELEASEDATE"), { s(u"2019-03-08") } } };
    QVERIFY(linuxbee::writeTags(path, set, as(WriteOptions::Id3Version::V24)).ok());
    r = linuxbee::writeTags(path, c, as(WriteOptions::Id3Version::V23));
    QCOMPARE(r.status, WriteResult::Status::Ok);
    QCOMPARE(r.notKept, QStringList({ s(u"Release Date") })); // the one the change did not name
}

QTEST_MAIN(TagWriterNotKeptNamesTest)
#include "tst_tagwriter_notkept_names.moc"
