// LOCKED ACCEPTANCE TEST (the library: a copy is made before a schema step runs).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/library.md, L-SCHEMA ("A copy before a step"). A step changes the file for
// good, and an older program then refuses it; so the library as it was is kept beside it.
//
// The older libraries are made here with the tables as versions 1 and 2 had them, which is
// history and does not change. Every case that needs a step has a row for each of those
// versions that is below this program's: while this program writes version 2 that is the step
// from 1; with the first step from 2 the rows from 2 come by themselves.
// MEASURED (SQLite 3.54, 2026-11-19): `VACUUM INTO` runs on a second connection while the
// first holds the write lock, takes what is in the write-ahead log along, keeps `user_version`,
// refuses a file that is there, and leaves an empty file when it runs out of room.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <csignal>
#include <sys/resource.h>

#include "library/library.h"

using namespace linuxbee;

namespace {

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

// Every track of the file as one text: id, path and plays.
QString rowsOf(const QString &file)
{
    QVariant v;
    const bool ok = sql(file,
        { QStringLiteral("SELECT count(*) || ' rows: ' || group_concat(id || ':' || path || ':' "
                         "|| play_count, '|') FROM (SELECT * FROM tracks ORDER BY id)") },
        &v);
    return ok ? v.toString() : QStringLiteral("not read");
}

bool hasColumn(const QString &file, const QString &column)
{
    QVariant v;
    return sql(file,
               { QStringLiteral(
                   "SELECT count(*) FROM pragma_table_info('tracks') WHERE name = '%1'")
                       .arg(column) },
               &v)
        && v.toInt() == 1;
}

// The table as schema 1 had it; schema 2 has one column more.
QString tableOf(int version)
{
    QString table = QStringLiteral(
        "CREATE TABLE tracks (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL UNIQUE, "
        "category TEXT NOT NULL, size_bytes INTEGER NOT NULL, modified_secs INTEGER NOT NULL, "
        "date_added_secs INTEGER NOT NULL, play_count INTEGER NOT NULL DEFAULT 0, "
        "skip_count INTEGER NOT NULL DEFAULT 0, last_played_secs INTEGER NOT NULL DEFAULT (-1), "
        "missing INTEGER NOT NULL DEFAULT 0, picture_count INTEGER NOT NULL DEFAULT 0, "
        "tags_json TEXT NOT NULL");
    if (version >= 2)
        table += QStringLiteral(", reader INTEGER NOT NULL DEFAULT 0");
    return table + QLatin1Char(')');
}

// A library of that version with that many tracks. No music file is behind them: no case
// here reads one.
bool makeOld(const QString &file, int version, int tracks)
{
    QStringList statements { tableOf(version),
        QStringLiteral("CREATE INDEX tracks_by_category ON tracks (category)"),
        QStringLiteral("BEGIN") };
    const QString filler(200, QLatin1Char('x'));
    for (int i = 1; i <= tracks; ++i) {
        statements += QStringLiteral(
            "INSERT INTO tracks (id, path, category, size_bytes, modified_secs, "
            "date_added_secs, play_count, tags_json) VALUES (%1, '/music/%2/track %1.flac', "
            "'Music', %3, 1700000000, 1700000100, %4, '{}')")
                          .arg(i)
                          .arg(filler)
                          .arg(1000 + i)
                          .arg(i % 7);
    }
    statements += QStringLiteral("COMMIT");
    statements += QStringLiteral("PRAGMA user_version = %1").arg(version);
    return sql(file, statements) && userVersion(file) == version;
}

// What lies in the folder, but for the library's own log and its index.
QStringList beside(const QString &db)
{
    const QFileInfo info(db);
    QStringList names
        = QDir(info.absolutePath())
              .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                  QDir::Name);
    names.removeAll(info.fileName() + QStringLiteral("-wal"));
    names.removeAll(info.fileName() + QStringLiteral("-shm"));
    return names;
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("not read");
}

// One row for each older version that this program has a step from.
void olderVersions()
{
    QTest::addColumn<int>("version");
    for (const int version : { 1, 2 }) {
        if (version < Library::currentSchemaVersion())
            QTest::addRow("from schema %d", version) << version;
    }
}

// For as long as it lives no file this process writes may pass `bytes`; a write past it fails.
class NoRoomPast {
public:
    explicit NoRoomPast(qint64 bytes)
    {
        m_signal = ::signal(SIGXFSZ, SIG_IGN);
        ::getrlimit(RLIMIT_FSIZE, &m_before);
        struct rlimit now = m_before;
        now.rlim_cur = rlim_t(bytes);
        m_set = ::setrlimit(RLIMIT_FSIZE, &now) == 0;
    }
    ~NoRoomPast()
    {
        ::setrlimit(RLIMIT_FSIZE, &m_before);
        ::signal(SIGXFSZ, m_signal);
    }
    bool isSet() const { return m_set; }

private:
    struct rlimit m_before { };
    void (*m_signal)(int) = SIG_DFL;
    bool m_set = false;
};

struct Folder {
    QTemporaryDir dir;
    QString root;
    QString db;
    explicit Folder(const QString &sub = QString())
    {
        root = QFileInfo(dir.path()).canonicalFilePath();
        if (!sub.isEmpty()) {
            root += QLatin1Char('/') + sub;
            QDir().mkpath(root);
        }
        db = root + QStringLiteral("/library.db");
    }
    QString copy(int version, int number = 1) const
    {
        const QString name = db + QStringLiteral(".schema-%1").arg(version);
        return number == 1 ? name : name + QStringLiteral("-%1").arg(number);
    }
};

}

class LibraryCopyBeforeStepTest : public QObject {
    Q_OBJECT
private slots:
    void thisProgramHasAStepFromOne();
    void anOlderLibraryIsCopiedFirst_data();
    void anOlderLibraryIsCopiedFirst();
    void whatIsInTheLogIsInTheCopy_data();
    void whatIsInTheLogIsInTheCopy();
    void openedAgainThereIsNoSecondCopy_data();
    void openedAgainThereIsNoSecondCopy();
    void aNewLibraryHasNoCopy();
    void aNameThatIsTakenIsLeft_data();
    void aNameThatIsTakenIsLeft();
    void aFolderWithAQuoteInItsName_data();
    void aFolderWithAQuoteInItsName();
    void noCopyNoStep_data();
    void noCopyNoStep();
    void aStepThatCannotRunLeavesNoCopy_data();
    void aStepThatCannotRunLeavesNoCopy();
    void theLineOfNotice();
};

void LibraryCopyBeforeStepTest::thisProgramHasAStepFromOne()
{
    // The rows of the cases below are made from this. Without one they would hold nothing.
    QVERIFY(Library::currentSchemaVersion() >= 2);
}

void LibraryCopyBeforeStepTest::anOlderLibraryIsCopiedFirst_data()
{
    olderVersions();
}

void LibraryCopyBeforeStepTest::anOlderLibraryIsCopiedFirst()
{
    // L-SCHEMA: the copy lies beside the library under the library's name and the version it
    // had, and is the library as it was: that version, those tracks, that table.
    QFETCH(int, version);
    const Folder f;
    QVERIFY(makeOld(f.db, version, 40));
    const QString before = rowsOf(f.db);
    QVERIFY(before.startsWith(QStringLiteral("40 rows: ")));
    {
        QString error;
        const auto library = Library::open(f.db, &error);
        QVERIFY2(library, qPrintable(error));
        const SchemaStep step = library->stepAtOpening();
        QCOMPARE(step.from, version);
        QCOMPARE(step.to, Library::currentSchemaVersion());
        QCOMPARE(step.copyPath, f.copy(version));
    }
    QCOMPARE(beside(f.db),
        QStringList(
            { QStringLiteral("library.db"), QStringLiteral("library.db.schema-%1").arg(version) }));
    QVERIFY(QFileInfo(f.copy(version)).isFile());
    QCOMPARE(userVersion(f.copy(version)), version);
    QCOMPARE(rowsOf(f.copy(version)), before);
    QCOMPARE(hasColumn(f.copy(version), QStringLiteral("reader")), version >= 2);
    // And the library itself was brought up, with what it held.
    QCOMPARE(userVersion(f.db), Library::currentSchemaVersion());
    QCOMPARE(rowsOf(f.db), before);
}

void LibraryCopyBeforeStepTest::whatIsInTheLogIsInTheCopy_data()
{
    olderVersions();
}

void LibraryCopyBeforeStepTest::whatIsInTheLogIsInTheCopy()
{
    // L-SCHEMA: the copy holds everything committed, also what is still in the write-ahead
    // log and not in the library's own file. Another program has the file open here, so the
    // log is not folded in.
    QFETCH(int, version);
    const Folder f;
    QVERIFY(makeOld(f.db, version, 40));
    QString expected;
    {
        QSqlDatabase db
            = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("other"));
        db.setDatabaseName(f.db);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode=WAL")));
        q.finish();
        QVERIFY(q.exec(QStringLiteral("PRAGMA wal_autocheckpoint=0")));
        q.finish();
        QVERIFY(q.exec(QStringLiteral("UPDATE tracks SET play_count = 99 WHERE id = 7")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO tracks (id, path, category, size_bytes, modified_secs, date_added_secs, "
            "play_count, tags_json) VALUES (500, '/music/late.flac', 'Music', 1, 2, 3, 98, "
            "'{}')")));
        QVERIFY(QFileInfo(f.db + QStringLiteral("-wal")).size() > 0);
        expected = rowsOf(f.db);
        QVERIFY(expected.startsWith(QStringLiteral("41 rows: ")));
        QVERIFY(expected.contains(QStringLiteral(":99|")));
        QVERIFY(expected.endsWith(QStringLiteral("500:/music/late.flac:98")));
        {
            QString error;
            const auto library = Library::open(f.db, &error);
            QVERIFY2(library, qPrintable(error));
            QCOMPARE(library->stepAtOpening().copyPath, f.copy(version));
        }
        q.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("other"));
    QCOMPARE(rowsOf(f.copy(version)), expected);
    QCOMPARE(userVersion(f.copy(version)), version);
    QCOMPARE(rowsOf(f.db), expected);
}

void LibraryCopyBeforeStepTest::openedAgainThereIsNoSecondCopy_data()
{
    olderVersions();
}

void LibraryCopyBeforeStepTest::openedAgainThereIsNoSecondCopy()
{
    // L-SCHEMA: a copy is made when a step runs, and then only.
    QFETCH(int, version);
    const Folder f;
    QVERIFY(makeOld(f.db, version, 5));
    QVERIFY(Library::open(f.db));
    const QByteArray copied = bytesOf(f.copy(version));
    {
        const auto library = Library::open(f.db);
        QVERIFY(library);
        const SchemaStep step = library->stepAtOpening();
        QCOMPARE(step.from, 0);
        QCOMPARE(step.to, 0);
        QCOMPARE(step.copyPath, QString());
    }
    QCOMPARE(beside(f.db),
        QStringList(
            { QStringLiteral("library.db"), QStringLiteral("library.db.schema-%1").arg(version) }));
    QCOMPARE(bytesOf(f.copy(version)), copied);
}

void LibraryCopyBeforeStepTest::aNewLibraryHasNoCopy()
{
    // L-SCHEMA: a file that is new, and a library in memory, have nothing to copy.
    const Folder f;
    {
        const auto library = Library::open(f.db);
        QVERIFY(library);
        QCOMPARE(library->stepAtOpening().from, 0);
        QCOMPARE(library->stepAtOpening().copyPath, QString());
    }
    QCOMPARE(beside(f.db), QStringList({ QStringLiteral("library.db") }));
    const auto memory = Library::open(QStringLiteral(":memory:"));
    QVERIFY(memory);
    QCOMPARE(memory->stepAtOpening().from, 0);
    QCOMPARE(memory->stepAtOpening().copyPath, QString());
}

void LibraryCopyBeforeStepTest::aNameThatIsTakenIsLeft_data()
{
    QTest::addColumn<int>("version");
    QTest::addColumn<int>("taken"); // how many of the names are taken
    QTest::addColumn<bool>("byAFolder"); // the first of them by a folder
    for (const int version : { 1, 2 }) {
        if (version >= Library::currentSchemaVersion())
            continue;
        QTest::addRow("from schema %d, the name is a file", version) << version << 1 << false;
        QTest::addRow("from schema %d, the name is a folder", version) << version << 1 << true;
        QTest::addRow("from schema %d, the first three names", version) << version << 3 << false;
    }
}

void LibraryCopyBeforeStepTest::aNameThatIsTakenIsLeft()
{
    // L-SCHEMA: a name that is taken is never written over. The copy gets the next one.
    QFETCH(int, version);
    QFETCH(int, taken);
    QFETCH(bool, byAFolder);
    const Folder f;
    QVERIFY(makeOld(f.db, version, 5));
    const QString before = rowsOf(f.db);
    for (int number = 1; number <= taken; ++number) {
        if (byAFolder && number == 1) {
            QVERIFY(QDir().mkpath(f.copy(version, number) + QStringLiteral("/in it")));
            continue;
        }
        QFile mine(f.copy(version, number));
        QVERIFY(mine.open(QIODevice::WriteOnly));
        QCOMPARE(mine.write("mine"), qint64(4));
    }
    {
        QString error;
        const auto library = Library::open(f.db, &error);
        QVERIFY2(library, qPrintable(error));
        QCOMPARE(library->stepAtOpening().copyPath, f.copy(version, taken + 1));
    }
    for (int number = 1; number <= taken; ++number) {
        if (byAFolder && number == 1)
            QVERIFY(QFileInfo(f.copy(version, number) + QStringLiteral("/in it")).isDir());
        else
            QCOMPARE(bytesOf(f.copy(version, number)), QByteArray("mine"));
    }
    QCOMPARE(rowsOf(f.copy(version, taken + 1)), before);
    QCOMPARE(beside(f.db).size(), taken + 2);
}

void LibraryCopyBeforeStepTest::aFolderWithAQuoteInItsName_data()
{
    olderVersions();
}

void LibraryCopyBeforeStepTest::aFolderWithAQuoteInItsName()
{
    // L-SCHEMA: the place of the copy is given to the database as it is, whatever is in it.
    QFETCH(int, version);
    const Folder f(QStringLiteral("it's \"mine\"; --"));
    QVERIFY(makeOld(f.db, version, 5));
    const QString before = rowsOf(f.db);
    {
        QString error;
        const auto library = Library::open(f.db, &error);
        QVERIFY2(library, qPrintable(error));
        QCOMPARE(library->stepAtOpening().copyPath, f.copy(version));
    }
    QCOMPARE(rowsOf(f.copy(version)), before);
    QCOMPARE(beside(f.db).size(), 2);
}

void LibraryCopyBeforeStepTest::noCopyNoStep_data()
{
    olderVersions();
}

void LibraryCopyBeforeStepTest::noCopyNoStep()
{
    // L-SCHEMA: when the copy cannot be made the step does not run. The library does not
    // open, says why and where, is at the version it had, and nothing is left beside it.
    // The copy needs some 500,000 bytes here and no file may pass 65,536; the step itself
    // would fit.
    QFETCH(int, version);
    const Folder f;
    QVERIFY(makeOld(f.db, version, 2000));
    QVERIFY(QFileInfo(f.db).size() > 300000);
    const QString before = rowsOf(f.db);
    {
        const NoRoomPast limit(65536);
        QVERIFY(limit.isSet());
        QString error;
        const bool opened = Library::open(f.db, &error) != nullptr;
        QVERIFY2(!opened, "the library opened though no copy of it could be made");
        QVERIFY2(error.contains(QStringLiteral("no copy of it could be made")), qPrintable(error));
        QVERIFY2(error.contains(f.root), qPrintable(error));
    }
    QCOMPARE(beside(f.db), QStringList({ QStringLiteral("library.db") }));
    QCOMPARE(userVersion(f.db), version);
    QCOMPARE(rowsOf(f.db), before);
    // With room again it is copied and brought up.
    {
        QString error;
        const auto library = Library::open(f.db, &error);
        QVERIFY2(library, qPrintable(error));
        QCOMPARE(library->stepAtOpening().copyPath, f.copy(version));
    }
    QCOMPARE(rowsOf(f.copy(version)), before);
    QCOMPARE(userVersion(f.db), Library::currentSchemaVersion());
}

void LibraryCopyBeforeStepTest::aStepThatCannotRunLeavesNoCopy_data()
{
    olderVersions();
}

void LibraryCopyBeforeStepTest::aStepThatCannotRunLeavesNoCopy()
{
    // L-SCHEMA: a copy lies only beside a library whose step ran. While another program
    // holds the write lock the step cannot run, and there is no copy.
    QFETCH(int, version);
    const Folder f;
    QVERIFY(makeOld(f.db, version, 5));
    {
        QSqlDatabase db
            = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("holder"));
        db.setDatabaseName(f.db);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("BEGIN IMMEDIATE")));
        QString error;
        const bool opened = Library::open(f.db, &error) != nullptr;
        QVERIFY(!opened);
        QVERIFY(!error.isEmpty());
        QCOMPARE(beside(f.db), QStringList({ QStringLiteral("library.db") }));
        QVERIFY(q.exec(QStringLiteral("ROLLBACK")));
        q.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("holder"));
    QCOMPARE(userVersion(f.db), version);
    QCOMPARE(beside(f.db), QStringList({ QStringLiteral("library.db") }));
}

void LibraryCopyBeforeStepTest::theLineOfNotice()
{
    // L-SCHEMA: one line, the same for the window and the command line. None when no step ran.
    SchemaStep step;
    QCOMPARE(schemaStepNotice(step), QString());
    step.from = 1;
    step.to = 2;
    step.copyPath = QStringLiteral("/a folder/library.db.schema-1");
    QCOMPARE(schemaStepNotice(step),
        QStringLiteral("The library was brought up to date (schema 1 to 2). A copy of it as it "
                       "was before: /a folder/library.db.schema-1"));
    step.from = 2;
    step.to = 5;
    step.copyPath = QStringLiteral("library.db.schema-2-3");
    QCOMPARE(schemaStepNotice(step),
        QStringLiteral("The library was brought up to date (schema 2 to 5). A copy of it as it "
                       "was before: library.db.schema-2-3"));
}

QTEST_GUILESS_MAIN(LibraryCopyBeforeStepTest)
#include "tst_library_copy_before_step.moc"
