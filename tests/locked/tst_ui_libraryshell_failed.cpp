// LOCKED ACCEPTANCE TEST (the library shell: a command says so when the library did not take the change). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-FAILED, SH-DELETE step 4). The library is a file here,
// and another holder has it for writing while the command runs, so the library's own step fails.
#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, c, d;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;
    bool yes = true;
    QStringList asked;
    int existedWhenAsked = 0; // how many of the four files were there at the question

    Rig()
        : library(Library::open(dir.path() + QStringLiteral("/library.db")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        qputenv("XDG_DATA_HOME", QFile::encodeName(dir.path() + QStringLiteral("/xdg-data")));
        made(QStringLiteral("music/album"));
        a = put("basic.flac", "music/album/a.flac");
        b = put("basic.mp3", "music/album/b.mp3");
        c = put("basic.ogg", "music/album/c.ogg");
        d = put("basic.m4a", "music/d.m4a");
        for (const QString &p : { a, b, c, d })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        ShellHooks hooks;
        hooks.now = [this] { return now; };
        hooks.confirm = [this](const QString &question) {
            asked.append(question);
            existedWhenAsked = 0;
            for (const QString &p : { a, b, c, d })
                existedWhenAsked += QFileInfo::exists(p) ? 1 : 0;
            return yes;
        };
        shell = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, hooks);
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    QString trashed(const char *name) const { return dir.path() + QStringLiteral("/xdg-data/Trash/files/") + QLatin1String(name); }
    qint64 id(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->id : 0;
    }
    void select(const QStringList &paths)
    {
        QList<qint64> ids;
        for (const QString &p : paths)
            ids.append(id(p));
        shell->trackList()->selectTracks(ids);
    }
    void run()
    {
        if (QAction *action = actions.action(QStringLiteral("file.delete")))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
};

#define NEED_DELETE(r) \
    do { \
        if (!(r).shell->trackList() || !(r).actions.isImplemented(QStringLiteral("file.delete"))) \
            QFAIL("Delete Files has not been built"); \
    } while (false)

// Holds the library's database for writing for as long as it lives.
struct Holder {
    QString name;
    explicit Holder(const Rig &r)
        : name(QStringLiteral("holder-%1").arg(quintptr(this)))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(r.dir.path() + QStringLiteral("/library.db"));
        if (!db.open() || !QSqlQuery(db).exec(QStringLiteral("BEGIN IMMEDIATE")))
            QTest::qFail("could not hold the database", __FILE__, __LINE__);
    }
    ~Holder()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(name);
            QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
};

void trigger(Rig &r, const char16_t *id)
{
    if (QAction *action = r.actions.action(s(id)))
        action->trigger();
    else
        QTest::qFail("no such action", __FILE__, __LINE__);
}

}

class LibraryShellFailedTest : public QObject {
    Q_OBJECT
private slots:
    void deleteFilesSaysTheLibraryStillListsThem();
    void removeSaysItCouldNot();
    void sendToSaysItCouldNot();
};

void LibraryShellFailedTest::deleteFilesSaysTheLibraryStillListsThem()
{
    // SH-DELETE step 4: the files went to the trash and the library could not be told. The message says so,
    // and the trashed paths are still announced (the files are gone; the queue must know).
    Rig r;
    NEED_DELETE(r);
    r.select({ r.a, r.b });
    QSignalSpy trashedPaths(r.shell.get(), &LibraryShell::filesTrashed);
    {
        Holder holder(r);
        r.run();
    }
    QVERIFY(!QFileInfo::exists(r.a));
    QVERIFY(QFileInfo(r.trashed("a.flac")).isFile());
    QCOMPARE(r.library->count(), 4); // the library did not take it
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 2 files to the trash; the library could not be changed and still lists them"));
    QCOMPARE(trashedPaths.count(), 1);
    QStringList told = trashedPaths.at(0).at(0).toStringList();
    told.sort();
    QStringList both { r.a, r.b };
    both.sort();
    QCOMPARE(told, both);
    // With the library free again the same command finds their files not there, and the tracks stay
    // (SH-DELETE step 3). Remove from Library is what takes them out.
    r.select({ r.a, r.b });
    r.run();
    QCOMPARE(r.shell->lastMessage(), s(u"No file was moved to the trash; 2 were not there"));
    // One file.
    r.select({ r.c });
    {
        Holder holder(r);
        r.run();
    }
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 1 file to the trash; the library could not be changed and still lists it"));
    // All well: the message is what it was.
    r.select({ r.d });
    r.run();
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 1 file to the trash"));
    QCOMPARE(r.library->count(), 3);
}

void LibraryShellFailedTest::removeSaysItCouldNot()
{
    // SH-FAILED: Remove from Library that removed fewer than were selected says how many it could not.
    Rig r;
    NEED_DELETE(r);
    r.select({ r.a, r.b, r.c });
    {
        Holder holder(r);
        trigger(r, u"file.remove");
    }
    QCOMPARE(r.library->count(), 4);
    QCOMPARE(r.shell->lastMessage(), s(u"The library could not be changed: none of the 3 tracks was removed"));
    r.select({ r.a, r.b, r.c });
    trigger(r, u"file.remove");
    QCOMPARE(r.shell->lastMessage(), s(u"Removed 3 from the library"));
    QCOMPARE(r.library->count(), 1);
}

void LibraryShellFailedTest::sendToSaysItCouldNot()
{
    // SH-FAILED: Send To that moved nothing although a selected track was in another section says so.
    Rig r;
    NEED_DELETE(r);
    r.select({ r.a, r.b });
    {
        Holder holder(r);
        trigger(r, u"track.sendToAudiobooks");
    }
    QCOMPARE(r.shell->lastMessage(), s(u"The library could not be changed: nothing was moved to Audiobooks"));
    QCOMPARE(r.library->track(r.id(r.a))->category, s(u"Music"));
    r.select({ r.a, r.b });
    trigger(r, u"track.sendToAudiobooks");
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 2 to Audiobooks"));
    // Already there: that is not a failure.
    r.shell->navigator()->selectNode(s(u"library:Audiobooks"));
    r.select({ r.a, r.b });
    trigger(r, u"track.sendToAudiobooks");
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 0 to Audiobooks"));
}

QTEST_MAIN(LibraryShellFailedTest)
#include "tst_ui_libraryshell_failed.moc"
