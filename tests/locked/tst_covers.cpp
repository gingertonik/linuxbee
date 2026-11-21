// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/covers.md (CV-x), applied to
// copies of the fixtures in tests/data/tags/ (basic.mp3 holds one 69-byte PNG
// front cover; v23.mp3 holds no picture) and to picture files made here.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "covers/cover.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

Picture picture(const char16_t *type)
{
    Picture p;
    p.type = s(type);
    p.mimeType = QStringLiteral("image/png");
    p.byteCount = 1;
    return p;
}

// A folder in a temporary place, filled by name.
struct Place {
    QTemporaryDir dir;
    QString root;
    Place() { root = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()); }
    QString path(const char16_t *relative) const { return root + QLatin1Char('/') + s(relative); }
    // A file with these bytes; folders on the way are made.
    QString file(const char16_t *relative, const QByteArray &bytes = QByteArray("picture")) { return fileAt(s(relative), bytes); }
    QString fileAt(const QString &relative, const QByteArray &bytes = QByteArray("picture"))
    {
        const QString target = root + QLatin1Char('/') + relative;
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
            QTest::qFail("could not write a file", __FILE__, __LINE__);
        return target;
    }
    QString music(const char16_t *fixtureName, const char16_t *relative)
    {
        const QString target = path(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + s(fixtureName), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
};

CoverOptions fileFirst()
{
    CoverOptions o;
    o.order = { Cover::Source::File, Cover::Source::Embedded };
    o.fallbackLast = false; // every file before the picture inside
    return o;
}

}

class CoversTest : public QObject {
    Q_OBJECT
private slots:
    void embedded();
    void names();
    void fileInFolder_data();
    void fileInFolder();
    void whatIsNotACover();
    void find();
    void findWithoutData();
    void findNothing();
};

void CoversTest::embedded()
{
    // CV-EMBEDDED
    QCOMPARE(chooseEmbedded({ }), -1);
    QCOMPARE(chooseEmbedded({ picture(u"Front Cover") }), 0);
    QCOMPARE(chooseEmbedded({ picture(u"Back Cover"), picture(u"Front Cover"), picture(u"Front Cover") }), 1);
    QCOMPARE(chooseEmbedded({ picture(u"Back Cover"), picture(u"front cover") }), 1);
    QCOMPARE(chooseEmbedded({ picture(u"Back Cover"), picture(u"Other") }), 0);
    QCOMPARE(chooseEmbedded({ picture(u""), picture(u"Artist") }), 0);
}

void CoversTest::names()
{
    // CV-FILE
    QCOMPARE(coverNames(), QStringList({ s(u"cover"), s(u"artwork"), s(u"front"), s(u"folder"), s(u"album"), s(u"albumart") }));
}

void CoversTest::fileInFolder_data()
{
    QTest::addColumn<QStringList>("files");
    QTest::addColumn<QString>("want"); // empty: none

    QTest::newRow("nothing") << QStringList { } << QString();
    QTest::newRow("cover") << QStringList { s(u"cover.jpg") } << s(u"cover.jpg");
    QTest::newRow("folder") << QStringList { s(u"folder.jpg") } << s(u"folder.jpg");
    QTest::newRow("cover before folder") << QStringList { s(u"folder.jpg"), s(u"cover.jpg") } << s(u"cover.jpg");
    QTest::newRow("front before folder") << QStringList { s(u"front.jpg"), s(u"folder.png") } << s(u"front.jpg"); // W-TP
    QTest::newRow("folder before album") << QStringList { s(u"album.jpg"), s(u"folder.png") } << s(u"folder.png");
    QTest::newRow("album before albumart") << QStringList { s(u"albumart.jpg"), s(u"album.jpeg") } << s(u"album.jpeg");
    QTest::newRow("albumart") << QStringList { s(u"albumart.png"), s(u"scan.jpg") } << s(u"albumart.png");
    QTest::newRow("case of the name") << QStringList { s(u"Folder.JPG") } << s(u"Folder.JPG");
    QTest::newRow("case of the extension") << QStringList { s(u"COVER.Png") } << s(u"COVER.Png");
    QTest::newRow("jpg before jpeg") << QStringList { s(u"cover.jpeg"), s(u"cover.jpg") } << s(u"cover.jpg");
    QTest::newRow("jpeg before png") << QStringList { s(u"cover.png"), s(u"cover.jpeg") } << s(u"cover.jpeg");
    QTest::newRow("the name outranks the extension") << QStringList { s(u"folder.jpg"), s(u"cover.png") } << s(u"cover.png");
    QTest::newRow("two that differ in case") << QStringList { s(u"cover.jpg"), s(u"Cover.jpg") } << s(u"Cover.jpg");
    QTest::newRow("a lone picture") << QStringList { s(u"scan.png"), s(u"notes.txt") } << s(u"scan.png");
    QTest::newRow("a lone picture, upper case") << QStringList { s(u"Scan 01.JPEG") } << s(u"Scan 01.JPEG");
    QTest::newRow("two pictures, neither named") << QStringList { s(u"scan1.png"), s(u"scan2.jpg") } << QString();
    QTest::newRow("a named one among many") << QStringList { s(u"scan1.png"), s(u"scan2.jpg"), s(u"front.jpg") } << s(u"front.jpg");
    QTest::newRow("a name is the whole name") << QStringList { s(u"cover art.jpg"), s(u"my cover.jpg") } << QString();
    QTest::newRow("not a picture") << QStringList { s(u"cover.txt"), s(u"folder.gif"), s(u"front.bmp"), s(u"cover") } << QString();
    QTest::newRow("hidden") << QStringList { s(u".cover.jpg"), s(u".folder.png") } << QString();
    QTest::newRow("hidden does not count as a picture") << QStringList { s(u".hidden.jpg"), s(u"scan.png") } << s(u"scan.png");
    QTest::newRow("below does not count") << QStringList { s(u"art/cover.jpg"), s(u"art/scan.png") } << QString();
}

void CoversTest::fileInFolder()
{
    // CV-FILE
    QFETCH(QStringList, files);
    QFETCH(QString, want);
    Place place;
    QDir().mkpath(place.root + QStringLiteral("/album"));
    for (const QString &name : std::as_const(files))
        place.fileAt(QStringLiteral("album/") + name);
    const QString expected = want.isEmpty() ? QString() : place.root + QStringLiteral("/album/") + want;
    QCOMPARE(coverFileIn(place.root + QStringLiteral("/album")), expected);
    QCOMPARE(coverFileIn(place.root + QStringLiteral("/album/")), expected); // a trailing slash is the same folder
}

void CoversTest::whatIsNotACover()
{
    // CV-FILE: a folder is not a file; a folder that is not there has no cover.
    Place place;
    QDir().mkpath(place.root + QStringLiteral("/album/cover.jpg")); // a folder with a picture's name
    QCOMPARE(coverFileIn(place.root + QStringLiteral("/album")), QString());
    place.file(u"album/folder.png");
    QCOMPARE(coverFileIn(place.root + QStringLiteral("/album")), place.path(u"album/folder.png"));
    QCOMPARE(coverFileIn(place.root + QStringLiteral("/nowhere")), QString());
    QCOMPARE(coverFileIn(QString()), QString());
    QCOMPARE(coverFileIn(place.path(u"album/folder.png")), QString()); // a file is not a folder
}

void CoversTest::find()
{
    // CV-FIND: the order of the sources, and what each answers.
    Place place;
    const QString withPicture = place.music(u"basic.mp3", u"a/one.mp3");
    const QString without = place.music(u"v23.mp3", u"a/two.mp3");
    const QByteArray bytes("not really a jpeg, but twenty-nine");
    const QString folderFile = place.file(u"a/folder.jpg", bytes);

    Cover c = findCover(withPicture);
    QVERIFY(c.source == Cover::Source::Embedded);
    QCOMPARE(c.file, withPicture);
    QCOMPARE(c.mimeType, s(u"image/png"));
    QCOMPARE(c.type, s(u"Front Cover"));
    QCOMPARE(c.byteCount, qsizetype(69));
    QCOMPARE(c.data.size(), qsizetype(69));
    QVERIFY(c.data.startsWith("\x89PNG"));

    c = findCover(withPicture, fileFirst());
    QVERIFY(c.source == Cover::Source::File);
    QCOMPARE(c.file, folderFile);
    QCOMPARE(c.mimeType, s(u"image/jpeg"));
    QCOMPARE(c.type, QString());
    QCOMPARE(c.byteCount, bytes.size());
    QCOMPARE(c.data, bytes);

    c = findCover(without); // no picture inside: the folder's file
    QVERIFY(c.source == Cover::Source::File);
    QCOMPARE(c.file, folderFile);

    // The mime type goes by the extension.
    Place other;
    const QString lone = other.music(u"v23.mp3", u"b/three.mp3");
    other.file(u"b/Cover.PNG", QByteArray("p"));
    c = findCover(lone);
    QVERIFY(c.source == Cover::Source::File);
    QCOMPARE(c.file, other.path(u"b/Cover.PNG"));
    QCOMPARE(c.mimeType, s(u"image/png"));
    QCOMPARE(c.byteCount, qsizetype(1));
    QFile::remove(other.path(u"b/Cover.PNG"));
    other.file(u"b/front.jpeg");
    QCOMPARE(findCover(lone).mimeType, s(u"image/jpeg"));

    // A music file that is not there, or is not audio: the folder's file still counts.
    c = findCover(place.path(u"a/gone.mp3"));
    QVERIFY(c.source == Cover::Source::File);
    QCOMPARE(c.file, folderFile);
    const QString notAudio = place.music(u"notaudio.mp3", u"a/notaudio.mp3");
    QVERIFY(findCover(notAudio).source == Cover::Source::File);
}

void CoversTest::findWithoutData()
{
    // CV-FIND: without the bytes, everything else is the same.
    Place place;
    const QString withPicture = place.music(u"basic.mp3", u"a/one.mp3");
    const QString folderFile = place.file(u"a/cover.png", QByteArray("12345"));
    CoverOptions o;
    o.data = false;
    o.order = { Cover::Source::Embedded }; // by default a cover file comes first (W-TP)
    Cover c = findCover(withPicture, o);
    QVERIFY(c.source == Cover::Source::Embedded);
    QCOMPARE(c.byteCount, qsizetype(69));
    QCOMPARE(c.mimeType, s(u"image/png"));
    QVERIFY(c.data.isEmpty());
    o.order = { Cover::Source::File };
    c = findCover(withPicture, o);
    QVERIFY(c.source == Cover::Source::File);
    QCOMPARE(c.file, folderFile);
    QCOMPARE(c.byteCount, qsizetype(5));
    QVERIFY(c.data.isEmpty());
}

void CoversTest::findNothing()
{
    // CV-FIND: nothing found, and sources left out.
    Place place;
    const QString without = place.music(u"v23.mp3", u"a/two.mp3");
    const QString withPicture = place.music(u"basic.mp3", u"b/one.mp3");
    const auto none = [](const Cover &c) {
        return c.source == Cover::Source::None && c.file.isEmpty() && c.mimeType.isEmpty() && c.type.isEmpty()
            && c.byteCount == 0 && c.data.isEmpty();
    };
    QVERIFY(none(findCover(without)));
    QVERIFY(none(findCover(place.path(u"nowhere/x.mp3"))));
    QVERIFY(none(findCover(QString())));

    place.file(u"a/cover.jpg");
    CoverOptions embeddedOnly;
    embeddedOnly.order = { Cover::Source::Embedded };
    QVERIFY(none(findCover(without, embeddedOnly))); // the file is there and is not asked for
    CoverOptions fileOnly;
    fileOnly.order = { Cover::Source::File };
    QVERIFY(none(findCover(withPicture, fileOnly))); // the picture inside is not asked for
    CoverOptions nothing;
    nothing.order = { };
    QVERIFY(none(findCover(withPicture, nothing)));
    CoverOptions odd;
    odd.order = { Cover::Source::None, Cover::Source::File, Cover::Source::File };
    QVERIFY(findCover(without, odd).source == Cover::Source::File);
}

QTEST_MAIN(CoversTest)
#include "tst_covers.moc"
