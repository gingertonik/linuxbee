// LOCKED ACCEPTANCE TEST (the library: the copy made before a schema step has the library's mode).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/library.md, L-SCHEMA ("A copy before a step": whose it is, and a library
// reached through a link). Found by the review of the first building, 2026-11-19: a library
// kept for its owner alone got a copy that everyone could read. It holds paths and tags.
//
// The mask of this process is set to nothing for the length of the test, so that a mode seen
// on a copy is one the program gave it and not one the mask left over.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>
#include <sys/types.h>

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

// The nine bits of a file's mode, or -1. A link is not followed.
int modeOf(const QString &path)
{
    struct stat st { };
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? int(st.st_mode & 0777) : -1;
}

}

class LibraryCopyModeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { m_mask = ::umask(0); }
    void cleanupTestCase() { ::umask(m_mask); }
    void theCopyHasTheLibrarysMode_data();
    void theCopyHasTheLibrarysMode();
    void aLibraryReachedThroughALink();

private:
    mode_t m_mask = 0;
};

void LibraryCopyModeTest::theCopyHasTheLibrarysMode_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("its owner alone (600)") << 0600;
    QTest::newRow("its group may read (640)") << 0640;
    QTest::newRow("its group may write (664)") << 0664;
    QTest::newRow("everyone may read (644)") << 0644;
}

void LibraryCopyModeTest::theCopyHasTheLibrarysMode()
{
    // L-SCHEMA: no one can read the copy who cannot read the library, and whoever can read
    // the library can read the copy.
    QFETCH(int, mode);
    QVERIFY(Library::currentSchemaVersion() >= 2);
    QTemporaryDir dir;
    const QString root = QFileInfo(dir.path()).canonicalFilePath();
    const QString db = root + QStringLiteral("/library.db");
    QVERIFY(makeOld(db, 1, 5));
    QCOMPARE(::chmod(QFile::encodeName(db).constData(), mode_t(mode)), 0);
    QString copy;
    {
        QString error;
        const auto library = Library::open(db, &error);
        QVERIFY2(library, qPrintable(error));
        copy = library->stepAtOpening().copyPath;
    }
    QCOMPARE(copy, db + QStringLiteral(".schema-1"));
    QCOMPARE(modeOf(copy), mode);
    QCOMPARE(modeOf(db), mode);
    QCOMPARE(userVersion(copy), 1);
    QCOMPARE(beside(db),
        QStringList({ QStringLiteral("library.db"), QStringLiteral("library.db.schema-1") }));
}

void LibraryCopyModeTest::aLibraryReachedThroughALink()
{
    // L-SCHEMA: the copy lies beside the name the library was opened by, and is a file of its
    // own with the mode of the file the link leads to. Nothing is put beside that file.
    QVERIFY(Library::currentSchemaVersion() >= 2);
    QTemporaryDir dir;
    const QString root = QFileInfo(dir.path()).canonicalFilePath();
    QVERIFY(QDir().mkpath(root + QStringLiteral("/real")));
    QVERIFY(QDir().mkpath(root + QStringLiteral("/named")));
    const QString real = root + QStringLiteral("/real/library.db");
    const QString named = root + QStringLiteral("/named/mine.db");
    QVERIFY(makeOld(real, 1, 5));
    QCOMPARE(::chmod(QFile::encodeName(real).constData(), mode_t(0600)), 0);
    QVERIFY(QFile::link(real, named));
    QString copy;
    {
        QString error;
        const auto library = Library::open(named, &error);
        QVERIFY2(library, qPrintable(error));
        copy = library->stepAtOpening().copyPath;
    }
    QCOMPARE(copy, named + QStringLiteral(".schema-1"));
    QVERIFY(!QFileInfo(copy).isSymLink());
    QCOMPARE(modeOf(copy), 0600);
    QCOMPARE(userVersion(copy), 1);
    QCOMPARE(userVersion(real), Library::currentSchemaVersion());
    QCOMPARE(beside(real), QStringList({ QStringLiteral("library.db") }));
    QVERIFY(QFileInfo(named).isSymLink());
}

QTEST_GUILESS_MAIN(LibraryCopyModeTest)
#include "tst_library_copy_mode.moc"
