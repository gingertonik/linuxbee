// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-x). Runs on the
// offscreen platform with a real library (in memory), a real playlist folder
// (in a temporary directory) and the real window parts.
#include <QFile>
#include <QAction>
#include <QLineEdit>
#include <QSettings>
#include <QLabel>
#include <QPointer>
#include <QSignalSpy>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "shell/libraryshell.h"
#include "shell/scanrunner.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// A settings file as the next run of the program reads it. Within one run Qt
// hands back the value that was put in (a bool stays a bool); read from disk
// by a later run, every value is text. Copying the file gives the later view.
QString nextRun(QSettings &settings)
{
    static int count = 0;
    settings.sync();
    const QString copy = settings.fileName() + QStringLiteral(".run%1.ini").arg(++count);
    QFile::remove(copy);
    QFile::copy(settings.fileName(), copy);
    return copy;
}

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(name);
}

// Stops a test, as a failure, when the shell has not built its parts.
#define NEED_PARTS(r) \
    do { \
        if (!(r).shell->navigator() || !(r).shell->model() || !(r).shell->trackList()) \
            QFAIL("the shell has not built its navigator, model and track list"); \
    } while (false)

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, c, d, junk;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QStringList folders; // what the folder chooser will answer
    QStringList files; // what the file chooser will answer
    int asked = 0;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        made(QStringLiteral("inbox"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.mp3", "music/b.mp3");
        c = put("basic.ogg", "music/c.ogg");
        junk = put("notaudio.mp3", "music/junk.mp3");
        d = put("basic.flac", "inbox/d.flac");
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(fixture(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void fill() // three in Music, one in the Inbox
    {
        for (const QString &p : { a, b, c })
            QVERIFY(library->addFile(p, QStringLiteral("Music"), now) != 0);
        QVERIFY(library->addFile(d, QStringLiteral("Inbox"), now) != 0);
    }
    void start()
    {
        ShellHooks hooks;
        hooks.chooseFolders = [this] {
            ++asked;
            return folders;
        };
        hooks.chooseFiles = [this] {
            ++asked;
            return files;
        };
        hooks.now = [this] { return now; };
        shell = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, hooks);
    }
    void run(const char16_t *id)
    {
        if (QAction *action = actions.action(s(id)))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    qint64 idOf(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->id : 0;
    }
    QStringList shown() const { return sorted(shell->shownPaths()); }
    QStringList navigatorRows() const
    {
        QStringList out;
        auto *tree = window.findChild<QTreeWidget *>(s(u"navigatorTree"));
        if (!tree)
            return { s(u"NO TREE") };
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            out.append((*it)->text(0));
        return out;
    }
};

}

class LibraryShellTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void showsTheLibrary();
    void scan();
    void scanIntoTheChosenSection();
    void addFiles();
    void followsTheLibrary();
    void removeAndSendTo();
    void playlistNodes();
    void playingNode();
    void activation();
    void searchAndFind();
    void stored();
    void extending();
};

void LibraryShellTest::structure()
{
    // SH-BUILD
    Rig r;
    r.start();
    QVERIFY(r.shell->navigator());
    QVERIFY(r.shell->model());
    QVERIFY(r.shell->trackList());
    QCOMPARE(r.window.panelWidget(MainWindow::Panel::Navigator), r.shell->navigator());
    QCOMPARE(r.window.pages(), QStringList({ s(u"tracks") }));
    QCOMPARE(r.window.currentPage(), s(u"tracks"));
    QCOMPARE(r.shell->currentNode(), s(u"library:Music"));
    QCOMPARE(r.shell->shownPaths(), QStringList());
    QCOMPARE(r.shell->lastMessage(), QString());
    for (const char16_t *id : { u"file.scanFolders", u"file.addFiles", u"file.remove", u"track.sendToInbox", u"track.sendToMusic",
             u"track.sendToAudiobooks", u"view.refresh", u"edit.find" })
        QVERIFY2(r.actions.isImplemented(s(id)), qPrintable(s(id)));
    // Gone with the shell.
    r.shell.reset();
    QVERIFY(!r.actions.isImplemented(s(u"file.scanFolders")));
}

void LibraryShellTest::showsTheLibrary()
{
    // SH-NODE, SH-COUNTS
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c }));
    QCOMPARE(r.shell->model()->totalCount(), 3);
    const QStringList rows = r.navigatorRows();
    QVERIFY2(rows.contains(s(u"Music (3)")), qPrintable(rows.join(u'|')));
    QVERIFY(rows.contains(s(u"Inbox (1)")));
    QVERIFY(rows.contains(s(u"Audiobooks")));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QCOMPARE(r.shell->currentNode(), s(u"library:Inbox"));
    QCOMPARE(r.shown(), QStringList({ r.d }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Podcasts")));
    QCOMPARE(r.shown(), QStringList());
    QVERIFY(r.shell->navigator()->selectNode(s(u"playing"))); // nothing to show yet
    QCOMPARE(r.shown(), QStringList());
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c }));
}

void LibraryShellTest::scan()
{
    // SH-COMMANDS: Scan Folders
    Rig r;
    r.start();
    NEED_PARTS(r);
    r.run(u"file.scanFolders"); // the user cancels the chooser
    QVERIFY2(r.shell->scanner(), "the shell has no scan runner");
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCOMPARE(r.asked, 1);
    QCOMPARE(r.library->count(), 0);
    QCOMPARE(r.shell->lastMessage(), QString());
    r.folders = { r.dir.path() + s(u"/music") };
    r.run(u"file.scanFolders");
    QVERIFY2(r.shell->scanner(), "the shell has no scan runner");
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCOMPARE(r.asked, 2);
    QCOMPARE(r.library->count(), 3);
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c }));
    QCOMPARE(r.shell->lastMessage(), s(u"Scan: 3 added, 0 updated, 0 missing, 1 not audio"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.shell->lastMessage()); // SH-MESSAGE
    QVERIFY(r.navigatorRows().contains(s(u"Music (3)")));
    const auto t = r.library->trackByPath(r.a);
    QVERIFY(t.has_value());
    QCOMPARE(t->category, s(u"Music"));
    QCOMPARE(t->dateAdded, r.now);
    // Again, after a file has gone.
    QVERIFY(QFile::remove(r.c));
    r.run(u"file.scanFolders");
    QVERIFY2(r.shell->scanner(), "the shell has no scan runner");
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCOMPARE(r.shell->lastMessage(), s(u"Scan: 0 added, 0 updated, 1 missing, 1 not audio"));
    QCOMPARE(r.library->count(), 3); // a missing file stays known
}

void LibraryShellTest::scanIntoTheChosenSection()
{
    // SH-COMMANDS: new files go to the library section on show; elsewhere, to Music.
    Rig r;
    r.start();
    NEED_PARTS(r);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    r.folders = { r.dir.path() + s(u"/inbox") };
    r.run(u"file.scanFolders");
    QVERIFY2(r.shell->scanner(), "the shell has no scan runner");
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCOMPARE(r.shown(), QStringList({ r.d }));
    QCOMPARE(r.library->trackByPath(r.d)->category, s(u"Inbox"));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playing")));
    r.files = { r.a };
    r.run(u"file.addFiles");
    QCOMPARE(r.library->trackByPath(r.a)->category, s(u"Music"));
    const QStringList rows = r.navigatorRows();
    QVERIFY(rows.contains(s(u"Music (1)")));
    QVERIFY(rows.contains(s(u"Inbox (1)")));
}

void LibraryShellTest::addFiles()
{
    // SH-COMMANDS: Add Files
    Rig r;
    r.start();
    NEED_PARTS(r);
    r.run(u"file.addFiles"); // cancelled
    QCOMPARE(r.shell->lastMessage(), QString());
    r.files = { r.a, r.junk, r.dir.path() + s(u"/music/not-there.flac") };
    r.run(u"file.addFiles");
    QCOMPARE(r.shell->lastMessage(), s(u"Added 1 of 3 files"));
    QCOMPARE(r.shown(), QStringList({ r.a }));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.shell->lastMessage());
    r.files = { r.b, r.c };
    r.run(u"file.addFiles");
    QCOMPARE(r.shell->lastMessage(), s(u"Added 2 of 2 files"));
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c }));
}

void LibraryShellTest::followsTheLibrary()
{
    // SH-LIVE: whoever changes the library, the list and the counts follow.
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    const qint64 a = r.idOf(r.a);
    const qint64 d = r.idOf(r.d);
    QVERIFY(a != 0 && d != 0);
    QCOMPARE(r.library->setCategory({ d }, s(u"Music")), 1); // comes into view
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c, r.d }));
    QVERIFY(r.navigatorRows().contains(s(u"Music (4)")));
    QVERIFY(r.navigatorRows().contains(s(u"Inbox")));
    QCOMPARE(r.library->setCategory({ a }, s(u"Audiobooks")), 1); // leaves it
    QCOMPARE(r.shown(), sorted({ r.b, r.c, r.d }));
    QVERIFY(r.navigatorRows().contains(s(u"Audiobooks (1)")));
    QCOMPARE(r.library->remove({ d }), 1);
    QCOMPARE(r.shown(), sorted({ r.b, r.c }));
    QVERIFY(r.navigatorRows().contains(s(u"Music (2)")));
    const QString e = r.put("basic.opus", "music/e.opus");
    QVERIFY(r.library->addFile(e, s(u"Music"), r.now) != 0);
    QCOMPARE(r.shown(), sorted({ r.b, r.c, e }));
    QVERIFY(r.library->addFile(r.d, s(u"Podcasts"), r.now) != 0); // another section: not shown here
    QCOMPARE(r.shown(), sorted({ r.b, r.c, e }));
    QVERIFY(r.navigatorRows().contains(s(u"Podcasts (1)")));
    // The selection survives changes that do not touch it.
    r.shell->trackList()->selectTracks({ r.idOf(r.b) });
    QVERIFY(r.library->refresh(r.idOf(r.c)));
    QCOMPARE(r.shell->selectedPaths(), QStringList({ r.b }));
}

void LibraryShellTest::removeAndSendTo()
{
    // SH-COMMANDS: commands on the selection
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    r.run(u"file.remove"); // nothing selected
    r.run(u"track.sendToInbox");
    QCOMPARE(r.library->count(), 4);
    QCOMPARE(r.shell->lastMessage(), QString());
    r.shell->trackList()->selectTracks({ r.idOf(r.a), r.idOf(r.b) });
    QCOMPARE(sorted(r.shell->selectedPaths()), sorted({ r.a, r.b }));
    QCOMPARE(r.shell->selectedTrackIds().size(), 2);
    r.run(u"track.sendToInbox");
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 2 to Inbox"));
    QCOMPARE(r.library->trackByPath(r.a)->category, s(u"Inbox"));
    QCOMPARE(r.shown(), QStringList({ r.c }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    r.shell->trackList()->selectTracks({ r.idOf(r.a) });
    r.run(u"track.sendToAudiobooks");
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 1 to Audiobooks"));
    r.shell->trackList()->selectTracks({ r.idOf(r.b), r.idOf(r.d) });
    r.run(u"file.remove");
    QCOMPARE(r.shell->lastMessage(), s(u"Removed 2 from the library"));
    QCOMPARE(r.library->count(), 2);
    QCOMPARE(r.shown(), QStringList());
    QVERIFY(QFile::exists(r.b)); // removed from the library, not from the disk
    r.shell->trackList()->selectTracks({ });
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Audiobooks")));
    r.shell->trackList()->selectTracks({ r.idOf(r.a) });
    r.run(u"track.sendToMusic");
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 1 to Music"));
}

void LibraryShellTest::playlistNodes()
{
    // SH-NODE (playlists), SH-LIVE
    Rig r;
    r.fill();
    QVERIFY(!r.playlists.create(s(u"Old")).isEmpty());
    r.start();
    NEED_PARTS(r);
    QVERIFY(r.shell->navigator()->nodes().contains(s(u"playlist:Old"))); // there from the start
    const QString mix = r.playlists.create(s(u"Mix"), s(u"Rock"));
    QCOMPARE(mix, s(u"Rock/Mix"));
    QVERIFY(r.shell->navigator()->nodes().contains(s(u"playlist:Rock/Mix"))); // and when one appears
    // In the playlist: one row per library track; files the library does not know are passed over.
    QVERIFY(r.playlists.setTracks(mix, { r.c, r.a, r.dir.path() + s(u"/elsewhere.flac"), r.d }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:Rock/Mix")));
    QCOMPARE(r.shown(), sorted({ r.a, r.c, r.d })); // whatever section they are in
    QCOMPARE(r.playlists.append(mix, { r.b }), 1); // changed while on show
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c, r.d }));
    QVERIFY(r.playlists.setTracks(s(u"Old"), { r.a })); // another playlist: no effect here
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c, r.d }));
    QCOMPARE(r.library->remove({ r.idOf(r.d) }), 1); // the library forgets a track
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c }));
    // The commands for adding still work from a playlist, into Music.
    QVERIFY(r.playlists.remove(mix)); // the playlist on show is deleted
    QCOMPARE(r.shell->currentNode(), s(u"library:Music"));
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c }));
    QVERIFY(!r.shell->navigator()->nodes().contains(s(u"playlist:Rock/Mix")));
}

void LibraryShellTest::playingNode()
{
    // SH-NODE: Playing Tracks shows what it was last told.
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    r.shell->setPlayingPaths({ r.c, r.d, r.dir.path() + s(u"/elsewhere.flac"), r.c }); // told while something else is on show
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playing")));
    QCOMPARE(r.shown(), sorted({ r.c, r.d }));
    r.shell->setPlayingPaths({ r.a }); // told while on show
    QCOMPARE(r.shown(), QStringList({ r.a }));
    QCOMPARE(r.library->remove({ r.idOf(r.a) }), 1);
    QCOMPARE(r.shown(), QStringList());
    r.shell->setPlayingPaths({ r.b, r.c });
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(r.shown(), sorted({ r.b, r.c }));
    r.shell->setPlayingPaths({ });
    QVERIFY(r.shell->navigator()->selectNode(s(u"playing")));
    QCOMPARE(r.shown(), QStringList());
}

void LibraryShellTest::activation()
{
    // SH-PLAY
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    QSignalSpy play(r.shell.get(), &LibraryShell::playRequested);
    const QStringList shown = r.shell->shownPaths();
    QCOMPARE(shown.size(), 3);
    emit r.shell->trackList()->trackActivated(r.idOf(shown.at(2)));
    QCOMPARE(play.size(), 1);
    QCOMPARE(play.at(0).at(0).toStringList(), shown);
    QCOMPARE(play.at(0).at(1).toInt(), 2);
    emit r.shell->trackList()->trackActivated(r.idOf(r.d)); // not in this list
    emit r.shell->trackList()->trackActivated(987654);
    QCOMPARE(play.size(), 1);
}

void LibraryShellTest::searchAndFind()
{
    // SH-NODE: a new node starts with an empty search. SH-COMMANDS: Find.
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    r.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&r.window));
    r.shell->trackList()->setSearchText(s(u"no such track anywhere"));
    QCOMPARE(r.shown(), QStringList());
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QCOMPARE(r.shell->trackList()->searchText(), QString());
    QCOMPARE(r.shown(), QStringList({ r.d }));
    auto *box = r.window.findChild<QLineEdit *>(s(u"searchBox"));
    QVERIFY(box);
    r.shell->navigator()->setFocus();
    QVERIFY(!box->hasFocus());
    r.run(u"edit.find");
    QVERIFY(box->hasFocus());
    // Refresh reads everything again.
    r.shell->trackList()->setSearchText(s(u"zzzz"));
    r.run(u"view.refresh");
    QCOMPARE(r.shell->trackList()->searchText(), s(u"zzzz")); // the search is the user's; it stays
    r.shell->trackList()->setSearchText(QString());
    QCOMPARE(r.shown(), QStringList({ r.d }));
}

void LibraryShellTest::stored()
{
    // SH-STORE
    Rig r;
    r.fill();
    QVERIFY(!r.playlists.create(s(u"Mix")).isEmpty());
    QVERIFY(r.playlists.setTracks(s(u"Mix"), { r.b }));
    r.start();
    NEED_PARTS(r);
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:Mix")));
    QVERIFY(r.shell->model()->setColumns({ s(u"Title"), s(u"Filename") }));
    r.shell->saveState();
    r.settings.sync();
    QCOMPARE(r.settings.value(s(u"shell/node")).toString(), s(u"playlist:Mix"));
    r.shell.reset();
    QSettings later(nextRun(r.settings), QSettings::IniFormat); // as the next run reads it
    {
        ActionRegistry actions;
        MainWindow window(&actions, &later);
        LibraryShell again(&window, &actions, r.library.get(), &r.playlists, &later, { });
        QCOMPARE(again.currentNode(), s(u"playlist:Mix"));
        QCOMPARE(again.shownPaths(), QStringList({ r.b }));
        QCOMPARE(again.model()->columns(), QStringList({ s(u"Title"), s(u"Filename") }));
        // With no hooks the two choosing commands do nothing, quietly.
        actions.action(s(u"file.scanFolders"))->trigger();
        actions.action(s(u"file.addFiles"))->trigger();
        QCOMPARE(again.lastMessage(), QString());
    }
    // A stored node that is gone: the music library.
    QVERIFY(r.playlists.remove(s(u"Mix")));
    ActionRegistry actions;
    MainWindow window(&actions, &later);
    LibraryShell again(&window, &actions, r.library.get(), &r.playlists, &later, { });
    QCOMPARE(again.currentNode(), s(u"library:Music"));
    QCOMPARE(sorted(again.shownPaths()), sorted({ r.a, r.b, r.c }));
}

void LibraryShellTest::extending()
{
    // SH-EXTEND
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    const auto ids = [](const QList<LibraryTrack> &tracks) {
        QList<qint64> out;
        for (const LibraryTrack &t : tracks)
            out.append(t.id);
        std::sort(out.begin(), out.end());
        return out;
    };
    const QList<qint64> music = { r.idOf(r.a), r.idOf(r.b), r.idOf(r.c) };
    QList<qint64> sortedMusic = music;
    std::sort(sortedMusic.begin(), sortedMusic.end());

    // The node's tracks, and the signal.
    QCOMPARE(ids(r.shell->nodeTracks()), sortedMusic);
    QSignalSpy changed(r.shell.get(), &LibraryShell::nodeTracksChanged);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QVERIFY(changed.size() >= 1);
    QCOMPARE(ids(r.shell->nodeTracks()), QList<qint64>({ r.idOf(r.d) }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));

    // Narrowing: the list shrinks, nodeTracks() and the counts do not.
    const QString flac = s(u"flac");
    r.shell->setNarrowing([flac](const LibraryTrack &t) { return t.tags.format == flac; });
    QCOMPARE(r.shown(), QStringList({ r.a }));
    QCOMPARE(ids(r.shell->nodeTracks()), sortedMusic);
    QVERIFY(r.navigatorRows().contains(s(u"Music (3)")));
    // It holds across nodes,
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QCOMPARE(r.shown(), QStringList({ r.d }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(r.shown(), QStringList({ r.a }));
    // and while the library changes: an arrival is listed only if it is kept.
    changed.clear();
    const QString e = r.put("basic.m4a", "music/e.m4a");
    const QString f = r.put("basic.flac", "music/f.flac");
    QVERIFY(r.library->addFile(e, s(u"Music"), r.now) != 0);
    QVERIFY(r.library->addFile(f, s(u"Music"), r.now) != 0);
    QCOMPARE(r.shown(), sorted({ r.a, f }));
    QCOMPARE(r.shell->nodeTracks().size(), 5);
    QVERIFY(changed.size() >= 1);
    QVERIFY(r.library->remove({ r.idOf(f) }) == 1);
    QCOMPARE(r.shown(), QStringList({ r.a }));
    QCOMPARE(r.shell->nodeTracks().size(), 4);
    // The search box narrows what is left.
    r.shell->trackList()->setSearchText(s(u"no such thing anywhere"));
    QCOMPARE(r.shown(), QStringList());
    r.shell->trackList()->setSearchText(QString());
    // Ended.
    r.shell->setNarrowing({ });
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c, e }));

    // The browser.
    auto *split = r.window.findChild<QSplitter *>(s(u"browserSplitter"));
    QVERIFY2(split, "the page is not a splitter named browserSplitter");
    QCOMPARE(split->orientation(), Qt::Horizontal);
    QCOMPARE(split->count(), 1);
    QVERIFY(split->isAncestorOf(r.shell->trackList()));
    QVERIFY(r.shell->browser() == nullptr);
    QPointer<QWidget> first(new QLabel(s(u"first")));
    r.shell->setBrowser(first);
    QCOMPARE(r.shell->browser(), first.data());
    QCOMPARE(split->count(), 2);
    QCOMPARE(split->widget(0), first.data()); // to the left
    QPointer<QWidget> second(new QLabel(s(u"second")));
    r.shell->setBrowser(second);
    QVERIFY(first.isNull()); // at once
    QCOMPARE(split->widget(0), second.data());
    r.shell->setBrowser(second); // the same again: nothing
    QVERIFY(!second.isNull());
    r.shell->setBrowser(nullptr);
    QVERIFY(second.isNull());
    QCOMPARE(split->count(), 1);
    QPointer<QWidget> last(new QLabel(s(u"last")));
    r.shell->setBrowser(last);

    // Node sources: asked first, in order; the first true wins.
    int askedFirst = 0, askedSecond = 0;
    QList<qint64> give = { r.idOf(r.a) };
    r.shell->addNodeSource([&](const QString &node, QList<LibraryTrack> *tracks) {
        ++askedFirst;
        if (node != s(u"library:Podcasts"))
            return false;
        for (qint64 id : give)
            if (const auto t = r.library->track(id))
                tracks->append(*t);
        return true;
    });
    r.shell->addNodeSource([&](const QString &node, QList<LibraryTrack> *tracks) {
        ++askedSecond;
        if (node != s(u"library:Podcasts") && node != s(u"library:Audiobooks"))
            return false;
        if (const auto t = r.library->track(r.idOf(r.b)))
            tracks->append(*t);
        return true;
    });
    r.shell->addNodeSource({ }); // nothing
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c, e })); // Music: no source answers
    QVERIFY(askedFirst >= 1 && askedSecond >= 1);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Podcasts")));
    QCOMPARE(r.shown(), QStringList({ r.a })); // the first source
    QCOMPARE(ids(r.shell->nodeTracks()), QList<qint64>({ r.idOf(r.a) }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Audiobooks")));
    QCOMPARE(r.shown(), QStringList({ r.b })); // the second
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Podcasts")));
    // The source's answer changed: reloadNode reads it, and the search text stays.
    r.shell->trackList()->setSearchText(s(u"title"));
    give = { r.idOf(r.a), r.idOf(r.c) };
    changed.clear();
    r.shell->reloadNode();
    QCOMPARE(ids(r.shell->nodeTracks()), QList<qint64>({ qMin(r.idOf(r.a), r.idOf(r.c)), qMax(r.idOf(r.a), r.idOf(r.c)) }));
    QVERIFY(changed.size() >= 1);
    QCOMPARE(r.shell->trackList()->searchText(), s(u"title"));
    r.shell->trackList()->setSearchText(QString());
    // A sourced node is read again when the library changes.
    give = { r.idOf(r.a) };
    QVERIFY(r.library->setCategory({ r.idOf(r.d) }, s(u"Music")) == 1);
    QCOMPARE(r.shown(), QStringList({ r.a }));
    QCOMPARE(r.shell->browser(), last.data());
}

QTEST_MAIN(LibraryShellTest)
#include "tst_ui_libraryshell.moc"
