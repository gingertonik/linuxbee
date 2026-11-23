// LOCKED ACCEPTANCE TEST (a library folder that looks unplugged, fix-m1). Written by the reviewer,
// not the builder. The builder makes these pass by changing src/, never by editing this file. A
// test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/library.md (L-UNPLUGGED), applied to copies of
// tests/data/tags/basic.mp3 in folders of this test's own. LINUXBEE_FSTAB names a table of mounts
// of the test's own, so the machine's /etc/fstab plays no part.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "library/library.h"

using namespace linuxbee;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

// music/a/01.mp3, music/a/02.mp3 and music/b/01.mp3 .. music/b/04.mp3.
struct Folder {
    QTemporaryDir dir;
    QString root;
    QString a, b;
    Folder()
    {
        root
            = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()) + QStringLiteral("/music");
        a = root + QStringLiteral("/a");
        b = root + QStringLiteral("/b");
        put(a, 2);
        put(b, 4);
    }
    static void put(const QString &folder, int count)
    {
        QDir().mkpath(folder);
        for (int n = 1; n <= count; ++n) {
            if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"),
                    QStringLiteral("%1/%2.mp3").arg(folder).arg(n, 2, 10, QLatin1Char('0'))))
                QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        }
    }
    void drop(int count) const
    {
        for (int n = 1; n <= count; ++n)
            QFile::remove(QStringLiteral("%1/%2.mp3").arg(b).arg(n, 2, 10, QLatin1Char('0')));
    }
};

ScanOptions options()
{
    ScanOptions o;
    o.category = QStringLiteral("Music");
    o.now = kNow;
    return o;
}

int missingIn(const Library &library, const QString &folder)
{
    int n = 0;
    for (const LibraryTrack &t : library.tracks()) {
        if (t.missing && t.path.startsWith(folder + QLatin1Char('/')))
            ++n;
    }
    return n;
}

QString told(const ScanReport &r)
{
    return QStringLiteral("added %1 unchanged %2 missing %3 notAudio %4 refused[%5]")
        .arg(r.added.size())
        .arg(r.unchanged)
        .arg(r.nowMissing.size())
        .arg(r.notAudio.size())
        .arg(r.refused);
}

} // namespace

class LibraryScanUnpluggedTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void anEmptiedFolderIsNotGone();
    void aFolderThatIsGoneIsNotGone();
    void aFolderThatLostAFewIsMarked();
    void aSubfolderEmptiedUnderARootWithMusicIsMarked();
    void aMountPointInsideARootIsNotGone();
    void aMountPointThatHoldsMusicIsScanned();
    void aLibraryFolderTheTableNamesAndHoldsMusicIsScanned();

private:
    QTemporaryDir tables;
    QString fstab;
    void writeFstab(const QString &point)
    {
        QFile file(fstab);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        if (!point.isEmpty())
            file.write(QStringLiteral("server:/share %1 nfs defaults 0 0\n").arg(point).toUtf8());
    }
};

void LibraryScanUnpluggedTest::init()
{
    fstab = tables.path() + QStringLiteral("/fstab");
    writeFstab(QString());
    qputenv("LINUXBEE_FSTAB", QFile::encodeName(fstab));
}

void LibraryScanUnpluggedTest::cleanup()
{
    qunsetenv("LINUXBEE_FSTAB");
}

void LibraryScanUnpluggedTest::anEmptiedFolderIsNotGone()
{
    // L-UNPLUGGED, rule 2: a library folder with no music file in it now, where more than half of
    // its known tracks were, looks unplugged (an empty mount point). None of its tracks is marked.
    Folder f;
    QTemporaryDir dir;
    auto library = Library::open(dir.path() + QStringLiteral("/library.db"));
    QVERIFY(library);
    QCOMPARE(library->scan({ f.a, f.b }, options()).added.size(), 6);
    f.drop(4);
    const ScanReport r = library->scan({ f.a, f.b }, options());
    QVERIFY2(r.refused.isEmpty() && r.nowMissing.isEmpty(), qPrintable(told(r)));
    QCOMPARE(missingIn(*library, f.b), 0);
    QCOMPARE(r.unchanged, 2);
}

void LibraryScanUnpluggedTest::aFolderThatIsGoneIsNotGone()
{
    // Rule 2: a library folder that is not there at all (a USB disk's folder, gone with the disk)
    // holds no music file either.
    Folder f;
    QTemporaryDir dir;
    auto library = Library::open(dir.path() + QStringLiteral("/library.db"));
    QVERIFY(library);
    QCOMPARE(library->scan({ f.a, f.b }, options()).added.size(), 6);
    QVERIFY(QDir(f.b).removeRecursively());
    const ScanReport r = library->scan({ f.a, f.b }, options());
    QVERIFY2(r.refused.isEmpty() && r.nowMissing.isEmpty(), qPrintable(told(r)));
    QCOMPARE(missingIn(*library, f.b), 0);
}

void LibraryScanUnpluggedTest::aFolderThatLostAFewIsMarked()
{
    // Rule 2's other side: a folder that lost a file but still holds music is scanned as before.
    Folder f;
    QTemporaryDir dir;
    auto library = Library::open(dir.path() + QStringLiteral("/library.db"));
    QVERIFY(library);
    QCOMPARE(library->scan({ f.a, f.b }, options()).added.size(), 6);
    f.drop(3);
    const ScanReport r = library->scan({ f.a, f.b }, options());
    QVERIFY2(r.nowMissing.size() == 3, qPrintable(told(r)));
    QCOMPARE(missingIn(*library, f.b), 3);
}

void LibraryScanUnpluggedTest::aSubfolderEmptiedUnderARootWithMusicIsMarked()
{
    // Rule 2 is for the library's folders, the ones it scans, not every folder below them: an album
    // folder emptied under a root that still holds music is marked, as before.
    Folder f;
    QTemporaryDir dir;
    auto library = Library::open(dir.path() + QStringLiteral("/library.db"));
    QVERIFY(library);
    QCOMPARE(library->scan({ f.root }, options()).added.size(), 6);
    f.drop(4);
    const ScanReport r = library->scan({ f.root }, options());
    QVERIFY2(r.nowMissing.size() == 4, qPrintable(told(r)));
    QCOMPARE(missingIn(*library, f.b), 4);
}

void LibraryScanUnpluggedTest::aMountPointInsideARootIsNotGone()
{
    // Rule 1: the root `music` is scanned and the table names `music/b`, where nothing is mounted
    // now and no music file is left (a share mounted under the music folder, gone). None of b's
    // tracks is marked; a's lost file is, as before.
    Folder f;
    QTemporaryDir dir;
    auto library = Library::open(dir.path() + QStringLiteral("/library.db"));
    QVERIFY(library);
    QCOMPARE(library->scan({ f.root }, options()).added.size(), 6);
    writeFstab(f.b);
    f.drop(4);
    QVERIFY(QFile::remove(f.a + QStringLiteral("/01.mp3")));
    const ScanReport r = library->scan({ f.root }, options());
    QVERIFY2(r.nowMissing.size() == 1, qPrintable(told(r)));
    QCOMPARE(missingIn(*library, f.b), 0);
    QCOMPARE(missingIn(*library, f.a), 1);
}

void LibraryScanUnpluggedTest::aMountPointThatHoldsMusicIsScanned()
{
    // Rule 1's other side, and the reason for "no music file": the table names `music/b` and the
    // mount table shows nothing there, yet b holds music. What is there is the truth (job 105: a
    // namespace in which /etc/fstab's /home is not mounted must not hide every folder below it).
    // b is scanned as before: its 2 files gone are marked, and a new file is added.
    Folder f;
    QTemporaryDir dir;
    auto library = Library::open(dir.path() + QStringLiteral("/library.db"));
    QVERIFY(library);
    writeFstab(f.b);
    QCOMPARE(library->scan({ f.root }, options()).added.size(), 6);
    f.drop(2);
    QVERIFY(QFile::copy(
        QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), f.b + QStringLiteral("/05.flac")));
    const ScanReport r = library->scan({ f.root }, options());
    QVERIFY2(r.nowMissing.size() == 2 && r.added.size() == 1, qPrintable(told(r)));
    QCOMPARE(missingIn(*library, f.b), 2);
}

void LibraryScanUnpluggedTest::aLibraryFolderTheTableNamesAndHoldsMusicIsScanned()
{
    // Job 105's case at a library folder (the orchestrator, 17:56): `b` is scanned as a library
    // folder of its own, the table names it, the mount table shows nothing there, and it holds
    // music. It is scanned as normal: its 2 files gone are marked, and a new one is added.
    Folder f;
    QTemporaryDir dir;
    auto library = Library::open(dir.path() + QStringLiteral("/library.db"));
    QVERIFY(library);
    writeFstab(f.b);
    QCOMPARE(library->scan({ f.a, f.b }, options()).added.size(), 6);
    f.drop(2);
    QVERIFY(QFile::copy(
        QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), f.b + QStringLiteral("/05.flac")));
    const ScanReport r = library->scan({ f.a, f.b }, options());
    QVERIFY2(r.nowMissing.size() == 2 && r.added.size() == 1, qPrintable(told(r)));
    QCOMPARE(missingIn(*library, f.b), 2);
}

QTEST_MAIN(LibraryScanUnpluggedTest)
#include "tst_library_scan_unplugged.moc"
