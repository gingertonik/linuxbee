// LOCKED ACCEPTANCE TEST (what is mounted now is read; a move is not an unplug; fix-m1b). Written
// by the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/library.md (L-UNPLUGGED, L-FOLLOW) and the review of M-1 (B1, N1).
// B1: /proc/self/mountinfo was never read, so a share mounted under the music folder looked
// unmounted. LINUXBEE_MOUNTINFO names a scratch table of what is mounted, as LINUXBEE_FSTAB names
// a scratch fstab; one row reads the real /proc file. N1: tracks whose files were moved elsewhere
// in the scan (a watched inbox emptied into the library) are followed, not unplugged. Every file
// is made here; each has its own time, so that a move is told by size and time alone.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "library/library.h"
#include "library/mounts.h"

using namespace linuxbee;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

ScanOptions options()
{
    ScanOptions o;
    o.category = QStringLiteral("Music");
    o.now = kNow;
    return o;
}

QString told(const ScanReport &r)
{
    return QStringLiteral("added %1 followed %2 missing %3 unplugged[%4]")
        .arg(r.added.size())
        .arg(r.followed.size())
        .arg(r.nowMissing.size())
        .arg(r.unplugged.join(QLatin1Char(' ')));
}

// `count` music files in `folder`, each a copy of a fixture with a time of its own.
void put(const QString &folder, int count)
{
    static int made = 0;
    const char *kinds[] = { "basic.mp3", "basic.flac", "basic.ogg", "basic.m4a" };
    QDir().mkpath(folder);
    for (int n = 1; n <= count; ++n, ++made) {
        const QString kind = QLatin1String(kinds[made % 4]);
        const QString path = QStringLiteral("%1/%2.%3")
                                 .arg(folder)
                                 .arg(n, 2, 10, QLatin1Char('0'))
                                 .arg(QFileInfo(kind).suffix());
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + kind, path))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        QFile file(path);
        if (!file.open(QIODevice::ReadWrite)
            || !file.setFileTime(
                QDateTime(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC).addSecs(made * 60),
                QFileDevice::FileModificationTime))
            QTest::qFail("could not set a fixture's time", __FILE__, __LINE__);
    }
}

QStringList filesIn(const QString &folder)
{
    QStringList out;
    for (const QFileInfo &info : QDir(folder).entryInfoList(QDir::Files, QDir::Name))
        out.append(info.absoluteFilePath());
    return out;
}

// Moves `count` files of `from` into `to` (a rename: the same size and time).
void moveFiles(const QString &from, const QString &to, int count)
{
    QDir().mkpath(to);
    const QStringList files = filesIn(from);
    for (int n = 0; n < count && n < files.size(); ++n) {
        if (!QFile::rename(files.at(n), to + QLatin1Char('/') + QFileInfo(files.at(n)).fileName()))
            QTest::qFail("could not move a file", __FILE__, __LINE__);
    }
}

void removeFiles(const QString &folder, int count)
{
    const QStringList files = filesIn(folder);
    for (int n = 0; n < count && n < files.size(); ++n)
        QFile::remove(files.at(n));
}

}

class LibraryScanMountsTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void aMountedShareEmptiedIsMarked();
    void aMountedShareMovedIsFollowed();
    void aShareNotMountedIsUnplugged();
    void theRealTableIsRead();
    void anInboxEmptiedIntoTheLibraryIsFollowed();
    void halfMovedHalfDeletedIsNotUnplugged();
    void anInboxEmptiedOfEverythingIsUnplugged();

private:
    QTemporaryDir m_dir;
    QString m_root, m_nas, m_fstab, m_mountinfo;
    std::unique_ptr<Library> m_library;
    void tables(bool mounted)
    {
        QFile fstab(m_fstab);
        QVERIFY(fstab.open(QIODevice::WriteOnly | QIODevice::Truncate));
        fstab.write(QStringLiteral("server:/share %1 nfs defaults 0 0\n").arg(m_nas).toUtf8());
        QFile mountinfo(m_mountinfo);
        QVERIFY(mountinfo.open(QIODevice::WriteOnly | QIODevice::Truncate));
        mountinfo.write("22 1 0:21 / / rw,relatime shared:1 - btrfs /dev/sda1 rw\n");
        if (mounted)
            mountinfo.write(
                QStringLiteral("41 22 0:40 / %1 rw,relatime shared:9 - nfs server:/share rw\n")
                    .arg(m_nas)
                    .toUtf8());
    }
};

void LibraryScanMountsTest::init()
{
    QVERIFY(m_dir.isValid());
    const QString base = QFileInfo(m_dir.path()).canonicalFilePath();
    const QString round
        = base + QStringLiteral("/r") + QString::number(QDateTime::currentMSecsSinceEpoch());
    m_root = round + QStringLiteral("/music");
    m_nas = m_root + QStringLiteral("/nas");
    m_fstab = round + QStringLiteral("/fstab");
    m_mountinfo = round + QStringLiteral("/mountinfo");
    QVERIFY(QDir().mkpath(round));
    qputenv("LINUXBEE_FSTAB", QFile::encodeName(m_fstab));
    qputenv("LINUXBEE_MOUNTINFO", QFile::encodeName(m_mountinfo));
    tables(true);
    m_library = Library::open(round + QStringLiteral("/library.db"));
    QVERIFY(m_library);
}

void LibraryScanMountsTest::cleanup()
{
    m_library.reset();
    qunsetenv("LINUXBEE_FSTAB");
    qunsetenv("LINUXBEE_MOUNTINFO");
}

void LibraryScanMountsTest::aMountedShareEmptiedIsMarked()
{
    // B1: a share the table names, mounted now, emptied of music: nothing is unplugged, so its
    // tracks are marked missing.
    put(m_root + QStringLiteral("/a"), 2);
    put(m_nas, 3);
    QCOMPARE(m_library->scan({ m_root }, options()).added.size(), 5);
    removeFiles(m_nas, 3);
    const ScanReport r = m_library->scan({ m_root }, options());
    QVERIFY2(r.nowMissing.size() == 3 && r.unplugged.isEmpty(), qPrintable(told(r)));
}

void LibraryScanMountsTest::aMountedShareMovedIsFollowed()
{
    // B1: the same share, its files moved elsewhere in the root: followed, not added again.
    put(m_root + QStringLiteral("/a"), 2);
    put(m_nas, 3);
    QCOMPARE(m_library->scan({ m_root }, options()).added.size(), 5);
    moveFiles(m_nas, m_root + QStringLiteral("/a/moved"), 3);
    const ScanReport r = m_library->scan({ m_root }, options());
    QVERIFY2(r.followed.size() == 3 && r.added.isEmpty() && r.nowMissing.isEmpty()
            && r.unplugged.isEmpty(),
        qPrintable(told(r)));
}

void LibraryScanMountsTest::aShareNotMountedIsUnplugged()
{
    // Rule 1, as before: the share is not in the table of what is mounted, holds nothing:
    // unplugged.
    put(m_root + QStringLiteral("/a"), 2);
    put(m_nas, 3);
    QCOMPARE(m_library->scan({ m_root }, options()).added.size(), 5);
    removeFiles(m_nas, 3);
    tables(false);
    const ScanReport r = m_library->scan({ m_root }, options());
    QVERIFY2(r.nowMissing.isEmpty() && r.unplugged == QStringList({ m_nas }), qPrintable(told(r)));
}

void LibraryScanMountsTest::theRealTableIsRead()
{
    // B1, without the seam: what is mounted now is read from /proc/self/mountinfo, which holds `/`.
    qunsetenv("LINUXBEE_MOUNTINFO");
    const QSet<QString> mounted = linuxbee::mounts::mountedNow();
    QVERIFY(!mounted.isEmpty());
    QVERIFY(mounted.contains(QStringLiteral("/")));
}

void LibraryScanMountsTest::anInboxEmptiedIntoTheLibraryIsFollowed()
{
    // N1: two library folders; everything in the inbox moved into the other by a file manager. The
    // tracks are followed: a move is not an unplug.
    const QString main = m_root + QStringLiteral("/main");
    const QString inbox = m_root + QStringLiteral("/inbox");
    put(main, 2);
    put(inbox, 3);
    QCOMPARE(m_library->scan({ main, inbox }, options()).added.size(), 5);
    moveFiles(inbox, main + QStringLiteral("/in"), 3);
    const ScanReport r = m_library->scan({ main, inbox }, options());
    QVERIFY2(r.followed.size() == 3 && r.added.isEmpty() && r.nowMissing.isEmpty()
            && r.unplugged.isEmpty(),
        qPrintable(told(r)));
}

void LibraryScanMountsTest::halfMovedHalfDeletedIsNotUnplugged()
{
    // N1: the moved half does not count toward "more than half have no file"; the deleted half is
    // then not more than half. The moved are followed, the deleted marked.
    const QString main = m_root + QStringLiteral("/main");
    const QString inbox = m_root + QStringLiteral("/inbox");
    put(main, 2);
    put(inbox, 4);
    QCOMPARE(m_library->scan({ main, inbox }, options()).added.size(), 6);
    moveFiles(inbox, main + QStringLiteral("/in"), 2);
    removeFiles(inbox, 2);
    const ScanReport r = m_library->scan({ main, inbox }, options());
    QVERIFY2(r.followed.size() == 2 && r.nowMissing.size() == 2 && r.unplugged.isEmpty(),
        qPrintable(told(r)));
}

void LibraryScanMountsTest::anInboxEmptiedOfEverythingIsUnplugged()
{
    // Rule 2, as before: nothing was moved; the inbox is empty where it held tracks.
    const QString main = m_root + QStringLiteral("/main");
    const QString inbox = m_root + QStringLiteral("/inbox");
    put(main, 2);
    put(inbox, 3);
    QCOMPARE(m_library->scan({ main, inbox }, options()).added.size(), 5);
    removeFiles(inbox, 3);
    const ScanReport r = m_library->scan({ main, inbox }, options());
    QVERIFY2(r.nowMissing.isEmpty() && r.unplugged == QStringList({ inbox }), qPrintable(told(r)));
}

QTEST_GUILESS_MAIN(LibraryScanMountsTest)
#include "tst_library_scan_mounts.moc"
