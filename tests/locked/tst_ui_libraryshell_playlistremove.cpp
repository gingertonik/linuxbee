// LOCKED ACCEPTANCE TEST (the library shell: in a playlist, Delete takes the track out of the playlist). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-REMOVE).
// Offscreen, with the real window, library, playlist store and shell. The
// files are copies in a temporary folder and the trash is this test's own.
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
#include "navigator/navigatorpanel.h"
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

void run(Rig &r, const char16_t *id)
{
    if (QAction *action = r.actions.action(s(id)))
        action->trigger();
    else
        QTest::qFail("no such action", __FILE__, __LINE__);
}

// A playlist "Mix": a, b, a again, c, and a file the library does not know.
QString mix(Rig &r, QString *ghost)
{
    *ghost = r.dir.path() + QStringLiteral("/music/not-in-the-library.flac");
    const QString id = r.playlists.create(s(u"Mix"));
    if (id.isEmpty() || !r.playlists.setTracks(id, { r.a, r.b, r.a, r.c, *ghost }))
        QTest::qFail("could not make the playlist", __FILE__, __LINE__);
    return id;
}

QString label(Rig &r)
{
    const QAction *action = r.actions.action(QStringLiteral("file.remove"));
    return action ? action->text() : QStringLiteral("NO SUCH ACTION");
}

}

class LibraryShellPlaylistRemoveTest : public QObject {
    Q_OBJECT
private slots:
    void outOfThePlaylist();
    void outsideAPlaylistItAsks();
    void theLabelFollowsTheView();
    void deleteFilesIsStillTheTrash();
};

void LibraryShellPlaylistRemoveTest::outOfThePlaylist()
{
    // SH-REMOVE: in a playlist, the command takes the selected tracks out of the playlist. It asks nothing,
    // and neither the library nor a file is touched.
    Rig r;
    NEED_DELETE(r);
    QString ghost;
    const QString id = mix(r, &ghost);
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + id));
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.a, r.b, r.c }));
    QSignalSpy removed(r.library.get(), &Library::tracksRemoved);
    QSignalSpy changed(&r.playlists, &PlaylistStore::tracksChanged);
    // Nothing selected: nothing happens and nothing is said.
    const QString before = r.shell->lastMessage();
    run(r, u"file.remove");
    QCOMPARE(r.playlists.tracks(id).size(), 5);
    QCOMPARE(r.shell->lastMessage(), before);

    r.select({ r.a, r.c });
    run(r, u"file.remove");
    QCOMPARE(r.asked, QStringList()); // no question
    QCOMPARE(r.playlists.tracks(id), QStringList({ r.b, ghost })); // both entries of a; the unknown file stays
    QCOMPARE(changed.count(), 1); // one change of the playlist
    QCOMPARE(removed.count(), 0);
    QCOMPARE(r.library->count(), 4);
    for (const QString &p : { r.a, r.b, r.c, r.d })
        QVERIFY2(QFileInfo::exists(p), qPrintable(p));
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.b }));
    QCOMPARE(r.shell->lastMessage(), s(u"Removed 2 tracks from the playlist"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.shell->lastMessage());
    // One track: the singular.
    r.select({ r.b });
    run(r, u"file.remove");
    QCOMPARE(r.playlists.tracks(id), QStringList({ ghost }));
    QCOMPARE(r.shell->lastMessage(), s(u"Removed 1 track from the playlist"));
    QCOMPARE(r.library->count(), 4);
}

void LibraryShellPlaylistRemoveTest::outsideAPlaylistItAsks()
{
    // SH-REMOVE: in any other view the command is Remove from Library, and it asks first, saying what is lost.
    Rig r;
    NEED_DELETE(r);
    QString ghost;
    const QString id = mix(r, &ghost);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    // Nothing selected: nothing is asked.
    run(r, u"file.remove");
    QCOMPARE(r.asked, QStringList());
    // "No": nothing happens, nothing is said, the selection is as it was.
    r.select({ r.a, r.d });
    r.yes = false;
    const QString before = r.shell->lastMessage();
    QSignalSpy removed(r.library.get(), &Library::tracksRemoved);
    run(r, u"file.remove");
    QCOMPARE(r.asked, QStringList({ s(u"Remove 2 tracks from the library? The files stay. Their play counts and dates added are lost.") }));
    QCOMPARE(r.library->count(), 4);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(r.shell->lastMessage(), before);
    QCOMPARE(r.shell->selectedTrackIds().size(), 2);
    // "Yes": as it was before there was a question.
    r.yes = true;
    run(r, u"file.remove");
    QCOMPARE(r.asked.size(), 2);
    QCOMPARE(r.library->count(), 2);
    QCOMPARE(removed.count(), 1);
    QVERIFY(QFileInfo::exists(r.a)); // the file stays
    QCOMPARE(r.shell->lastMessage(), s(u"Removed 2 from the library"));
    QCOMPARE(r.playlists.tracks(id).size(), 5); // no playlist is touched
    // One track: the singular.
    r.select({ r.b });
    r.yes = false;
    run(r, u"file.remove");
    QCOMPARE(r.asked.last(), s(u"Remove 1 track from the library? The file stays. Its play count and date added are lost."));
    QCOMPARE(r.library->count(), 2);
}

void LibraryShellPlaylistRemoveTest::theLabelFollowsTheView()
{
    // SH-REMOVE: the command is named for what it does in the view on show.
    Rig r;
    NEED_DELETE(r);
    QString ghost;
    const QString id = mix(r, &ghost);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(label(r), s(u"Remove from Library"));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + id));
    QCOMPARE(label(r), s(u"Remove from Playlist"));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QCOMPARE(label(r), s(u"Remove from Library"));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + id));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playing")));
    QCOMPARE(label(r), s(u"Remove from Library"));
}

void LibraryShellPlaylistRemoveTest::deleteFilesIsStillTheTrash()
{
    // SH-DELETE: in a playlist too, Delete Files asks and moves the file to the trash.
    Rig r;
    NEED_DELETE(r);
    QString ghost;
    const QString id = mix(r, &ghost);
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + id));
    r.select({ r.b });
    r.run();
    QCOMPARE(r.asked, QStringList({ s(u"Move 1 file to the trash?") }));
    QVERIFY(!QFileInfo::exists(r.b));
    QVERIFY(QFileInfo::exists(r.trashed("b.mp3")));
    QCOMPARE(r.library->count(), 3);
}

QTEST_MAIN(LibraryShellPlaylistRemoveTest)
#include "tst_ui_libraryshell_playlistremove.moc"
