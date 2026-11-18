// LOCKED ACCEPTANCE TEST (the Delete key in a playlist says when nothing was removed). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: SH-REMOVE in docs/spec/library-shell.md, Y-EDIT in docs/spec/playlists.md.
// Offscreen, with the real window, library and store, in a temporary folder. The rig is the one of
// tst_ui_libraryshell_playlistremove.cpp.
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

class LibraryShellPlaylistRemoveRefusedTest : public QObject {
    Q_OBJECT
private slots:
    void aPlaylistThatIsNotUtf8();
    void aPlaylistThatCannotBeWritten();
};

namespace {

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArrayLiteral("<cannot open>");
}

}

void LibraryShellPlaylistRemoveRefusedTest::aPlaylistThatIsNotUtf8()
{
    // SH-REMOVE: a playlist the store will not edit (Y-EDIT) keeps every entry, and the message
    // says why. It used to say "Removed 1 track", and write the other entry back as a name of no
    // file.
    (void)&label;
    (void)&mix;
    Rig r;
    NEED_DELETE(r);
    // Made through the store, so that the navigator has it; then its bytes are put there by hand.
    QCOMPARE(r.playlists.create(s(u"Old")), s(u"Old"));
    const QString path = r.dir.path() + s(u"/playlists/Old.m3u8");
    const QByteArray bytes = QFile::encodeName(r.a) + "\n/m/Bj\xF6rk/J\xF3ga.mp3\n";
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(bytes);
    }
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:Old")));
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.a }));
    QSignalSpy changed(&r.playlists, &PlaylistStore::tracksChanged);
    r.select({ r.a });
    run(r, u"file.remove");
    QCOMPARE(
        r.shell->lastMessage(), s(u"Could not remove from the playlist: the file is not UTF-8"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.shell->lastMessage());
    QCOMPARE(bytesOf(path), bytes);
    QCOMPARE(int(changed.count()), 0);
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.a }));
    QCOMPARE(r.library->count(), 4);
}

void LibraryShellPlaylistRemoveRefusedTest::aPlaylistThatCannotBeWritten()
{
    // SH-REMOVE: when the store removed nothing, "Removed" is not said. (The audit, part 4, F5.)
    Rig r;
    NEED_DELETE(r);
    const QString id = r.playlists.create(s(u"Fixed"));
    QVERIFY(r.playlists.setTracks(id, { r.a, r.b }));
    const QString path = r.dir.path() + s(u"/playlists/Fixed.m3u8");
    const QByteArray bytes = bytesOf(path);
    QVERIFY(QFile::setPermissions(path, QFile::ReadOwner));
    QVERIFY(
        QFile::setPermissions(r.dir.path() + s(u"/playlists"), QFile::ReadOwner | QFile::ExeOwner));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:Fixed")));
    r.select({ r.a });
    run(r, u"file.remove");
    QFile::setPermissions(
        r.dir.path() + s(u"/playlists"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QCOMPARE(r.shell->lastMessage(),
        s(u"Could not remove from the playlist: the file could not be written"));
    QCOMPARE(bytesOf(path), bytes);
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.a, r.b }));
}

QTEST_MAIN(LibraryShellPlaylistRemoveRefusedTest)
#include "tst_ui_libraryshell_playlistremove_refused.moc"
