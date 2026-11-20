// LOCKED ACCEPTANCE TEST (the library: a deleted track's row is kept for thirty days).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/library.md, L-DELETED, and L-SCHEMA (version 3). The owner's ruling: a file
// sent to the trash takes its track out of every list at once; the row is kept, so that a
// restore from the trash and a scan bring the track back with its plays, its date added and its
// id; after thirty days the row is removed.
//
// What the library keeps of a deleted track is asked of the file itself here (how many rows,
// and whether `deleted_secs` is set), because the library hands such a track out to no one.
// The music files are copies of fixtures in a temporary folder.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "library/library.h"

using namespace linuxbee;

namespace {

const QDateTime kThen(QDate(2026, 11, 1), QTime(12, 0, 0), QTimeZone::UTC);
const qint64 kDay = 24 * 60 * 60;

// One statement after another on a connection of its own; false at the first that fails.
bool sql(const QString &file, const QStringList &statements, QVariant *answer = nullptr)
{
    static int counter = 0;
    const QString name = QStringLiteral("copy-%1").arg(++counter);
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(file);
        ok = db.open();
        QSqlQuery q(db);
        for (const QString &statement : statements) {
            if (ok && !q.exec(statement)) {
                qWarning(
                    "%s: %s", qPrintable(statement.left(60)), qPrintable(q.lastError().text()));
                ok = false;
            }
        }
        if (ok && answer && q.next())
            *answer = q.value(0);
        q.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

int userVersion(const QString &file)
{
    QVariant v;
    return sql(file, { QStringLiteral("PRAGMA user_version") }, &v) ? v.toInt() : -1;
}

qint64 number(const QString &file, const QString &statement)
{
    QVariant v;
    return sql(file, { statement }, &v) ? v.toLongLong() : -99;
}

// Rows in the file, deleted ones too.
qint64 rowsIn(const QString &file)
{
    return number(file, QStringLiteral("SELECT count(*) FROM tracks"));
}

// Rows in the file that are deleted.
qint64 deletedIn(const QString &file)
{
    return number(file, QStringLiteral("SELECT count(*) FROM tracks WHERE deleted_secs >= 0"));
}

QList<qint64> idsOf(const QSignalSpy &spy, int call)
{
    return spy.value(call).value(0).value<QList<qint64>>();
}

// A folder with three music files, and a library that has scanned them. The first has plays.
struct Rig {
    QTemporaryDir dir;
    QString root, music, db, a, b, c;
    std::unique_ptr<Library> library;
    qint64 idA = 0, idB = 0, idC = 0;

    Rig()
    {
        root = QFileInfo(dir.path()).canonicalFilePath();
        music = root + QStringLiteral("/music");
        QDir().mkpath(music);
        db = root + QStringLiteral("/library.db");
        a = music + QStringLiteral("/a.mp3");
        b = music + QStringLiteral("/b.flac");
        c = music + QStringLiteral("/c.ogg");
        bool ok = QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), a)
            && QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), b)
            && QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.ogg"), c);
        library = Library::open(db);
        ok = ok && library && scan(kThen).added.size() == 3;
        if (ok) {
            idA = library->trackByPath(a).value_or(LibraryTrack()).id;
            idB = library->trackByPath(b).value_or(LibraryTrack()).id;
            idC = library->trackByPath(c).value_or(LibraryTrack()).id;
            ok = idA > 0 && idB > 0 && idC > 0 && library->recordPlay(idA, kThen.addSecs(60))
                && library->recordPlay(idA, kThen.addSecs(120));
        }
        if (!ok)
            QTest::qFail("the rig could not be made", __FILE__, __LINE__);
    }
    ScanReport scan(const QDateTime &now)
    {
        ScanOptions options;
        options.now = now;
        return library->scan({ music }, options);
    }
    void reopen()
    {
        library.reset();
        library = Library::open(db);
        if (!library)
            QTest::qFail("the library did not open again", __FILE__, __LINE__);
    }
};

}

class LibraryDeletedTest : public QObject {
    Q_OBJECT
private slots:
    void theSchemaIsThree();
    void aLibraryOfSchemaTwoIsBroughtUp();
    void aDeletedTrackIsNoTrack();
    void oneChangeAndOneSignalForMany();
    void whatIsNoTrackIsNotDeleted();
    void theRowIsKeptAcrossOpenings();
    void aScanThatFindsTheFileBringsTheTrackBack();
    void aScanThatDoesNotFindItLeavesItDeleted();
    void addingTheFileBringsTheTrackBack();
    void anAlikeFileElsewhereIsANewTrack();
    void aTrackMovedToThePathTakesIt_data();
    void aTrackMovedToThePathTakesIt();
    void afterThirtyDaysTheRowGoes_data();
    void afterThirtyDaysTheRowGoes();
    void aRowWhoseFileIsThereIsNotRemoved();
    void aScanInPartsBringsBackWhatALaterPartHolds();
    void anOpeningRemovesTheOldOnes();
};

void LibraryDeletedTest::theSchemaIsThree()
{
    QCOMPARE(Library::currentSchemaVersion(), 3);
    QCOMPARE(Library::deletedKeptDays(), 30);
}

void LibraryDeletedTest::aLibraryOfSchemaTwoIsBroughtUp()
{
    // L-SCHEMA, version 3: the step from 2 gives every track "not deleted" and changes
    // nothing else. The copy made before it has the name of the version found.
    QTemporaryDir dir;
    const QString db = QFileInfo(dir.path()).canonicalFilePath() + QStringLiteral("/library.db");
    QVERIFY(sql(db,
        { QStringLiteral(
              "CREATE TABLE tracks (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL "
              "UNIQUE, category TEXT NOT NULL, size_bytes INTEGER NOT NULL, modified_secs "
              "INTEGER NOT NULL, date_added_secs INTEGER NOT NULL, play_count INTEGER NOT NULL "
              "DEFAULT 0, skip_count INTEGER NOT NULL DEFAULT 0, last_played_secs INTEGER NOT "
              "NULL DEFAULT (-1), missing INTEGER NOT NULL DEFAULT 0, picture_count INTEGER NOT "
              "NULL DEFAULT 0, tags_json TEXT NOT NULL, reader INTEGER NOT NULL DEFAULT 0)"),
            QStringLiteral("CREATE INDEX tracks_by_category ON tracks (category)"),
            QStringLiteral(
                "INSERT INTO tracks (id, path, category, size_bytes, modified_secs, "
                "date_added_secs, play_count, missing, tags_json, reader) VALUES "
                "(7, '/m/one.flac', 'Music', 10, 1700000000, 1700000100, 4, 1, '{}', 2)"),
            QStringLiteral(
                "INSERT INTO tracks (id, path, category, size_bytes, modified_secs, "
                "date_added_secs, play_count, missing, tags_json, reader) VALUES "
                "(9, '/m/two.flac', 'Inbox', 11, 1700000000, 1700000100, 5, 0, '{}', 2)"),
            QStringLiteral("PRAGMA user_version = 2") }));
    {
        QString error;
        const auto library = Library::open(db, &error);
        QVERIFY2(library, qPrintable(error));
        QCOMPARE(library->stepAtOpening().from, 2);
        QCOMPARE(library->stepAtOpening().to, 3);
        QCOMPARE(library->stepAtOpening().copyPath, db + QStringLiteral(".schema-2"));
        QCOMPARE(library->count(), 2);
        const LibraryTrack one = library->track(7).value_or(LibraryTrack());
        QCOMPARE(one.path, QStringLiteral("/m/one.flac"));
        QCOMPARE(one.playCount, 4);
        QVERIFY(one.missing);
        QCOMPARE(library->track(9).value_or(LibraryTrack()).category, QStringLiteral("Inbox"));
    }
    QCOMPARE(userVersion(db), 3);
    QCOMPARE(deletedIn(db), 0);
    QCOMPARE(userVersion(db + QStringLiteral(".schema-2")), 2);
}

void LibraryDeletedTest::aDeletedTrackIsNoTrack()
{
    // L-DELETED: it leaves every list and every count at once, and every call that names it
    // answers as for an id the library does not know. The row is in the file.
    Rig f;
    QSignalSpy removed(f.library.get(), &Library::tracksRemoved);
    QSignalSpy updated(f.library.get(), &Library::tracksUpdated);
    QCOMPARE(f.library->markDeleted({ f.idA }, kThen.addSecs(600)), 1);
    QCOMPARE(removed.size(), 1);
    QCOMPARE(idsOf(removed, 0), QList<qint64>({ f.idA }));
    QCOMPARE(updated.size(), 0);

    QCOMPARE(f.library->count(), 2);
    QCOMPARE(f.library->categoryCounts().value(QStringLiteral("Music")), 2);
    QCOMPARE(f.library->tracks().size(), 2);
    QCOMPARE(f.library->tracks(QStringLiteral("Music")).size(), 2);
    QVERIFY(!f.library->track(f.idA));
    QVERIFY(!f.library->trackByPath(f.a));

    QCOMPARE(f.library->remove({ f.idA }), 0);
    QCOMPARE(f.library->setCategory({ f.idA }, QStringLiteral("Inbox")), 0);
    QVERIFY(!f.library->refresh(f.idA));
    QVERIFY(!f.library->recordPlay(f.idA, kThen.addSecs(700)));
    QVERIFY(!f.library->recordSkip(f.idA, kThen.addSecs(700)));
    QCOMPARE(removed.size(), 1);
    QCOMPARE(updated.size(), 0);

    QCOMPARE(rowsIn(f.db), 3);
    QCOMPARE(deletedIn(f.db), 1);
    QCOMPARE(
        number(f.db, QStringLiteral("SELECT deleted_secs FROM tracks WHERE id = %1").arg(f.idA)),
        kThen.addSecs(600).toSecsSinceEpoch());
    QCOMPARE(number(f.db, QStringLiteral("SELECT play_count FROM tracks WHERE id = %1").arg(f.idA)),
        qint64(2));
    QCOMPARE(number(f.db, QStringLiteral("SELECT missing FROM tracks WHERE id = %1").arg(f.idA)),
        qint64(1));
}

void LibraryDeletedTest::oneChangeAndOneSignalForMany()
{
    // L-DELETED: many tracks are one change and one signal, with the ids in the order given.
    Rig f;
    QSignalSpy removed(f.library.get(), &Library::tracksRemoved);
    QCOMPARE(f.library->markDeleted({ f.idC, f.idA, f.idB }, kThen.addSecs(600)), 3);
    QCOMPARE(removed.size(), 1);
    QCOMPARE(idsOf(removed, 0), QList<qint64>({ f.idC, f.idA, f.idB }));
    QCOMPARE(f.library->count(), 0);
    QCOMPARE(rowsIn(f.db), 3);
    QCOMPARE(deletedIn(f.db), 3);
}

void LibraryDeletedTest::whatIsNoTrackIsNotDeleted()
{
    // L-DELETED: an id that is unknown, or deleted already, is not counted and not named, and
    // the time of an earlier deleting stands.
    Rig f;
    QCOMPARE(f.library->markDeleted({ f.idA }, kThen.addSecs(600)), 1);
    QSignalSpy removed(f.library.get(), &Library::tracksRemoved);
    QCOMPARE(f.library->markDeleted({ f.idA, 9999 }, kThen.addSecs(900)), 0);
    QCOMPARE(removed.size(), 0);
    QCOMPARE(f.library->markDeleted({ f.idA, f.idB, 9999 }, kThen.addSecs(900)), 1);
    QCOMPARE(removed.size(), 1);
    QCOMPARE(idsOf(removed, 0), QList<qint64>({ f.idB }));
    QCOMPARE(
        number(f.db, QStringLiteral("SELECT deleted_secs FROM tracks WHERE id = %1").arg(f.idA)),
        kThen.addSecs(600).toSecsSinceEpoch());
    QCOMPARE(f.library->markDeleted({ }, kThen), 0);
    QCOMPARE(f.library->markDeleted({ f.idC }, QDateTime()), 0); // no time, nothing deleted
    // A time before 1970 is no time either.
    const QDateTime early(QDate(1969, 12, 31), QTime(23, 0, 0), QTimeZone::UTC);
    QCOMPARE(f.library->markDeleted({ f.idC }, early), 0);
    QCOMPARE(f.library->count(), 1);
    // An id given twice is one track: counted once, named once, at its first place.
    Rig g;
    QSignalSpy twice(g.library.get(), &Library::tracksRemoved);
    QCOMPARE(g.library->markDeleted({ g.idB, g.idA, g.idB, g.idA }, kThen.addSecs(600)), 2);
    QCOMPARE(twice.size(), 1);
    QCOMPARE(idsOf(twice, 0), QList<qint64>({ g.idB, g.idA }));
}

void LibraryDeletedTest::theRowIsKeptAcrossOpenings()
{
    Rig f;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QCOMPARE(f.library->markDeleted({ f.idA }, now), 1);
    f.reopen();
    QCOMPARE(f.library->count(), 2);
    QVERIFY(!f.library->track(f.idA));
    QCOMPARE(deletedIn(f.db), 1);
}

void LibraryDeletedTest::aScanThatFindsTheFileBringsTheTrackBack()
{
    // L-DELETED: the file is at its path again (restored from the trash). The scan brings
    // the track back: the same id, its plays, its date added. It is named as added, since
    // every list had let it go, and counted as found.
    Rig f;
    const QString away = f.root + QStringLiteral("/trash-a.mp3");
    QVERIFY(QFile::rename(f.a, away));
    QCOMPARE(f.library->markDeleted({ f.idA }, kThen.addSecs(600)), 1);
    QVERIFY(QFile::rename(away, f.a));
    QSignalSpy added(f.library.get(), &Library::tracksAdded);
    QSignalSpy updated(f.library.get(), &Library::tracksUpdated);
    const ScanReport report = f.scan(kThen.addSecs(5 * kDay));
    QCOMPARE(report.found, QList<qint64>({ f.idA }));
    QCOMPARE(report.added, QList<qint64>());
    QCOMPARE(report.unchanged, 2);
    QCOMPARE(added.size(), 1);
    QCOMPARE(idsOf(added, 0), QList<qint64>({ f.idA }));
    QCOMPARE(updated.size(), 0);
    const LibraryTrack back = f.library->track(f.idA).value_or(LibraryTrack());
    QCOMPARE(back.id, f.idA);
    QCOMPARE(back.path, f.a);
    QVERIFY(!back.missing);
    QCOMPARE(back.playCount, 2);
    QCOMPARE(back.dateAdded, kThen);
    QCOMPARE(f.library->count(), 3);
    QCOMPARE(deletedIn(f.db), 0);
    QCOMPARE(rowsIn(f.db), 3);
}

void LibraryDeletedTest::aScanThatDoesNotFindItLeavesItDeleted()
{
    // L-DELETED: with the file still gone a scan says nothing of the track: it is not
    // `nowMissing` (it is no track), not found, and nothing is signalled.
    Rig f;
    QVERIFY(QFile::remove(f.a));
    QCOMPARE(f.library->markDeleted({ f.idA }, kThen.addSecs(600)), 1);
    QSignalSpy added(f.library.get(), &Library::tracksAdded);
    QSignalSpy updated(f.library.get(), &Library::tracksUpdated);
    QSignalSpy removed(f.library.get(), &Library::tracksRemoved);
    const ScanReport report = f.scan(kThen.addSecs(5 * kDay));
    QCOMPARE(report.found, QList<qint64>());
    QCOMPARE(report.nowMissing, QList<qint64>());
    QCOMPARE(report.added, QList<qint64>());
    QCOMPARE(report.unchanged, 2);
    QCOMPARE(added.size() + updated.size() + removed.size(), 0);
    QCOMPARE(deletedIn(f.db), 1);
}

void LibraryDeletedTest::addingTheFileBringsTheTrackBack()
{
    // L-DELETED: `addFile` of the path of a deleted track brings that track back; it makes
    // no second row for the path.
    Rig f;
    QCOMPARE(f.library->markDeleted({ f.idA }, kThen.addSecs(600)), 1);
    QSignalSpy added(f.library.get(), &Library::tracksAdded);
    QCOMPARE(f.library->addFile(f.a, QStringLiteral("Inbox"), kThen.addSecs(5 * kDay)), f.idA);
    QCOMPARE(added.size(), 1);
    QCOMPARE(idsOf(added, 0), QList<qint64>({ f.idA }));
    const LibraryTrack back = f.library->track(f.idA).value_or(LibraryTrack());
    QCOMPARE(back.playCount, 2);
    QCOMPARE(back.dateAdded, kThen);
    QCOMPARE(back.category, QStringLiteral("Music")); // as it was, not as asked now
    QCOMPARE(rowsIn(f.db), 3);
    QCOMPARE(deletedIn(f.db), 0);
}

void LibraryDeletedTest::anAlikeFileElsewhereIsANewTrack()
{
    // L-DELETED: a deleted track is not followed (L-FOLLOW). A file alike in size, time and
    // tags that turns up elsewhere is a new track, and the deleted row waits for its own.
    Rig f;
    const QString other = f.music + QStringLiteral("/kept/a.mp3");
    QVERIFY(QDir().mkpath(f.music + QStringLiteral("/kept")));
    QCOMPARE(f.library->markDeleted({ f.idA }, kThen.addSecs(600)), 1);
    QVERIFY(QFile::rename(f.a, other)); // the same bytes and the same time, at another place
    const ScanReport report = f.scan(kThen.addSecs(5 * kDay));
    QCOMPARE(report.followed, QList<qint64>());
    QCOMPARE(report.found, QList<qint64>());
    QCOMPARE(report.added.size(), 1);
    QVERIFY(report.added.value(0) != f.idA);
    const LibraryTrack fresh = f.library->trackByPath(other).value_or(LibraryTrack());
    QCOMPARE(fresh.playCount, 0);
    QCOMPARE(fresh.dateAdded, kThen.addSecs(5 * kDay));
    QCOMPARE(deletedIn(f.db), 1);
    QCOMPARE(rowsIn(f.db), 4);
}

void LibraryDeletedTest::aTrackMovedToThePathTakesIt_data()
{
    QTest::addColumn<int>("how");
    QTest::newRow("setPath") << 0;
    QTest::newRow("recordMoves") << 1;
    QTest::newRow("relinkPaths") << 2;
}

void LibraryDeletedTest::aTrackMovedToThePathTakesIt()
{
    // L-DELETED: the path of a deleted track is free. A track whose file is moved there takes
    // it, and the deleted row, which would be in its way, is removed in the same change.
    QFETCH(int, how);
    Rig f;
    const QString from = f.music + QStringLiteral("/sub/a.mp3");
    const QString to = f.root + QStringLiteral("/else/a.mp3");
    QVERIFY(QDir().mkpath(f.music + QStringLiteral("/sub")));
    QVERIFY(QDir().mkpath(f.root + QStringLiteral("/else")));
    // A track at `to`, deleted; and another at `from`, whose file then goes to `to`.
    QVERIFY(QFile::copy(f.a, to));
    const qint64 gone = f.library->addFile(to, QStringLiteral("Music"), kThen);
    QVERIFY(gone > 0);
    QVERIFY(QFile::remove(to));
    QCOMPARE(f.library->markDeleted({ gone }, kThen.addSecs(600)), 1);
    QVERIFY(QFile::copy(f.a, from));
    const qint64 moved = f.library->addFile(from, QStringLiteral("Music"), kThen);
    QVERIFY(moved > 0 && moved != gone);
    const qint64 before = rowsIn(f.db);
    QVERIFY(QFile::rename(from, to));
    if (how == 0)
        QVERIFY(f.library->setPath(moved, to));
    else if (how == 1)
        QCOMPARE(f.library->recordMoves({ FileMove { from, to } }), 1);
    else
        QCOMPARE(f.library->relinkPaths(
                     f.music + QStringLiteral("/sub"), f.root + QStringLiteral("/else")),
            1);
    const LibraryTrack now = f.library->trackByPath(to).value_or(LibraryTrack());
    QCOMPARE(now.id, moved);
    QVERIFY(!now.missing);
    QCOMPARE(rowsIn(f.db), before - 1);
    QCOMPARE(deletedIn(f.db), 0);
}

void LibraryDeletedTest::afterThirtyDaysTheRowGoes_data()
{
    QTest::addColumn<qint64>("later");
    QTest::addColumn<int>("goes");
    QTest::newRow("a day later") << kDay << 0;
    QTest::newRow("a second short of thirty days") << 30 * kDay - 1 << 0;
    QTest::newRow("thirty days") << 30 * kDay << 1;
    QTest::newRow("a year") << 365 * kDay << 1;
    QTest::newRow("the clock set back") << -kDay << 0;
}

void LibraryDeletedTest::afterThirtyDaysTheRowGoes()
{
    // L-DELETED: `expireDeleted(now)` removes the rows deleted thirty days before `now` or
    // longer ago. Nothing is signalled: they were no tracks. No other row is touched.
    QFETCH(qint64, later);
    QFETCH(int, goes);
    Rig f;
    QVERIFY(QFile::remove(f.a));
    const QDateTime when = kThen.addSecs(600);
    QCOMPARE(f.library->markDeleted({ f.idA }, when), 1);
    QSignalSpy removed(f.library.get(), &Library::tracksRemoved);
    QCOMPARE(f.library->expireDeleted(when.addSecs(later)), goes);
    QCOMPARE(removed.size(), 0);
    QCOMPARE(rowsIn(f.db), qint64(3 - goes));
    QCOMPARE(f.library->count(), 2);
    QCOMPARE(f.library->expireDeleted(QDateTime()), 0); // no time, nothing removed
    // A scan does the same with its own time.
    Rig g;
    QVERIFY(QFile::remove(g.a));
    QCOMPARE(g.library->markDeleted({ g.idA }, when), 1);
    g.scan(when.addSecs(later));
    QCOMPARE(rowsIn(g.db), qint64(3 - goes));
}

void LibraryDeletedTest::aRowWhoseFileIsThereIsNotRemoved()
{
    // L-DELETED: a row at whose path a file is, is never removed for its age: that file is
    // the track's, taken back out of the trash, and the next scan brings the track back.
    Rig f;
    QCOMPARE(f.library->markDeleted({ f.idA, f.idB }, kThen.addSecs(600)), 2);
    QVERIFY(QFile::remove(f.b));
    QCOMPARE(f.library->expireDeleted(kThen.addSecs(400 * kDay)), 1); // b alone
    QCOMPARE(rowsIn(f.db), 2);
    QCOMPARE(deletedIn(f.db), 1);
    const ScanReport report = f.scan(kThen.addSecs(400 * kDay));
    QCOMPARE(report.found, QList<qint64>({ f.idA }));
    QCOMPARE(f.library->count(), 2);
    QCOMPARE(f.library->track(f.idA).value_or(LibraryTrack()).playCount, 2);
    QCOMPARE(deletedIn(f.db), 0);
}

void LibraryDeletedTest::aScanInPartsBringsBackWhatALaterPartHolds()
{
    // L-DELETED: the window scans in parts, and each part is stored by itself. A track
    // deleted long ago whose file is in the last part is still there to be brought back when
    // that part comes. (Found by the proving of this rule: the first part had removed it.)
    Rig f;
    QCOMPARE(f.library->markDeleted({ f.idC }, kThen.addSecs(600)), 1); // c.ogg: the last path
    ScanOptions options;
    options.now = kThen.addSecs(400 * kDay);
    QList<ScanFindings> parts;
    ScanFindOptions find;
    find.partSize = 1;
    find.part = [&parts](const ScanFindings &part) { parts.append(part); };
    const ScanFindings rest = findForScan({ f.music }, f.library->scanKnown(), find);
    QVERIFY(parts.size() >= 2);
    ScanReport all;
    for (const ScanFindings &part : std::as_const(parts)) {
        all = addedUp(all, f.library->applyScan(part, options));
        QCOMPARE(rowsIn(f.db), 3); // no part removed it
    }
    all = addedUp(all, f.library->applyScan(rest, options));
    QCOMPARE(all.found, QList<qint64>({ f.idC }));
    QCOMPARE(all.added, QList<qint64>());
    QCOMPARE(f.library->count(), 3);
    QCOMPARE(f.library->track(f.idC).value_or(LibraryTrack()).id, f.idC);
    QCOMPARE(deletedIn(f.db), 0);
}

void LibraryDeletedTest::anOpeningRemovesTheOldOnes()
{
    // L-DELETED: an opening removes what was deleted thirty days before the clock's time and
    // has no file at its path, and keeps the rest.
    Rig f;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QCOMPARE(f.library->markDeleted({ f.idA }, now.addSecs(-31 * kDay)), 1);
    QCOMPARE(f.library->markDeleted({ f.idB }, now.addSecs(-29 * kDay)), 1);
    QCOMPARE(f.library->markDeleted({ f.idC }, now.addSecs(-31 * kDay)), 1);
    QVERIFY(QFile::remove(f.a));
    QVERIFY(QFile::remove(f.b));
    QCOMPARE(rowsIn(f.db), 3);
    f.reopen();
    // a is old and its file is gone: removed. b is not old. c is old and its file is there.
    QCOMPARE(rowsIn(f.db), 2);
    QCOMPARE(deletedIn(f.db), 2);
    QCOMPARE(number(f.db, QStringLiteral("SELECT count(*) FROM tracks WHERE id = %1").arg(f.idA)),
        qint64(0));
    QCOMPARE(f.library->count(), 0);
}

QTEST_GUILESS_MAIN(LibraryDeletedTest)
#include "tst_library_deleted.moc"
