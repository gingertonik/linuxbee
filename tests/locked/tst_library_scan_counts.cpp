// LOCKED ACCEPTANCE TEST (the scan's counts, fix-d). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/library.md (L-SCAN, L-PARTS), applied to copies of
// tests/data/tags/basic.mp3 in folders of this test's own. The re-probe of row 2 (N4) and the
// library audit (F4, F5, F6) found each case.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <unistd.h>

#include "library/library.h"

using namespace linuxbee;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

// music/a/01.mp3 .. music/a/06.mp3 and music/b/01.mp3 .. music/b/06.mp3, in that order of path.
struct Folder {
    QTemporaryDir dir;
    QString root;
    Folder()
    {
        root
            = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()) + QStringLiteral("/music");
        for (const QString &sub : { QStringLiteral("a"), QStringLiteral("b") }) {
            QDir().mkpath(root + QLatin1Char('/') + sub);
            for (int n = 1; n <= 6; ++n) {
                const QString to
                    = QStringLiteral("%1/%2/%3.mp3").arg(root, sub).arg(n, 2, 10, QLatin1Char('0'));
                if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), to))
                    QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
            }
        }
    }
    ~Folder()
    {
        QFile::setPermissions(
            root + QStringLiteral("/b"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    QString path(const QString &relative) const { return root + QLatin1Char('/') + relative; }
};

ScanOptions options()
{
    ScanOptions o;
    o.category = QStringLiteral("Music");
    o.now = kNow;
    return o;
}

QString told(const ScanReport &r)
{
    return QStringLiteral("added %1 updated %2 unchanged %3 missing %4 found %5 notAudio[%6] "
                          "passedOver %7 refused[%8]")
        .arg(r.added.size())
        .arg(r.updated.size())
        .arg(r.unchanged)
        .arg(r.nowMissing.size())
        .arg(r.found.size())
        .arg(r.notAudio.join(QLatin1Char('|')))
        .arg(r.passedOver)
        .arg(r.refused);
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

} // namespace

class LibraryScanCountsTest : public QObject {
    Q_OBJECT
private slots:
    void filesGoneDuringTheLookingAreMissing();
    void aFolderThatCannotBeLookedInIsNotGone();
    void aFileAnotherWriterAddedIsPassedOver();
};

void LibraryScanCountsTest::filesGoneDuringTheLookingAreMissing()
{
    // L-SCAN (N4, F6): a folder taken away while the files are looked at, in parts as ScanRunner
    // looks. Its six tracks are missing when the scan is stored, and none of its files, known or
    // new, is called "not music": they are not there, not other than music.
    Folder f;
    QTemporaryDir dir;
    auto library = Library::open(dir.path() + QStringLiteral("/library.db"));
    QVERIFY(library);
    QCOMPARE(library->scan({ f.root }, options()).added.size(), 12);
    // A file the library does not know yet, in the folder that goes: listed, then gone before it
    // is looked at. It is in no count either (fix-d: job 097 showed fault B unseen without it).
    QVERIFY(QFile::copy(
        QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), f.path(QStringLiteral("b/07.mp3"))));
    const LibraryRead<ScanKnown> known = library->readScanKnown();
    QVERIFY(!known.failed());
    ScanReport sum;
    ScanFindOptions find;
    find.partSize = 3;
    find.part = [&](const ScanFindings &part) {
        sum = addedUp(sum, library->applyScan(part, options()));
    };
    find.progress = [&](int done, int) {
        // b/01 and b/02 have been looked at; b/03 to b/06 are still to come.
        if (done == 8
            && !QDir().rename(f.path(QStringLiteral("b")), f.path(QStringLiteral("away"))))
            QTest::qFail("could not take the folder away", __FILE__, __LINE__);
        return true;
    };
    sum = addedUp(sum, library->applyScan(findForScan({ f.root }, known.value(), find), options()));
    QVERIFY2(sum.refused.isEmpty(), qPrintable(told(sum)));
    QVERIFY2(sum.notAudio.isEmpty(), qPrintable(told(sum)));
    QVERIFY2(sum.added.isEmpty(), qPrintable(told(sum)));
    QVERIFY2(sum.nowMissing.size() == 6, qPrintable(told(sum)));
    QCOMPARE(missingIn(*library, f.path(QStringLiteral("b"))), 6);
    QCOMPARE(missingIn(*library, f.path(QStringLiteral("a"))), 0);
}

void LibraryScanCountsTest::aFolderThatCannotBeLookedInIsNotGone()
{
    // L-SCAN (F4): a folder that cannot be looked in (its permissions, or a mount gone stale) does
    // not say its files have gone. Its tracks are left as they are: none is marked missing.
    if (::geteuid() == 0)
        QSKIP("root may look in any folder");
    Folder f;
    QTemporaryDir dir;
    auto library = Library::open(dir.path() + QStringLiteral("/library.db"));
    QVERIFY(library);
    QCOMPARE(library->scan({ f.root }, options()).added.size(), 12);
    QVERIFY(QFile::setPermissions(f.path(QStringLiteral("b")), QFileDevice::Permissions()));
    const ScanReport r = library->scan({ f.root }, options());
    QVERIFY2(r.refused.isEmpty(), qPrintable(told(r)));
    QVERIFY2(r.nowMissing.isEmpty(), qPrintable(told(r)));
    QCOMPARE(missingIn(*library, f.path(QStringLiteral("b"))), 0);
    QCOMPARE(r.unchanged, 6);
}

void LibraryScanCountsTest::aFileAnotherWriterAddedIsPassedOver()
{
    // L-PARTS (F5): files looked at as new, which the library holds by the time they are stored
    // (another scan stored them meanwhile), are passed over and counted so: the report does not
    // say that twelve music files were nothing at all. No row is made twice.
    Folder f;
    QTemporaryDir dir;
    auto library = Library::open(dir.path() + QStringLiteral("/library.db"));
    QVERIFY(library);
    const LibraryRead<ScanKnown> known = library->readScanKnown();
    QVERIFY(!known.failed());
    const ScanFindings findings = findForScan({ f.root }, known.value());
    QCOMPARE(library->scan({ f.root }, options()).added.size(), 12);
    const ScanReport r = library->applyScan(findings, options());
    QVERIFY2(r.refused.isEmpty(), qPrintable(told(r)));
    QVERIFY2(r.added.isEmpty(), qPrintable(told(r)));
    QVERIFY2(r.passedOver == 12, qPrintable(told(r)));
    QCOMPARE(library->tracks().size(), 12);
}

QTEST_MAIN(LibraryScanCountsTest)
#include "tst_library_scan_counts.moc"
