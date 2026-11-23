// LOCKED ACCEPTANCE TEST (the library shell: Delete Files moves the selected files to the trash).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never by
// editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-DELETE) and
// docs/spec/delete-files.md. Runs on the offscreen platform with the real
// window, library and shell. The files are copies of fixtures in a
// temporary folder, and the trash is a folder of this test's own
// (XDG_DATA_HOME), so no real file and no real trash is touched.
#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
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
        : library(Library::open(QStringLiteral(":memory:")))
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
    QString trashed(const char *name) const
    {
        return dir.path() + QStringLiteral("/xdg-data/Trash/files/") + QLatin1String(name);
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
    void run()
    {
        if (QAction *action = actions.action(QStringLiteral("file.delete")))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
};

#define NEED_DELETE(r)                                                                             \
    do {                                                                                           \
        if (!(r).shell->trackList() || !(r).actions.isImplemented(QStringLiteral("file.delete")))  \
            QFAIL("Delete Files has not been built");                                              \
    } while (false)

}

class LibraryShellDeleteTest : public QObject {
    Q_OBJECT
private slots:
    void theCommand();
    void askedFirst();
    void toTheTrash();
    void whatCouldNotGo();
    void nothingSelected();
};

void LibraryShellDeleteTest::theCommand()
{
    // SH-COMMANDS: Delete Files is the shell's while it exists; Remove from Library is still its
    // own command.
    Rig r;
    NEED_DELETE(r);
    QVERIFY(r.actions.isImplemented(s(u"file.remove")));
    r.shell.reset();
    QVERIFY(!r.actions.isImplemented(s(u"file.delete")));
}

void LibraryShellDeleteTest::askedFirst()
{
    // SH-DELETE: the question comes before anything is touched, and "no" touches nothing.
    Rig r;
    NEED_DELETE(r);
    r.yes = false;
    r.select({ r.a, r.b });
    QSignalSpy removed(r.library.get(), &Library::tracksRemoved);
    const QString before = r.shell->lastMessage();
    r.run();
    QCOMPARE(r.asked, QStringList({ s(u"Move 2 files to the trash?") }));
    QCOMPARE(r.existedWhenAsked, 4);
    QVERIFY(QFileInfo::exists(r.a) && QFileInfo::exists(r.b));
    QCOMPARE(r.library->count(), 4);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(r.shell->lastMessage(), before); // no message
    QCOMPARE(r.shell->selectedTrackIds().size(), 2); // the selection is as it was
    QVERIFY(!QFileInfo::exists(r.dir.path() + s(u"/xdg-data/Trash/files/a.flac")));
    // One file: the singular.
    r.select({ r.d });
    r.run();
    QCOMPARE(r.asked.last(), s(u"Move 1 file to the trash?"));
    QVERIFY(QFileInfo::exists(r.d));
}

void LibraryShellDeleteTest::toTheTrash()
{
    // SH-DELETE: yes moves the files to the trash and takes their tracks out of the library, in one
    // go.
    Rig r;
    NEED_DELETE(r);
    r.select({ r.a, r.b });
    const QList<qint64> ids = r.shell->selectedTrackIds();
    QSignalSpy removed(r.library.get(), &Library::tracksRemoved);
    r.run();
    QCOMPARE(r.asked.size(), 1);
    QCOMPARE(r.existedWhenAsked, 4);
    QVERIFY(!QFileInfo::exists(r.a));
    QVERIFY(!QFileInfo::exists(r.b));
    QVERIFY(QFileInfo(r.trashed("a.flac")).isFile());
    QVERIFY(QFileInfo(r.trashed("b.mp3")).isFile());
    QCOMPARE(QFileInfo(r.trashed("a.flac")).size(),
        QFileInfo(QStringLiteral(LINUXBEE_TEST_DATA) + s(u"/tags/basic.flac")).size());
    // The others are where they were.
    QVERIFY(QFileInfo::exists(r.c) && QFileInfo::exists(r.d));
    QCOMPARE(r.library->count(), 2);
    QVERIFY(!r.library->trackByPath(r.a).has_value());
    QVERIFY(r.library->trackByPath(r.c).has_value());
    // One call for the whole set: one change of the list.
    QCOMPARE(removed.count(), 1);
    QList<qint64> gone = removed.at(0).at(0).value<QList<qint64>>();
    std::sort(gone.begin(), gone.end());
    QList<qint64> wanted = ids;
    std::sort(wanted.begin(), wanted.end());
    QCOMPARE(gone, wanted);
    QCOMPARE(r.shell->model()->rowCount(), 2);
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 2 files to the trash"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.shell->lastMessage());
    // The folder the files were in stays, though one file is left in it; and one that is emptied
    // stays too.
    r.select({ r.c });
    r.run();
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 1 file to the trash"));
    QVERIFY(QFileInfo(r.dir.path() + s(u"/music/album")).isDir());
    QCOMPARE(r.library->count(), 1);
}

void LibraryShellDeleteTest::whatCouldNotGo()
{
    // SH-DELETE: a file that is not there, and one the trash does not take, keep their tracks; the
    // message counts them.
    Rig r;
    NEED_DELETE(r);
    QVERIFY(QFile::remove(r.b)); // gone behind the program's back
    r.select({ r.a, r.b, r.c });
    QSignalSpy removed(r.library.get(), &Library::tracksRemoved);
    r.run();
    QCOMPARE(r.asked, QStringList({ s(u"Move 3 files to the trash?") }));
    QVERIFY(!QFileInfo::exists(r.a) && !QFileInfo::exists(r.c));
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 2 files to the trash; 1 was not there"));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(0).value<QList<qint64>>().size(), 2);
    QVERIFY(r.library->trackByPath(r.b)
            .has_value()); // its track stays: "Remove from Library" is the way
    QCOMPARE(r.library->count(), 2);

    // The trash is broken: the file stays, byte for byte, and so does its track.
    const qint64 size = QFileInfo(r.d).size();
    qputenv("XDG_DATA_HOME",
        QFile::encodeName(r.b + s(u"/not-a-folder"))); // under a path that is no folder
    QFile blocker(r.b);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    removed.clear();
    r.select({ r.d });
    r.run();
    QCOMPARE(QFileInfo(r.d).size(), size);
    QVERIFY(r.library->trackByPath(r.d).has_value());
    QCOMPARE(removed.count(), 0); // nothing went, so the library is not asked
    QCOMPARE(r.shell->lastMessage(),
        s(u"No file was moved to the trash; 1 could not be moved: the trash did not take it"));

    // A link in the library is refused.
    qputenv("XDG_DATA_HOME", QFile::encodeName(r.dir.path() + s(u"/xdg-data")));
    const QString link = r.dir.path() + s(u"/music/link.m4a");
    QVERIFY(QFile::link(r.d, link));
    QVERIFY(r.library->addFile(link, QStringLiteral("Music"), r.now) != 0);
    r.select({ link, r.d });
    r.run();
    QVERIFY(QFileInfo(link).isSymLink());
    QVERIFY(!QFileInfo::exists(r.d)); // the real file went; the link was left
    QVERIFY(r.library->trackByPath(link).has_value());
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 1 file to the trash; 1 could not be moved: a link"));
}

void LibraryShellDeleteTest::nothingSelected()
{
    // SH-COMMANDS: with nothing selected nothing is asked and nothing is said.
    Rig r;
    NEED_DELETE(r);
    const QString before = r.shell->lastMessage();
    r.run();
    QCOMPARE(r.asked, QStringList());
    QCOMPARE(r.shell->lastMessage(), before);
    QCOMPARE(r.library->count(), 4);
}

QTEST_MAIN(LibraryShellDeleteTest)
#include "tst_ui_libraryshell_delete.moc"
