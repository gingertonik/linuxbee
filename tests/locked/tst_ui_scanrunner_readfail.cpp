// LOCKED ACCEPTANCE TEST (L-READFAIL 4, SR-REFUSED: a scan whose start cannot be read). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-READFAIL) and docs/spec/scan-runner.md
// (SR-REFUSED). What the library knows, read before the looking, cannot be read: the scan looks
// at no file, ends refused with the database's words, and the library says so once. The read is
// made to fail by damaging one page of a library file made in the test.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <atomic>

#include "library/library.h"
#include "shell/scanrunner.h"

using namespace linuxbee;
using namespace linuxbee::ui;

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

}

class ScanRunnerReadFailTest : public QObject {
    Q_OBJECT
private slots:
    void whatIsKnownCannotBeRead_data();
    void whatIsKnownCannotBeRead();
};

void ScanRunnerReadFailTest::whatIsKnownCannotBeRead_data()
{
    QTest::addColumn<int>("damage");
    // The first leaf: the read fails at once. The last leaf: the read gives many rows first and
    // fails part way; a read that fails part way is a failed read, not a shorter answer.
    QTest::newRow("at once") << int(Damage::FirstLeaf);
    QTest::newRow("part way") << int(Damage::LastLeaf);
}

void ScanRunnerReadFailTest::whatIsKnownCannotBeRead()
{
    QFETCH(int, damage);
    QTemporaryDir dir;
    const QString file = dir.path() + QStringLiteral("/library.db");
    QVERIFY2(makeDamaged(file, Damage(damage)), "the damaged library could not be made");
    auto library = Library::open(file);
    QVERIFY(library);
    const QString music = dir.path() + QStringLiteral("/music");
    QDir().mkpath(music);
    QFile::copy(
        QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), music + QStringLiteral("/one.mp3"));

    std::atomic<int> looked { 0 };
    ScanRunnerHooks hooks;
    hooks.looking = [&looked](int, int) { ++looked; };
    ScanRunner runner(library.get(), hooks);
    QSignalSpy failed(library.get(), &Library::failed);
    QSignalSpy added(library.get(), &Library::tracksAdded);
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QSignalSpy idle(&runner, &ScanRunner::idle);
    ScanOptions o;
    o.category = QStringLiteral("Music");
    o.now = kNow;
    runner.scan({ music }, o);
    QTRY_COMPARE_WITH_TIMEOUT(idle.size(), 1, 10000);
    QCOMPARE(finished.size(), 1);
    const ScanReport report = finished.at(0).at(0).value<ScanReport>();
    QVERIFY2(report.refused.contains(QStringLiteral("malformed")), qPrintable(report.refused));
    QVERIFY(!report.cancelled);
    QVERIFY(report.added.isEmpty());
    QCOMPARE(looked.load(), 0); // no file was looked at
    QCOMPARE(added.size(), 0);
    QCOMPARE(failed.size(), 1);
    QVERIFY2(failed.at(0).at(0).toString().contains(QStringLiteral("malformed")),
        qPrintable(failed.at(0).at(0).toString()));
}

QTEST_MAIN(ScanRunnerReadFailTest)
#include "tst_ui_scanrunner_readfail.moc"
