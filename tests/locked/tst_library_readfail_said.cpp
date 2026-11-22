// LOCKED ACCEPTANCE TEST (L-READFAIL 5: an answer that cannot be read is said once). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
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

class LibraryReadFailSaidTest : public QObject {
    Q_OBJECT
private slots:
    void theFixture();
    void anAnswerIsSaidOnce();
    void aRefusedChangeIsAlwaysSaid();
    void saidAgainAfterAWholeReadingSucceeds();

private:
    QTemporaryDir dir;
    std::unique_ptr<Library> library;
    void open(Damage damage);
};

void LibraryReadFailSaidTest::open(Damage damage)
{
    library.reset();
    const QString file = dir.path() + QStringLiteral("/library-%1.db").arg(int(damage));
    QFile::remove(file);
    QVERIFY2(makeDamaged(file, damage), "the damaged library could not be made");
    library = Library::open(file);
    QVERIFY(library);
}

void LibraryReadFailSaidTest::theFixture()
{
    // What each damage does to the answers, so a failing row below is about the library.
    open(Damage::FirstLeaf);
    QVERIFY(library->readTracks().failed());
    QVERIFY(library->readCount().failed());
    QVERIFY(library->readScanKnown().failed());
    open(Damage::PathIndex);
    QVERIFY(library->readTracks().failed()); // in order of path: through the damaged index
    const LibraryRead<QList<LibraryTrack>> music = library->readTracks(QStringLiteral("Music"));
    QVERIFY(!music.failed()); // by section: another index
    QCOMPARE(music.value().size(), kTracks);
}

void LibraryReadFailSaidTest::anAnswerIsSaidOnce()
{
    // L-READFAIL 5: the list, the count and the scan's start all fail. The library says it once.
    open(Damage::FirstLeaf);
    QSignalSpy failed(library.get(), &Library::failed);
    QVERIFY(library->readTracks().failed());
    QVERIFY(library->readCount().failed());
    QVERIFY(library->readScanKnown().failed());
    QVERIFY(library->readTracks().failed());
    QVERIFY2(failed.size() == 1, qPrintable(said(failed)));
    QVERIFY2(failed.at(0).at(0).toString().contains(QStringLiteral("malformed")),
        qPrintable(said(failed)));
}

void LibraryReadFailSaidTest::aRefusedChangeIsAlwaysSaid()
{
    // L-READFAIL 2 and 5: an answer was said; a change refused for a failed look is still said,
    // each time.
    open(Damage::FirstLeaf);
    QSignalSpy failed(library.get(), &Library::failed);
    QVERIFY(library->readTracks().failed());
    QCOMPARE(library->remove({ 1 }), 0);
    QCOMPARE(library->remove({ 1 }), 0);
    QVERIFY2(failed.size() == 3, qPrintable(said(failed)));
}

void LibraryReadFailSaidTest::saidAgainAfterAWholeReadingSucceeds()
{
    // L-READFAIL 5: a reading of the whole library that succeeds ends the silence; the next answer
    // that fails is said again.
    open(Damage::PathIndex);
    QSignalSpy failed(library.get(), &Library::failed);
    QVERIFY(library->readTracks().failed());
    QVERIFY(library->readTracks().failed());
    QVERIFY2(failed.size() == 1, qPrintable(said(failed)));
    // fix-r3b, the reviewer's change: one section's list is no whole reading; the scan's start is.
    QVERIFY(!library->readTracks(QStringLiteral("Music")).failed());
    QVERIFY(library->readTracks().failed());
    QVERIFY2(failed.size() == 1, qPrintable(said(failed)));
    QVERIFY(!library->readScanKnown().failed());
    QVERIFY(library->readTracks().failed());
    QVERIFY2(failed.size() == 2, qPrintable(said(failed)));
}

QTEST_MAIN(LibraryReadFailSaidTest)
#include "tst_library_readfail_said.moc"
