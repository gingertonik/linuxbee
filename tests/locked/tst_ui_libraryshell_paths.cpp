// LOCKED ACCEPTANCE TEST (the library shell: the paths of the list are given without reading a track). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-PATHS) and
// docs/spec/library.md (L-COUNT). The cost is counted, not timed.
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

QStringList sortedCopy(QStringList list)
{
    list.sort();
    return list;
}

}

class LibraryShellPathsTest : public QObject {
    Q_OBJECT
private slots:
    void noTrackIsRead();
    void theSamePathsAsTheList();
    void theyFollowTheLibrary();
};

void LibraryShellPathsTest::noTrackIsRead()
{
    // SH-PATHS, speed: the paths of the list, of the selection, and of an activated track's list cost no read.
    Rig r;
    NEED_DELETE(r);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    const QList<qint64> ids { r.id(r.a), r.id(r.b), r.id(r.c), r.id(r.d) };
    r.shell->trackList()->selectTracks({ ids.at(1), ids.at(3) });
    QStringList played;
    int at = -1;
    QObject::connect(r.shell.get(), &LibraryShell::playRequested, r.shell.get(), [&](const QStringList &paths, int index) {
        played = paths;
        at = index;
    });
    const qint64 before = r.library->tracksRead();
    const QStringList shown = r.shell->shownPaths();
    QCOMPARE(r.library->tracksRead() - before, 0);
    const QStringList selected = r.shell->selectedPaths();
    QCOMPARE(r.library->tracksRead() - before, 0);
    emit r.shell->trackList()->trackActivated(ids.at(2));
    QCOMPARE(r.library->tracksRead() - before, 0);
    QCOMPARE(sortedCopy(shown), sortedCopy({ r.a, r.b, r.c, r.d })); // the order is the list's (next function)
    QStringList wanted;
    for (const QString &p : shown)
        if (p == r.b || p == r.d)
            wanted.append(p);
    QCOMPARE(selected, wanted);
    QCOMPARE(played, shown);
    QCOMPARE(at, int(shown.indexOf(r.c)));
}

void LibraryShellPathsTest::theSamePathsAsTheList()
{
    // SH-PATHS: the rows of the list, in the order shown, whatever the order and the search.
    Rig r;
    NEED_DELETE(r);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    auto listed = [&r] {
        QStringList out;
        TrackListModel *model = r.shell->model();
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto t = r.library->track(model->trackId(row));
            out.append(t ? t->path : QStringLiteral("NOT IN THE LIBRARY"));
        }
        return out;
    };
    QCOMPARE(r.shell->shownPaths(), listed());
    // Another order: the first column, descending.
    r.shell->model()->sort(0, Qt::DescendingOrder);
    const QStringList down = listed();
    QCOMPARE(down.size(), 4);
    QCOMPARE(r.shell->shownPaths(), down);
    r.shell->model()->sort(0, Qt::AscendingOrder);
    QStringList up = down;
    std::reverse(up.begin(), up.end());
    QCOMPARE(listed(), up); // the sort did turn the list round
    QCOMPARE(r.shell->shownPaths(), up);
    // A search narrows the list, and the paths with it.
    r.shell->trackList()->setSearchText(s(u"flac"));
    QCOMPARE(listed(), QStringList({ r.a }));
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.a }));
    r.shell->trackList()->setSearchText(QString());
    QCOMPARE(r.shell->shownPaths().size(), 4);
    // A playlist: its tracks, in its order.
    const QString id = r.playlists.create(s(u"Mix"));
    QVERIFY(r.playlists.setTracks(id, { r.d, r.b }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + id));
    QCOMPARE(r.shell->shownPaths(), listed());
    QCOMPARE(sortedCopy(r.shell->shownPaths()), sortedCopy({ r.b, r.d }));
    // The selection, in the order shown.
    r.shell->trackList()->selectTracks({ r.id(r.b), r.id(r.d) });
    QStringList want;
    for (const QString &p : listed())
        want.append(p);
    QCOMPARE(r.shell->selectedPaths(), want);
}

void LibraryShellPathsTest::theyFollowTheLibrary()
{
    // SH-PATHS: a path that changed is given as it is now; a track that left is not given.
    Rig r;
    NEED_DELETE(r);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(r.shell->shownPaths().size(), 4);
    const QString moved = r.dir.path() + QStringLiteral("/music/album/a-moved.flac");
    QVERIFY(QFile::rename(r.a, moved));
    QVERIFY(r.library->setPath(r.id(r.a), moved));
    QVERIFY(r.shell->shownPaths().contains(moved));
    QVERIFY(!r.shell->shownPaths().contains(r.a));
    r.shell->trackList()->selectTracks({ r.id(moved) });
    QCOMPARE(r.shell->selectedPaths(), QStringList({ moved }));
    QCOMPARE(r.library->remove({ r.id(r.b) }), 1);
    QVERIFY(!r.shell->shownPaths().contains(r.b));
    QCOMPARE(r.shell->shownPaths().size(), 3);
    // Sent to another section: gone from this view's paths.
    QCOMPARE(r.library->setCategory({ r.id(r.c) }, s(u"Inbox")), 1);
    QVERIFY(!r.shell->shownPaths().contains(r.c));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.c }));
}

QTEST_MAIN(LibraryShellPathsTest)
#include "tst_ui_libraryshell_paths.moc"
