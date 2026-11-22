// LOCKED ACCEPTANCE TEST (L-READFAIL: a read that failed is not "nothing there", inside a change).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never by
// editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-READFAIL). Road row 3, package R-1: every
// look the library takes inside a change, and before one, refuses the change when the database
// cannot answer it, says so once (`failed`, with the database's words) and stores nothing. The
// reads are made to fail by damaging one page of a library file made in the test: no hook, and no
// real library.
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
bool saidOnce(const QSignalSpy &failed)
{
    return failed.size() == 1
        && failed.at(0).at(0).toString().contains(QStringLiteral("malformed"));
}

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

class LibraryReadFailTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void theFixture();
    void aLookBeforeAChangeThatFails();
    void refreshAndStatistics();
    void aLookInsideAChangeThatFails();
    void relinkWhenTheListCannotBeRead();
    void aScanWhoseLooksFail();
    void aStoringWhoseDeletedRowsCannotBeRead();
    void theDeletedRowsCannotBeRead();
    void aMoveWhosePathsCannotBeLooked();
    void aPlayStaysAsItIs();

private:
    std::unique_ptr<QTemporaryDir> dir;
    QString file;
    std::unique_ptr<Library> library;
    void open(Damage damage);
};

void LibraryReadFailTest::init()
{
    library.reset();
    dir = std::make_unique<QTemporaryDir>();
    file = dir->path() + QStringLiteral("/library.db");
}

void LibraryReadFailTest::open(Damage damage)
{
    QVERIFY2(makeDamaged(file, damage), "the damaged library could not be made");
    library = Library::open(file);
    QVERIFY(library);
}

void LibraryReadFailTest::theFixture()
{
    // What the damage does, so a failing row below is about the library, not the fixture.
    open(Damage::FirstLeaf);
    QVERIFY(!library->track(1)); // in the damaged page
    QVERIFY(library->track(kTracks)); // not
    QCOMPARE(library->track(kTracks)->path, pathOf(kTracks - 1));
}

void LibraryReadFailTest::aLookBeforeAChangeThatFails()
{
    // L-READFAIL 2: the look that finds out whether there is anything to do (remove, mark deleted,
    // set a section) could not be made: the call changes nothing and says so once.
    open(Damage::FirstLeaf);
    {
        QSignalSpy failed(library.get(), &Library::failed);
        QSignalSpy removed(library.get(), &Library::tracksRemoved);
        QCOMPARE(library->remove({ 1 }), 0);
        QVERIFY2(saidOnce(failed), qPrintable(QStringLiteral("remove: ") + said(failed)));
        QCOMPARE(removed.size(), 0);
    }
    {
        QSignalSpy failed(library.get(), &Library::failed);
        QCOMPARE(library->markDeleted({ 1 }, kNow), 0);
        QVERIFY2(saidOnce(failed), qPrintable(QStringLiteral("markDeleted: ") + said(failed)));
    }
    {
        QSignalSpy failed(library.get(), &Library::failed);
        QCOMPARE(library->setCategory({ 1 }, QStringLiteral("Audiobooks")), 0);
        QVERIFY2(saidOnce(failed), qPrintable(QStringLiteral("setCategory: ") + said(failed)));
    }
    // A track the look can read is as it was: nothing above reached it.
    QCOMPARE(library->track(kTracks)->category, QStringLiteral("Music"));
}

void LibraryReadFailTest::refreshAndStatistics()
{
    open(Damage::FirstLeaf);
    {
        QSignalSpy failed(library.get(), &Library::failed);
        QVERIFY(!library->refresh(1));
        QVERIFY2(saidOnce(failed), qPrintable(QStringLiteral("refresh: ") + said(failed)));
    }
    {
        QSignalSpy failed(library.get(), &Library::failed);
        TrackStatistics stats;
        stats.dateAdded = kNow;
        stats.playCount = 7;
        QVERIFY(!library->setStatistics(1, stats));
        QVERIFY2(saidOnce(failed), qPrintable(QStringLiteral("setStatistics: ") + said(failed)));
    }
}

void LibraryReadFailTest::aLookInsideAChangeThatFails()
{
    // L-READFAIL 2: inside one change, the first move is stored and the second move's look
    // fails. The whole change is given up: the first track keeps its path, and it is said once.
    open(Damage::FirstLeaf);
    QSignalSpy failed(library.get(), &Library::failed);
    QSignalSpy updated(library.get(), &Library::tracksUpdated);
    const QList<FileMove> moves { { pathOf(kTracks - 1), QStringLiteral("/music/moved-last.mp3") },
        { pathOf(0), QStringLiteral("/music/moved-first.mp3") } };
    QCOMPARE(library->recordMoves(moves), 0);
    QVERIFY2(saidOnce(failed), qPrintable(said(failed)));
    QCOMPARE(updated.size(), 0);
    QCOMPARE(library->track(kTracks)->path, pathOf(kTracks - 1));
}

void LibraryReadFailTest::relinkWhenTheListCannotBeRead()
{
    open(Damage::FirstLeaf);
    QSignalSpy failed(library.get(), &Library::failed);
    QCOMPARE(library->relinkPaths(QStringLiteral("/music"), QStringLiteral("/elsewhere")), 0);
    QVERIFY2(saidOnce(failed), qPrintable(said(failed)));
    QCOMPARE(library->track(kTracks)->path, pathOf(kTracks - 1));
}

void LibraryReadFailTest::aScanWhoseLooksFail()
{
    // L-READFAIL 2, L-PARTS, L-DELETED: the storing's looks (the deleted rows old enough to go, the
    // four values by id) cannot be made. The storing is refused as a refused write is: nothing of
    // it is stored, and its report and `failed` give the database's words.
    open(Damage::FirstLeaf);
    ScanFindings findings;
    findings.found = { 1 };
    ScanFindings::Judged was;
    was.path = pathOf(0);
    was.sizeBytes = 1000;
    was.modified = kNow;
    was.missing = true;
    findings.judged.insert(1, was);
    LibraryTrack fresh;
    fresh.path = QStringLiteral("/music/new.mp3");
    fresh.sizeBytes = 5;
    fresh.modified = kNow;
    findings.fresh.append(fresh);
    ScanOptions o;
    o.category = QStringLiteral("Music");
    o.now = kNow;
    QSignalSpy failed(library.get(), &Library::failed);
    QSignalSpy added(library.get(), &Library::tracksAdded);
    const ScanReport report = library->applyScan(findings, o);
    QVERIFY2(report.refused.contains(QStringLiteral("malformed")), qPrintable(report.refused));
    QVERIFY(report.added.isEmpty());
    QVERIFY2(saidOnce(failed), qPrintable(said(failed)));
    QCOMPARE(added.size(), 0);
    QVERIFY(!library->trackByPath(QStringLiteral("/music/new.mp3")));
}

void LibraryReadFailTest::aStoringWhoseDeletedRowsCannotBeRead()
{
    // L-READFAIL 2, L-DELETED: the storing's first look, at the deleted rows old enough to go,
    // cannot be made, and the storing has nothing else to look up by id. It is still refused: the
    // new file is not stored.
    open(Damage::FirstLeaf);
    ScanFindings findings;
    LibraryTrack fresh;
    fresh.path = QStringLiteral("/music/new.mp3");
    fresh.sizeBytes = 5;
    fresh.modified = kNow;
    findings.fresh.append(fresh);
    ScanOptions o;
    o.category = QStringLiteral("Music");
    o.now = kNow;
    QSignalSpy failed(library.get(), &Library::failed);
    const ScanReport report = library->applyScan(findings, o);
    QVERIFY2(report.refused.contains(QStringLiteral("malformed")), qPrintable(report.refused));
    QVERIFY(report.added.isEmpty());
    QVERIFY2(saidOnce(failed), qPrintable(said(failed)));
    QVERIFY(!library->trackByPath(QStringLiteral("/music/new.mp3")));
}

void LibraryReadFailTest::theDeletedRowsCannotBeRead()
{
    // L-READFAIL 2, L-DELETED: the deleted rows old enough to go cannot be read: nothing goes,
    // and it is said once.
    open(Damage::FirstLeaf);
    QSignalSpy failed(library.get(), &Library::failed);
    QCOMPARE(library->expireDeleted(kNow.addDays(400)), 0);
    QVERIFY2(saidOnce(failed), qPrintable(said(failed)));
}

void LibraryReadFailTest::aMoveWhosePathsCannotBeLooked()
{
    // L-READFAIL 2: whether a move has anything to do cannot be found out (the paths cannot be
    // looked up): that is not "nothing to do". The call changes nothing and says so once.
    open(Damage::PathIndex);
    QVERIFY(library->track(kTracks)); // by id, the library still answers
    QSignalSpy failed(library.get(), &Library::failed);
    QCOMPARE(library->recordMoves({ { pathOf(5), QStringLiteral("/music/moved.mp3") } }), 0);
    QVERIFY2(saidOnce(failed), qPrintable(said(failed)));
    QCOMPARE(library->track(6)->path, pathOf(5));
}

void LibraryReadFailTest::aPlayStaysAsItIs()
{
    // L-STATS, L-READFAIL: plays already told a failed look apart from no such track. They still
    // do, on the new readers.
    open(Damage::FirstLeaf);
    QSignalSpy failed(library.get(), &Library::failed);
    QVERIFY(!library->recordPlay(1, kNow));
    QVERIFY2(saidOnce(failed), qPrintable(said(failed)));
    QSignalSpy none(library.get(), &Library::failed);
    QVERIFY(!library->recordPlay(kTracks + 50, kNow)); // no such track: nothing said
    QCOMPARE(none.size(), 0);
}

QTEST_MAIN(LibraryReadFailTest)
#include "tst_library_readfail.moc"
