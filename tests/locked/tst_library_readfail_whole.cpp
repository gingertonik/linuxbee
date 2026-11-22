// LOCKED ACCEPTANCE TEST (L-READFAIL 5: only a whole reading of the tracks ends the silence,
// fix-r3b). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason, and
// left alone.
//
// Expected values are rules in docs/spec/library.md (L-READFAIL 5). An answer (the list, the count,
// the scan's start) that the database cannot give is said once; until a reading of the whole
// library succeeds again, a further one says nothing more. A change refused for a failed look
// always says it (L-READFAIL 2). The reads are made to fail by damaging one page of a library file
// made here.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "library/library.h"

using namespace linuxbee;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
constexpr int kTracks = 300;

QString pathOf(int i)
{
    return QStringLiteral("/music/a%1.mp3").arg(i, 4, 10, QLatin1Char('0'));
}

// The page of the database file, by its number, overwritten with bytes no page has. SQLite then
// answers "database disk image is malformed" for every read that needs that page, and every
// other read as before (L-READFAIL, "How a test makes a read fail").
enum class Damage { FirstLeaf, LastLeaf, PathIndex };

// A library of kTracks tracks (ids 1 to kTracks, paths pathOf(id - 1)), written through the
// library itself, then one page damaged while no program has it open. Each track's tags are
// long, so the table's rows spread over many pages and the first leaf holds only the first few.
bool makeDamaged(const QString &file, Damage damage)
{
    {
        auto library = Library::open(file);
        if (!library)
            return false;
        ScanFindings findings;
        for (int i = 0; i < kTracks; ++i) {
            LibraryTrack t;
            t.path = pathOf(i);
            t.sizeBytes = 1000;
            t.modified = kNow;
            t.tags.fields.insert(QStringLiteral("Title"), { QStringLiteral("Title %1").arg(i) });
            t.tags.fields.insert(QStringLiteral("Comment"), { QString(600, QLatin1Char('y')) });
            findings.fresh.append(t);
        }
        ScanOptions o;
        o.category = QStringLiteral("Music");
        o.now = kNow;
        if (library->applyScan(findings, o).added.size() != kTracks)
            return false;
    }
    int pageSize = 0;
    int tableRoot = 0;
    int indexRoot = 0;
    {
        QSqlDatabase raw
            = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("damage"));
        raw.setDatabaseName(file);
        if (!raw.open())
            return false;
        QSqlQuery q(raw);
        q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
        if (q.exec(QStringLiteral("PRAGMA page_size")) && q.next())
            pageSize = q.value(0).toInt();
        if (q.exec(QStringLiteral("SELECT rootpage FROM sqlite_master WHERE name = 'tracks'"))
            && q.next())
            tableRoot = q.value(0).toInt();
        if (q.exec(QStringLiteral(
                "SELECT rootpage FROM sqlite_master WHERE type = 'index' AND tbl_name = "
                "'tracks' AND sql IS NULL"))
            && q.next())
            indexRoot = q.value(0).toInt(); // the index SQLite keeps for UNIQUE path
        q.finish();
        raw.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("damage"));
    QFile::remove(file + QStringLiteral("-wal"));
    QFile::remove(file + QStringLiteral("-shm"));
    if (pageSize <= 0 || tableRoot <= 0 || indexRoot <= 0)
        return false;
    QFile f(file);
    if (!f.open(QIODevice::ReadWrite))
        return false;
    QByteArray bytes = f.readAll();
    const auto page = [&](int number) { return qsizetype(number - 1) * pageSize; };
    const auto u16 = [&](qsizetype at) { return (uchar(bytes[at]) << 8) | uchar(bytes[at + 1]); };
    const auto u32 = [&](qsizetype at) {
        return int((uint(uchar(bytes[at])) << 24) | (uint(uchar(bytes[at + 1])) << 16)
            | (uint(uchar(bytes[at + 2])) << 8) | uint(uchar(bytes[at + 3])));
    };
    int victim = 0;
    if (damage == Damage::PathIndex) {
        victim = indexRoot;
    } else {
        const qsizetype root = page(tableRoot);
        if (uchar(bytes[root]) != 0x05)
            return false; // the table's root must be an interior page
        if (damage == Damage::FirstLeaf)
            victim = u32(root + u16(root + 12)); // the left child of the first cell
        else
            victim = u32(root + 8); // the right-most child
        if (uchar(bytes[page(victim)]) != 0x0D)
            return false; // and its child a leaf
    }
    bytes.replace(page(victim), pageSize, QByteArray(pageSize, char(0xA5)));
    f.seek(0);
    f.write(bytes);
    f.close();
    return true;
}

// The one `failed` of a call: exactly one, with the database's words.

QString said(const QSignalSpy &failed)
{
    QStringList all;
    for (const QList<QVariant> &one : failed)
        all.append(one.at(0).toString());
    return QStringLiteral("%1 failed: [%2]")
        .arg(failed.size())
        .arg(all.join(QStringLiteral(" | ")));
}

}

class LibraryReadFailWholeTest : public QObject {
    Q_OBJECT
private slots:
    void countsDoNotEndTheSilence();
    void theScanStartEndsTheSilence();

private:
    QTemporaryDir dir;
    std::unique_ptr<Library> library;
};

void LibraryReadFailWholeTest::countsDoNotEndTheSilence()
{
    // L-READFAIL 5 (fix-r3b; the review of R-3, note 1): under partial damage the counts, the count
    // and one section's list can still be read. None of them is a reading of the whole library's
    // tracks, so none ends the silence: three rounds of "the counts, then a look that fails", as a
    // library change makes in the window, say `failed` once.
    const QString file = dir.path() + QStringLiteral("/counts.db");
    QVERIFY2(makeDamaged(file, Damage::PathIndex), "the damaged library could not be made");
    library = Library::open(file);
    QVERIFY(library);
    QSignalSpy failed(library.get(), &Library::failed);
    QVERIFY(library->readTracks().failed());
    for (int round = 0; round < 3; ++round) {
        QVERIFY(!library->readCategoryCounts().failed());
        (void)library->readCount();
        (void)library->readTracks(QStringLiteral("Music"));
        QVERIFY(library->readTrackByPath(pathOf(round)).failed());
    }
    QVERIFY2(failed.size() == 1, qPrintable(said(failed)));
}

void LibraryReadFailWholeTest::theScanStartEndsTheSilence()
{
    // L-READFAIL 5: a scan's start is a whole reading of the tracks. Read, it ends the silence, and
    // the next answer that fails is said again.
    const QString file = dir.path() + QStringLiteral("/start.db");
    QVERIFY2(makeDamaged(file, Damage::PathIndex), "the damaged library could not be made");
    library = Library::open(file);
    QVERIFY(library);
    QSignalSpy failed(library.get(), &Library::failed);
    QVERIFY(library->readTracks().failed());
    QVERIFY(library->readTracks().failed());
    QVERIFY2(failed.size() == 1, qPrintable(said(failed)));
    QVERIFY(!library->readScanKnown().failed());
    QVERIFY(library->readTracks().failed());
    QVERIFY2(failed.size() == 2, qPrintable(said(failed)));
}

QTEST_MAIN(LibraryReadFailWholeTest)
#include "tst_library_readfail_whole.moc"
