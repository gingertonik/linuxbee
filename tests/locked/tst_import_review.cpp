// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Review of plan 21: I-LOCATION and I-MAP say the path is cleaned, with no
// "." or ".." left in it. A ".." at a root stays at that root: "/" for a
// Unix path, "C:/" for a path with a drive letter. So a cleaned path never
// starts with "..", never loses its drive letter, and a mapped path cannot
// climb out of the new folder.
#include <QtTest>

#include "import/itunesxml.h"

using linuxbee::PathMapping;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

bool hasDotDot(const QString &path)
{
    return path.split(QLatin1Char('/')).contains(QStringLiteral(".."));
}

}

class ImportReviewTest : public QObject {
    Q_OBJECT
private slots:
    void locationAboveRoot_data();
    void locationAboveRoot();
    void mappingAboveRoot_data();
    void mappingAboveRoot();
    void parsedLocationAboveRoot();
};

void ImportReviewTest::locationAboveRoot_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("expected");
    // I-LOCATION: the result is cleaned (no ., ..).
    QTest::newRow("unix-encoded") << s(u"file:///%2e%2e/%2e%2e/etc/passwd") << s(u"/etc/passwd");
    QTest::newRow("unix-plain") << s(u"file://localhost/../x/a.mp3") << s(u"/x/a.mp3");
    QTest::newRow("drive") << s(u"file:///C:/Music/../../../Windows/a.mp3") << s(u"C:/Windows/a.mp3");
    QTest::newRow("drive-encoded") << s(u"file:///C:/M/%2e%2e/%2e%2e/a.mp3") << s(u"C:/a.mp3");
    QTest::newRow("inside-still-works") << s(u"file:///C:/M/x/../a.mp3") << s(u"C:/M/a.mp3");
}

void ImportReviewTest::locationAboveRoot()
{
    QFETCH(QString, url);
    QFETCH(QString, expected);
    const QString path = linuxbee::pathFromItunesLocation(url);
    QVERIFY2(!hasDotDot(path), qPrintable(path));
    QCOMPARE(path, expected);
}

void ImportReviewTest::mappingAboveRoot_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("from");
    QTest::addColumn<QString>("to");
    QTest::addColumn<QString>("expected");
    // I-MAP: the path is cleaned first, then the old folder is replaced.
    QTest::newRow("drive-unmapped") << s(u"C:\\Music\\..\\..\\..\\Windows\\a.mp3") << s(u"C:/Music")
                                    << s(u"/srv/Music") << s(u"C:/Windows/a.mp3");
    QTest::newRow("drive-root-pair") << s(u"C:/Music/../../x.mp3") << s(u"C:/") << s(u"/srv")
                                     << s(u"/srv/x.mp3");
    QTest::newRow("unix-root-pair") << s(u"/../../etc/passwd") << s(u"/") << s(u"/srv")
                                    << s(u"/srv/etc/passwd");
    QTest::newRow("music2-still-not-music") << s(u"C:/Music/../Music2/a.mp3") << s(u"C:/Music")
                                            << s(u"/srv/Music") << s(u"C:/Music2/a.mp3");
}

void ImportReviewTest::mappingAboveRoot()
{
    QFETCH(QString, path);
    QFETCH(QString, from);
    QFETCH(QString, to);
    QFETCH(QString, expected);
    const QString mapped = linuxbee::mapImportedPath(path, PathMapping { { from, to } });
    QVERIFY2(!hasDotDot(mapped), qPrintable(mapped));
    QCOMPARE(mapped, expected);
}

void ImportReviewTest::parsedLocationAboveRoot()
{
    // I-PARSE uses I-LOCATION for every track.
    const QByteArray xml = "<plist version=\"1.0\"><dict><key>Tracks</key><dict>"
                           "<key>1</key><dict><key>Track ID</key><integer>1</integer>"
                           "<key>Location</key><string>file://localhost/C:/Music/../../../a.mp3</string>"
                           "</dict></dict></dict></plist>";
    const linuxbee::ImportedLibrary lib = linuxbee::parseItunesLibrary(xml);
    QVERIFY(lib.ok);
    QCOMPARE(lib.tracks.size(), 1);
    QCOMPARE(lib.tracks.value(0).location, s(u"C:/a.mp3"));
}

QTEST_APPLESS_MAIN(ImportReviewTest)
#include "tst_import_review.moc"
