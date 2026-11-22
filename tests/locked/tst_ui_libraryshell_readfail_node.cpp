// LOCKED ACCEPTANCE TEST (the window: a new node that cannot be read, and a library change,
// fix-r3b). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason, and
// left alone.
//
// Expected values are rules in docs/spec/library.md (L-READFAIL) and docs/spec/library-shell.md.
// The library is a file here; one of its pages is overwritten while the shell shows it, so every
// read of the tracks fails from then on.
#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"
#include "shell/scanrunner.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

// L-READFAIL (how a test makes a read fail): the root page of the tracks table is overwritten
// while the library has the file open. The library's last changes are written into the file first,
// and another connection's write afterwards makes the library read the file again rather than what
// it remembers. Every read of the tracks then fails with "malformed"; the rest of the file reads.
bool damageOpen(const QString &file)
{
    const QString name = QStringLiteral("damage-open");
    bool ok = false;
    {
        QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        raw.setDatabaseName(file);
        if (raw.open()) {
            QSqlQuery q(raw);
            int size = 0;
            int root = 0;
            int version = 0;
            q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
            if (q.exec(QStringLiteral("PRAGMA page_size")) && q.next())
                size = q.value(0).toInt();
            if (q.exec(QStringLiteral("SELECT rootpage FROM sqlite_master WHERE name = 'tracks'"))
                && q.next())
                root = q.value(0).toInt();
            if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
                version = q.value(0).toInt();
            q.finish();
            QFile f(file);
            if (size > 0 && root > 0 && f.open(QIODevice::ReadWrite)
                && f.seek(qint64(root - 1) * size))
                ok = f.write(QByteArray(size, char(0xff))) == size;
            f.close();
            ok = ok && q.exec(QStringLiteral("PRAGMA user_version = %1").arg(version));
            q.finish();
            raw.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

struct Rig {
    QTemporaryDir dir;
    QString file;
    QString a, b, c, d;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;
    QStringList asked;
    QStringList folders;

    Rig()
        : file(dir.path() + QStringLiteral("/library.db"))
        , library(Library::open(file))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        qputenv("XDG_DATA_HOME", QFile::encodeName(dir.path() + QStringLiteral("/xdg-data")));
        made(QStringLiteral("music"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.mp3", "music/b.mp3");
        c = put("basic.ogg", "music/c.ogg");
        d = put("basic.m4a", "music/d.m4a");
        for (const QString &p : { a, b, c, d })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        ShellHooks hooks;
        hooks.now = [this] { return now; };
        hooks.confirm = [this](const QString &question) {
            asked.append(question);
            return true;
        };
        hooks.chooseFolders = [this] { return folders; };
        shell = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, hooks);
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from),
                target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
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
    void run(const char *name)
    {
        if (QAction *action = actions.action(QLatin1String(name)))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    bool unreadable() const
    {
        return message().contains(QStringLiteral("the library could not be read"));
    }
    QString message() const { return shell->lastMessage(); }
};

}

class LibraryShellReadFailNodeTest : public QObject {
    Q_OBJECT
private slots:
    void aNewNodeThatCannotBeReadIsShownEmpty();
    void aChangeThatCannotBeReadIsSaid();
};

void LibraryShellReadFailNodeTest::aNewNodeThatCannotBeReadIsShownEmpty()
{
    // L-READFAIL 3 (fix-r3b; the review of R-3, note 2): keeping what is shown is for a node read
    // again. A node chosen while the library cannot be read is shown empty, with the line, and
    // nodeTracks() answers for the node it names: no other node's tracks under its name.
    Rig r;
    const QString list = r.playlists.create(QStringLiteral("Two"));
    QVERIFY(!list.isEmpty());
    QVERIFY(r.playlists.setTracks(list, { r.a, r.b }));
    QVERIFY(r.shell->navigator()->selectNode(QStringLiteral("library:Music")));
    QCOMPARE(r.shell->model()->rowCount(), 4);
    QVERIFY(damageOpen(r.file));
    QVERIFY(r.shell->navigator()->selectNode(QStringLiteral("playlist:") + list));
    QCOMPARE(r.shell->model()->rowCount(), 0);
    QCOMPARE(r.shell->nodeTracks().size(), 0);
    QCOMPARE(
        r.message(), QStringLiteral("Could not show the tracks: the library could not be read"));
    // Read again, still unreadable: it stays empty (it is the same node now).
    r.shell->reloadNode();
    QCOMPARE(r.shell->model()->rowCount(), 0);
    QVERIFY2(r.unreadable(), qPrintable(r.message()));
}

void LibraryShellReadFailNodeTest::aChangeThatCannotBeReadIsSaid()
{
    // L-READFAIL 3: a library change whose tracks cannot be looked up leaves the list as it is, and
    // says so on the status bar, as a node does.
    Rig r;
    QVERIFY(r.shell->navigator()->selectNode(QStringLiteral("library:Music")));
    QCOMPARE(r.shell->model()->rowCount(), 4);
    QVERIFY(damageOpen(r.file));
    emit r.library->tracksUpdated({ 1 });
    QCOMPARE(r.shell->model()->rowCount(), 4);
    QCOMPARE(
        r.message(), QStringLiteral("Could not show the tracks: the library could not be read"));
}

QTEST_MAIN(LibraryShellReadFailNodeTest)
#include "tst_ui_libraryshell_readfail_node.moc"
