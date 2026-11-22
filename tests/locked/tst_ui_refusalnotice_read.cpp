// LOCKED ACCEPTANCE TEST (L-WORDS in the refusal notice: a failed read is said as a read). Written
// by the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-WORDS, L-READFAIL 5) and
// docs/spec/library-shell.md (SH-REFUSED). An answer the library could not read is not a change it
// refused: the status bar says "could not be read", with the words a person reads. The reads are
// made to fail by damaging one page of a library file made here.
#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "library/library.h"
#include "shell/refusalnotice.h"

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

// The one `failed` of a call: exactly one, with the database's words.

}

class RefusalNoticeReadTest : public QObject {
    Q_OBJECT
private slots:
    void aReadIsSaidAsARead();
    void aChangeIsStillAChange();
    void aLineThatSaysItAlready();

private:
    QTemporaryDir dir;
    int made = 0;
    std::unique_ptr<Library> open();
};

std::unique_ptr<Library> RefusalNoticeReadTest::open()
{
    const QString file = dir.path() + QStringLiteral("/library-%1.db").arg(++made);
    if (!makeDamaged(file, Damage::FirstLeaf))
        return nullptr;
    return Library::open(file);
}

const QString kDamaged
    = QStringLiteral("the library file is damaged: database disk image is malformed");

void RefusalNoticeReadTest::aReadIsSaidAsARead()
{
    auto library = open();
    QVERIFY(library);
    QMainWindow window;
    RefusalNotice notice(library.get(), window.statusBar());
    QVERIFY(library->readTracks().failed());
    QTRY_COMPARE(notice.count(), 1);
    QCOMPARE(window.statusBar()->currentMessage(),
        QStringLiteral("The library could not be read: ") + kDamaged);
}

void RefusalNoticeReadTest::aChangeIsStillAChange()
{
    // A refused change after a failed read is a change, and says so.
    auto library = open();
    QVERIFY(library);
    QMainWindow window;
    RefusalNotice notice(library.get(), window.statusBar());
    QVERIFY(library->readTracks().failed());
    QTRY_COMPARE(notice.count(), 1);
    QCOMPARE(library->remove({ 1 }), 0);
    QTRY_COMPARE(notice.count(), 2);
    QCOMPARE(window.statusBar()->currentMessage(),
        QStringLiteral("The library could not be changed: ") + kDamaged);
}

void RefusalNoticeReadTest::aLineThatSaysItAlready()
{
    // SH-REFUSED: a command that has said on its own line that the library could not be read is
    // not said over; the notice adds nothing to it.
    auto library = open();
    QVERIFY(library);
    QMainWindow window;
    RefusalNotice notice(library.get(), window.statusBar());
    QVERIFY(library->readTracks().failed());
    window.statusBar()->showMessage(
        QStringLiteral("Could not show the tracks: the library could not be read"));
    QTest::qWait(50);
    QCOMPARE(notice.count(), 0);
    QCOMPARE(window.statusBar()->currentMessage(),
        QStringLiteral("Could not show the tracks: the library could not be read"));
}

QTEST_MAIN(RefusalNoticeReadTest)
#include "tst_ui_refusalnotice_read.moc"
