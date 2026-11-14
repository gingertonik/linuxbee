// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/library.md, L-NAMES (added after the review of
// plan 05): a scan says which names it had to pass over because they are
// not valid UTF-8. The files are made with the system's own calls, since no
// QString can name them.
#include <QtTest>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "library/library.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// Makes an empty file or a folder under `dir` with these exact bytes for a
// name. False when the file system will not have such a name.
bool rawFile(const QString &dir, const QByteArray &name)
{
    const QByteArray path = QFile::encodeName(dir) + '/' + name;
    const int fd = ::open(path.constData(), O_CREAT | O_WRONLY, 0644);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
}

bool rawFolder(const QString &dir, const QByteArray &name)
{
    const QByteArray path = QFile::encodeName(dir) + '/' + name;
    return ::mkdir(path.constData(), 0755) == 0;
}

struct Rig {
    QTemporaryDir dir;
    std::unique_ptr<Library> library = Library::open(QStringLiteral(":memory:"));
    ScanOptions options;
    QString music;

    Rig()
    {
        options.now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
        options.category = QStringLiteral("Music");
        music = dir.path() + QStringLiteral("/music");
        QDir().mkpath(music + QStringLiteral("/a/deep"));
        QDir().mkpath(music + QStringLiteral("/b"));
        put("a/one.flac");
        put("b/two.flac");
    }
    void put(const char *to)
    {
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/basic.flac"), music + QLatin1Char('/') + QLatin1String(to)))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    }
};

}

class LibraryNamesTest : public QObject {
    Q_OBJECT
private slots:
    void nothingToReport();
    void reported();
    void onlyInsideWhatWasScanned();
    void everyScan();
};

void LibraryNamesTest::nothingToReport()
{
    Rig r;
    // Odd but valid names are not what this is about.
    QVERIFY(QFile::copy(r.music + s(u"/a/one.flac"), r.music + s(u"/a/J\u00f3ga \u65e5\u672c.flac")));
    const ScanReport report = r.library->scan({ r.music }, r.options);
    QCOMPARE(report.added.size(), 3);
    QCOMPARE(report.unreadableNames, 0);
    QCOMPARE(report.unreadableIn, QStringList());
}

void LibraryNamesTest::reported()
{
    Rig r;
    // "caf\xe9.flac" is Latin-1, not UTF-8; so are the others.
    if (!rawFile(r.music + s(u"/a"), QByteArray("caf\xe9.flac")))
        QSKIP("this file system refuses names that are not valid UTF-8");
    QVERIFY(rawFile(r.music + s(u"/a"), QByteArray("\xff\xfe.mp3")));
    QVERIFY(rawFile(r.music + s(u"/a/deep"), QByteArray("na\xefve.ogg")));
    QVERIFY(rawFile(r.music + s(u"/a"), QByteArray("notes \xe9.txt"))); // not music by its ending: still a name passed over
    QVERIFY(rawFolder(r.music + s(u"/b"), QByteArray("\xc0lbum"))); // a folder: counted once, not gone into
    const ScanReport report = r.library->scan({ r.music }, r.options);
    // The two readable files are in; nothing else was stored or called "not audio".
    QCOMPARE(report.added.size(), 2);
    QCOMPARE(report.notAudio, QStringList());
    QCOMPARE(r.library->count(), 2);
    QCOMPARE(report.unreadableNames, 5);
    QCOMPARE(report.unreadableIn, QStringList({ r.music + s(u"/a"), r.music + s(u"/a/deep"), r.music + s(u"/b") }));
}

void LibraryNamesTest::onlyInsideWhatWasScanned()
{
    Rig r;
    if (!rawFile(r.music + s(u"/a"), QByteArray("caf\xe9.flac")))
        QSKIP("this file system refuses names that are not valid UTF-8");
    QVERIFY(rawFile(r.music + s(u"/b"), QByteArray("\xff.flac")));
    ScanReport report = r.library->scan({ r.music + s(u"/b") }, r.options);
    QCOMPARE(report.unreadableNames, 1);
    QCOMPARE(report.unreadableIn, QStringList({ r.music + s(u"/b") }));
    // A folder given twice, and a folder inside another: each name once.
    report = r.library->scan({ r.music, r.music + s(u"/a"), r.music }, r.options);
    QCOMPARE(report.unreadableNames, 2);
    QCOMPARE(report.unreadableIn, QStringList({ r.music + s(u"/a"), r.music + s(u"/b") }));
    // A folder that is not there has nothing to report.
    report = r.library->scan({ r.music + s(u"/nowhere") }, r.options);
    QCOMPARE(report.unreadableNames, 0);
}

void LibraryNamesTest::everyScan()
{
    // It is said again at each scan, as long as the names are there; and a
    // cancelled scan claims nothing about names.
    Rig r;
    if (!rawFile(r.music + s(u"/a"), QByteArray("caf\xe9.flac")))
        QSKIP("this file system refuses names that are not valid UTF-8");
    QCOMPARE(r.library->scan({ r.music }, r.options).unreadableNames, 1);
    const ScanReport again = r.library->scan({ r.music }, r.options);
    QCOMPARE(again.unchanged, 2);
    QCOMPARE(again.unreadableNames, 1);
    QVERIFY(::unlink((QFile::encodeName(r.music) + "/a/caf\xe9.flac").constData()) == 0);
    QCOMPARE(r.library->scan({ r.music }, r.options).unreadableNames, 0);
    QVERIFY(rawFile(r.music + s(u"/a"), QByteArray("caf\xe9.flac")));
    ScanOptions stopping = r.options;
    stopping.progress = [](int, int) { return false; };
    const ScanReport stopped = r.library->scan({ r.music }, stopping);
    QVERIFY(stopped.cancelled);
    QCOMPARE(stopped.unreadableNames, 0);
    QCOMPARE(stopped.unreadableIn, QStringList());
}

QTEST_GUILESS_MAIN(LibraryNamesTest)
#include "tst_library_names.moc"
