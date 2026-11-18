// LOCKED ACCEPTANCE TEST (a library written before the reader learnt something: its files are read
// again, once). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason, and
// left alone.
//
// Rules: docs/spec/library.md, L-SCHEMA (version 2 and the step from 1), L-SCAN and L-PARTS (the
// reader's number). A library of schema 1 is made here with the table as version 1 had it, which is
// history and does not change. Whether a file was read is told without a counter: its title is
// changed in place, size and time kept, so that only a reading can see it.
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

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
const QDateTime kThen = QDateTime(QDate(2019, 3, 4), QTime(7, 0), QTimeZone::UTC);

ScanOptions options()
{
    ScanOptions o;
    o.category = QStringLiteral("Music");
    o.now = kNow;
    return o;
}

// One statement after another on a connection of its own; false at the first that fails.
bool sql(const QString &file, const QStringList &statements, QVariant *answer = nullptr)
{
    static int counter = 0;
    const QString name = QStringLiteral("reread-%1").arg(++counter);
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

// The table as schema 1 had it.
const QString kVersionOne = QStringLiteral(
    "CREATE TABLE tracks (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL UNIQUE, "
    "category TEXT NOT NULL, "
    "size_bytes INTEGER NOT NULL, modified_secs INTEGER NOT NULL, date_added_secs INTEGER NOT "
    "NULL, "
    "play_count INTEGER NOT NULL DEFAULT 0, skip_count INTEGER NOT NULL DEFAULT 0, "
    "last_played_secs INTEGER NOT NULL DEFAULT (-1), missing INTEGER NOT NULL DEFAULT 0, "
    "picture_count INTEGER NOT NULL DEFAULT 0, tags_json TEXT NOT NULL)");

// A folder with one FLAC file, and a library of schema 1 that holds it.
struct Old {
    QTemporaryDir dir;
    QString file; // the music file
    QString db;
    qint64 size = 0;
    qint64 modified = 0;
    int pictures = 0;
    QString json; // what this program stores for the file as it is

    Old()
    {
        const QString root = QFileInfo(dir.path()).canonicalFilePath();
        QDir().mkpath(root + QStringLiteral("/music"));
        file = root + QStringLiteral("/music/one.flac");
        db = root + QStringLiteral("/library.db");
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), file))
            QTest::qFail("could not copy the fixture", __FILE__, __LINE__);
        // What is stored for it is taken from a library of this program, so the test knows no
        // layout of it.
        const QString other = root + QStringLiteral("/other.db");
        {
            const auto now = Library::open(other);
            if (!now || now->addFile(file, QStringLiteral("Music"), kNow) <= 0)
                QTest::qFail("could not read the fixture", __FILE__, __LINE__);
        }
        QVariant v;
        sql(other, { QStringLiteral("SELECT tags_json FROM tracks") }, &v);
        json = v.toString();
        sql(other, { QStringLiteral("SELECT size_bytes FROM tracks") }, &v);
        size = v.toLongLong();
        sql(other, { QStringLiteral("SELECT modified_secs FROM tracks") }, &v);
        modified = v.toLongLong();
        sql(other, { QStringLiteral("SELECT picture_count FROM tracks") }, &v);
        pictures = v.toInt();
        if (!json.contains(QStringLiteral("Flac Title")) || size != QFileInfo(file).size())
            QTest::qFail("the fixture is not as this test takes it to be", __FILE__, __LINE__);
    }
    // The schema 1 library, holding the file with this text, size and mark.
    void make(const QString &tags, qint64 sizeHeld, bool missing = false, int picturesHeld = -1)
    {
        QString quoted = tags;
        quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
        QString path = file;
        path.replace(QLatin1Char('\''), QStringLiteral("''"));
        const bool ok = sql(db,
            { kVersionOne, QStringLiteral("CREATE INDEX tracks_by_category ON tracks (category)"),
                QStringLiteral("INSERT INTO tracks (id, path, category, size_bytes, modified_secs, "
                               "date_added_secs, play_count, "
                               "skip_count, last_played_secs, missing, picture_count, tags_json) "
                               "VALUES (41, '%1', 'Music', %2, %3, "
                               "%4, 7, 2, %5, %6, %8, '%7')")
                    .arg(path)
                    .arg(sizeHeld)
                    .arg(modified)
                    .arg(kThen.toSecsSinceEpoch())
                    .arg(kThen.toSecsSinceEpoch() + 60)
                    .arg(missing ? 1 : 0)
                    .arg(quoted)
                    .arg(picturesHeld < 0 ? pictures : picturesHeld),
                QStringLiteral("PRAGMA user_version = 1") });
        if (!ok || userVersion(db) != 1)
            QTest::qFail("could not make the old library", __FILE__, __LINE__);
    }
    QString stale() const
    {
        return QString(json).replace(QStringLiteral("Flac Title"), QStringLiteral("Held Title"));
    }
};

// The file's title is changed where it lies, its size and its time kept: only a reading sees it.
bool retitleInPlace(const QString &path)
{
    const QFileInfo before(path);
    const QDateTime stamp = before.fileTime(QFileDevice::FileModificationTime);
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite))
        return false;
    QByteArray bytes = f.readAll();
    if (bytes.count("Flac Title") != 1)
        return false;
    bytes.replace("Flac Title", "Flac Titel");
    f.seek(0);
    if (f.write(bytes) != bytes.size() || !f.flush()
        || !f.setFileTime(stamp, QFileDevice::FileModificationTime))
        return false;
    f.close();
    const QFileInfo after(path);
    return after.size() == before.size()
        && after.fileTime(QFileDevice::FileModificationTime) == stamp;
}

QString title(const Library &library, qint64 id)
{
    const auto t = library.track(id);
    return t ? t->tags.fields.value(QStringLiteral("Title")).join(QLatin1Char('|'))
             : QStringLiteral("<gone>");
}

}

class LibraryRereadTest : public QObject {
    Q_OBJECT
private slots:
    void anOlderLibraryIsBroughtUp();
    void itsFilesAreReadAgainAndOnlyOnce();
    void whatReadsTheSameIsUnchanged();
    void anotherCountOfPicturesIsAChange();
    void changedAsWellAsOld();
    void markedMissingAndOld();
    void aNewTrackIsNotReadAgain();
    void aTrackReadAgainOnRequestIsNotReadAgain();
    void theLookingAlone();
    void aLibraryInUseIsNotBroughtUpHalf();
};

void LibraryRereadTest::anOlderLibraryIsBroughtUp()
{
    // L-SCHEMA: opened, it is at this version, and holds what it held.
    Old f;
    f.make(f.stale(), f.size);
    QVERIFY(Library::currentSchemaVersion() >= 2);
    for (int time = 0; time < 2; ++time) { // and opening it again runs no step again
        QString error;
        const auto library = Library::open(f.db, &error);
        QVERIFY2(library, qPrintable(error));
        QCOMPARE(userVersion(f.db), Library::currentSchemaVersion());
        QCOMPARE(library->count(), 1);
        const auto t = library->track(41);
        QVERIFY(t);
        QCOMPARE(t->path, f.file);
        QCOMPARE(t->category, s(u"Music"));
        QCOMPARE(t->dateAdded, kThen);
        QCOMPARE(t->playCount, 7);
        QCOMPARE(t->skipCount, 2);
        QCOMPARE(t->lastPlayed, kThen.addSecs(60));
        QCOMPARE(title(*library, 41), s(u"Held Title")); // opening reads no file
    }
}

void LibraryRereadTest::itsFilesAreReadAgainAndOnlyOnce()
{
    // L-SCAN: a file as stored, read by an older reader, is read again.
    Old f;
    f.make(f.stale(), f.size);
    auto library = Library::open(f.db);
    QVERIFY(library);
    QSignalSpy updated(library.get(), &Library::tracksUpdated);
    QSignalSpy added(library.get(), &Library::tracksAdded);
    ScanReport report = library->scan({ QFileInfo(f.file).path() }, options());
    QCOMPARE(report.updated, (QList<qint64> { 41 }));
    QCOMPARE(report.unchanged, 0);
    QCOMPARE(report.added.size(), 0);
    QCOMPARE(added.size(), 0);
    QCOMPARE(updated.size(), 1);
    QCOMPARE(title(*library, 41), s(u"Flac Title"));
    const auto t = library->track(41);
    QCOMPARE(t->dateAdded, kThen); // what only the library knows stays
    QCOMPARE(t->playCount, 7);
    QCOMPARE(t->lastPlayed, kThen.addSecs(60));
    // Once: the next scan does not read it.
    QVERIFY(retitleInPlace(f.file));
    report = library->scan({ QFileInfo(f.file).path() }, options());
    QCOMPARE(report.unchanged, 1);
    QCOMPARE(report.updated.size(), 0);
    QCOMPARE(title(*library, 41), s(u"Flac Title"));
    QCOMPARE(updated.size(), 1);
    // Nor does a scan after the library was closed and opened.
    library.reset();
    const auto again = Library::open(f.db);
    QCOMPARE(again->scan({ QFileInfo(f.file).path() }, options()).unchanged, 1);
    QCOMPARE(title(*again, 41), s(u"Flac Title"));
}

void LibraryRereadTest::whatReadsTheSameIsUnchanged()
{
    // L-PARTS: read again only for the reader's sake, and the reading is what the library holds:
    // unchanged, nobody told, and not read a third time.
    Old f;
    f.make(f.json, f.size);
    const auto library = Library::open(f.db);
    QVERIFY(library);
    QSignalSpy updated(library.get(), &Library::tracksUpdated);
    ScanReport report = library->scan({ QFileInfo(f.file).path() }, options());
    QCOMPARE(report.updated.size(), 0);
    QCOMPARE(report.unchanged, 1);
    QCOMPARE(report.found.size(), 0);
    QCOMPARE(updated.size(), 0);
    QVERIFY(retitleInPlace(f.file));
    report = library->scan({ QFileInfo(f.file).path() }, options());
    QCOMPARE(report.updated.size(), 0);
    QCOMPARE(report.unchanged, 1);
    QCOMPARE(title(*library, 41),
        s(u"Flac Title")); // it was not read: the reader's number was stored the first time
    QCOMPARE(updated.size(), 0);
}

void LibraryRereadTest::anotherCountOfPicturesIsAChange()
{
    // L-SCAN: what is compared is the fields, the properties of the sound and the count of
    // pictures.
    Old f;
    f.make(f.json, f.size, false, f.pictures + 1);
    const auto library = Library::open(f.db);
    QSignalSpy updated(library.get(), &Library::tracksUpdated);
    const ScanReport report = library->scan({ QFileInfo(f.file).path() }, options());
    QCOMPARE(report.updated, (QList<qint64> { 41 }));
    QCOMPARE(report.unchanged, 0);
    QCOMPARE(updated.size(), 1);
    QCOMPARE(library->track(41)->pictureCount, f.pictures);
}

void LibraryRereadTest::changedAsWellAsOld()
{
    // A file that differs is updated as ever, whatever read it before, even when it reads the same.
    Old f;
    f.make(f.json, f.size + 1);
    const auto library = Library::open(f.db);
    const ScanReport report = library->scan({ QFileInfo(f.file).path() }, options());
    QCOMPARE(report.updated, (QList<qint64> { 41 }));
    QCOMPARE(report.unchanged, 0);
    QCOMPARE(library->track(41)->sizeBytes, f.size);
}

void LibraryRereadTest::markedMissingAndOld()
{
    // L-SCAN: marked missing and there: found, read again for the reader's sake, and not counted as
    // updated or unchanged.
    Old f;
    f.make(f.stale(), f.size, true);
    const auto library = Library::open(f.db);
    ScanReport report = library->scan({ QFileInfo(f.file).path() }, options());
    QCOMPARE(report.found, (QList<qint64> { 41 }));
    QCOMPARE(report.updated.size(), 0);
    QCOMPARE(report.unchanged, 0);
    QCOMPARE(title(*library, 41), s(u"Flac Title"));
    QVERIFY(!library->track(41)->missing);
    {
        // And when it reads as held: found, and still not unchanged as well.
        Old g;
        g.make(g.json, g.size, true);
        const auto other = Library::open(g.db);
        const ScanReport same = other->scan({ QFileInfo(g.file).path() }, options());
        QCOMPARE(same.found, (QList<qint64> { 41 }));
        QCOMPARE(same.updated.size(), 0);
        QCOMPARE(same.unchanged, 0);
    }
    QVERIFY(retitleInPlace(f.file));
    report = library->scan({ QFileInfo(f.file).path() }, options());
    QCOMPARE(report.unchanged, 1);
    QCOMPARE(title(*library, 41), s(u"Flac Title"));
}

void LibraryRereadTest::aNewTrackIsNotReadAgain()
{
    // What this program reads carries this reader's number: added by a scan, or added as one file.
    Old f;
    for (int how = 0; how < 2; ++how) {
        const QString db = f.dir.path() + QStringLiteral("/new-%1.db").arg(how);
        const auto library = Library::open(db);
        QVERIFY(library);
        if (how == 0)
            QCOMPARE(library->scan({ QFileInfo(f.file).path() }, options()).added.size(), 1);
        else
            QVERIFY(library->addFile(f.file, QStringLiteral("Music"), kNow) > 0);
        const qint64 id = library->tracks().at(0).id;
        const QString held = title(*library, id);
        if (how == 0)
            QVERIFY(retitleInPlace(f.file));
        else
            QCOMPARE(held, s(u"Flac Titel")); // the file as the first round left it
        const ScanReport report = library->scan({ QFileInfo(f.file).path() }, options());
        QCOMPARE(report.unchanged, 1);
        QCOMPARE(report.updated.size(), 0);
        QCOMPARE(title(*library, id), held);
        QCOMPARE(library->scanKnown().byPath.value(f.file).reader, kReaderVersion);
    }
}

void LibraryRereadTest::aTrackReadAgainOnRequestIsNotReadAgain()
{
    // L-EDIT refresh reads the file now; what it stores carries this reader's number too.
    Old f;
    f.make(f.stale(), f.size);
    const auto library = Library::open(f.db);
    QVERIFY(library->scanKnown().byPath.value(f.file).reader < kReaderVersion);
    QVERIFY(library->refresh(41));
    QCOMPARE(title(*library, 41), s(u"Flac Title"));
    QCOMPARE(library->scanKnown().byPath.value(f.file).reader, kReaderVersion);
    QVERIFY(retitleInPlace(f.file));
    QCOMPARE(library->scan({ QFileInfo(f.file).path() }, options()).unchanged, 1);
    QCOMPARE(title(*library, 41), s(u"Flac Title"));
}

void LibraryRereadTest::theLookingAlone()
{
    // L-PARTS: the looking is told the reader's number of each track and says why it read.
    Old f;
    ScanKnown known;
    ScanKnown::Entry entry; // made by hand, it is as if read by this reader
    entry.id = 5;
    entry.sizeBytes = f.size;
    entry.modified = QDateTime::fromSecsSinceEpoch(f.modified, QTimeZone::UTC);
    QCOMPARE(entry.reader, kReaderVersion);
    known.byPath.insert(f.file, entry);
    const QStringList folders { QFileInfo(f.file).path() };
    ScanFindings found = findForScan(folders, known);
    QCOMPARE(found.unchanged, 1);
    QCOMPARE(found.reread.size(), 0);

    known.byPath[f.file].reader = kReaderVersion - 1;
    found = findForScan(folders, known);
    QCOMPARE(found.unchanged, 0);
    QCOMPARE(found.reread.size(), 1);
    QCOMPARE(found.reread.at(0).id, 5);
    QVERIFY(found.reread.at(0).onlyTheReader);
    QVERIFY(found.judged.contains(5));

    known.byPath[f.file].reader
        = kReaderVersion + 1; // read by a later program: not ours to read again
    QCOMPARE(findForScan(folders, known).unchanged, 1);

    known.byPath[f.file].reader = kReaderVersion - 1;
    known.byPath[f.file].sizeBytes = f.size + 1; // it differs as well: read for that, as ever
    found = findForScan(folders, known);
    QCOMPARE(found.reread.size(), 1);
    QVERIFY(!found.reread.at(0).onlyTheReader);
}

void LibraryRereadTest::aLibraryInUseIsNotBroughtUpHalf()
{
    // L-SCHEMA: the step runs under the write lock. While another has it, the library does not
    // open, and is left at the version it had.
    Old f;
    f.make(f.stale(), f.size);
    {
        QSqlDatabase db
            = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("holder"));
        db.setDatabaseName(f.db);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("BEGIN IMMEDIATE")));
        QString error;
        QVERIFY(!Library::open(f.db, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(q.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        q.finish();
        QVERIFY(q.exec(QStringLiteral("ROLLBACK")));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("holder"));
    const auto library = Library::open(f.db);
    QVERIFY(library);
    QCOMPARE(userVersion(f.db), Library::currentSchemaVersion());
    QCOMPARE(title(*library, 41), s(u"Held Title"));
}

QTEST_MAIN(LibraryRereadTest)
#include "tst_library_reread.moc"
