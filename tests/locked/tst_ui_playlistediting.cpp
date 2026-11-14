// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/playlist-editing.md (PE-x) and in
// docs/spec/navigator.md (V-MENU). Runs on the offscreen platform, with a
// real playlist store in a temporary folder, the real window and the real
// library shell.
#include <QContextMenuEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "shell/libraryshell.h"
#include "shell/playlistediting.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

struct Rig {
    QTemporaryDir dir;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    std::unique_ptr<LibraryShell> shell;
    std::unique_ptr<PlaylistEditing> editing;

    // What the user will answer, and what was asked.
    std::optional<QString> name;
    std::optional<QString> folder;
    bool yes = true;
    QString menuChoice;
    QStringList asked; // "name:TITLE:INITIAL", "folder:A|B", "confirm:QUESTION", "menu:a,b|La,Lb"

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        playlists.create(s(u"Morning"));
        playlists.create(s(u"Best of"), s(u"Rock"));
        playlists.create(s(u"Sets"), s(u"Rock/Live"));
        shell = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        editing = std::make_unique<PlaylistEditing>(&window, &actions, shell.get(), &playlists, hooks());
    }
    QString made(const QString &sub)
    {
        QDir(dir.path()).mkpath(sub);
        return dir.path() + QLatin1Char('/') + sub;
    }
    PlaylistEditing::Hooks hooks()
    {
        PlaylistEditing::Hooks h;
        h.askName = [this](const QString &title, const QString &initial) {
            asked.append(QStringLiteral("name:") + title + QLatin1Char(':') + initial);
            return name;
        };
        h.chooseFolder = [this](const QStringList &folders) {
            asked.append(QStringLiteral("folder:") + folders.join(QLatin1Char('|')));
            return folder;
        };
        h.confirm = [this](const QString &question) {
            asked.append(QStringLiteral("confirm:") + question);
            return yes;
        };
        h.showMenu = [this](const QStringList &commands, const QStringList &labels, const QPoint &) {
            asked.append(QStringLiteral("menu:") + commands.join(QLatin1Char(',')) + QLatin1Char('|') + labels.join(QLatin1Char(',')));
            return menuChoice;
        };
        return h;
    }
    QStringList ids() const
    {
        QStringList out;
        for (const PlaylistInfo &p : playlists.playlists())
            out.append(p.id);
        return out;
    }
    QString status() { return window.statusBar()->currentMessage(); }
    // The middle of a navigator row, in the tree's viewport; null point when the row is not there.
    QPoint rowPoint(const QString &rowId)
    {
        auto *tree = window.findChild<QTreeWidget *>(s(u"navigatorTree"));
        NavigatorPanel *nav = shell->navigator();
        if (!tree || !nav)
            return { };
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            const QPoint middle = tree->visualItemRect(*it).center();
            if (nav->rowAt(middle) == rowId)
                return middle;
        }
        return { };
    }
    bool rightClick(const QString &rowId)
    {
        auto *tree = window.findChild<QTreeWidget *>(s(u"navigatorTree"));
        const QPoint at = rowPoint(rowId);
        if (!tree || at.isNull())
            return false;
        QContextMenuEvent event(QContextMenuEvent::Mouse, at, tree->viewport()->mapToGlobal(at));
        QCoreApplication::sendEvent(tree->viewport(), &event);
        return true;
    }
};

#define NEED_PARTS(r) \
    do { \
        if (!(r).shell->navigator() || (r).editing->commandsFor(s(u"playlists")).isEmpty()) \
            QFAIL("the playlist commands have not been built"); \
    } while (false)

}

class PlaylistEditingTest : public QObject {
    Q_OBJECT
private slots:
    void rowIds();
    void menuSignal();
    void commands();
    void create();
    void createRefused();
    void rename();
    void move();
    void remove();
    void fromTheMenu();
    void fromTheFileMenu();
    void gone();
};

void PlaylistEditingTest::rowIds()
{
    // V-MENU: ids of rows, by the navigator alone.
    NavigatorPanel nav;
    nav.resize(300, 600);
    nav.setPlaylists({ s(u"Morning"), s(u"Rock/Best of"), s(u"Rock/Live/Sets") });
    QCOMPARE(nav.rows(), QStringList({ s(u"playing"), s(u"library:Music"), s(u"library:Inbox"), s(u"library:Audiobooks"),
                             s(u"library:Podcasts"), s(u"library:Videos"), s(u"playlists"), s(u"playlist:Morning"), s(u"folder:Rock"),
                             s(u"playlist:Rock/Best of"), s(u"folder:Rock/Live"), s(u"playlist:Rock/Live/Sets") }));
    auto *tree = nav.findChild<QTreeWidget *>(s(u"navigatorTree"));
    QVERIFY(tree);
    QStringList byPoint;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        byPoint.append(nav.rowAt(tree->visualItemRect(*it).center()));
    QStringList want = nav.rows();
    want.insert(1, QString()); // the Library heading has no id
    QCOMPARE(byPoint, want);
    QCOMPARE(nav.rowAt(QPoint(5, 5000)), QString()); // below every row
}

void PlaylistEditingTest::menuSignal()
{
    // V-MENU: the signal, and that a right click chooses nothing.
    NavigatorPanel nav;
    nav.resize(300, 600);
    nav.setPlaylists({ s(u"Morning"), s(u"Rock/Best of") });
    auto *tree = nav.findChild<QTreeWidget *>(s(u"navigatorTree"));
    QVERIFY(tree);
    QSignalSpy menus(&nav, &NavigatorPanel::menuRequested);
    QSignalSpy chosen(&nav, &NavigatorPanel::nodeSelected);
    const auto ask = [&](const QString &text) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->text(0) != text)
                continue;
            const QPoint at = tree->visualItemRect(*it).center();
            QContextMenuEvent event(QContextMenuEvent::Mouse, at, tree->viewport()->mapToGlobal(at));
            QCoreApplication::sendEvent(tree->viewport(), &event);
            return tree->viewport()->mapToGlobal(at);
        }
        return QPoint(-1, -1);
    };
    const QPoint where = ask(s(u"Morning"));
    QCOMPARE(menus.size(), 1);
    QCOMPARE(menus.at(0).at(0).toString(), s(u"playlist:Morning"));
    QCOMPARE(menus.at(0).at(1).toPoint(), where);
    ask(s(u"Rock"));
    QCOMPARE(menus.size(), 2);
    QCOMPARE(menus.at(1).at(0).toString(), s(u"folder:Rock"));
    ask(s(u"Playlists"));
    QCOMPARE(menus.at(2).at(0).toString(), s(u"playlists"));
    ask(s(u"Inbox"));
    QCOMPARE(menus.at(3).at(0).toString(), s(u"library:Inbox"));
    ask(s(u"Library")); // no id: nothing
    QCOMPARE(menus.size(), 4);
    QContextMenuEvent nowhere(QContextMenuEvent::Mouse, QPoint(5, 550), tree->viewport()->mapToGlobal(QPoint(5, 550)));
    QCoreApplication::sendEvent(tree->viewport(), &nowhere);
    QCOMPARE(menus.size(), 4);
    QCOMPARE(chosen.size(), 0);
    QCOMPARE(nav.currentNode(), s(u"library:Music"));
}

void PlaylistEditingTest::commands()
{
    // PE-MENU
    Rig r;
    NEED_PARTS(r);
    QCOMPARE(r.editing->commandsFor(s(u"playlists")), QStringList({ s(u"new") }));
    QCOMPARE(r.editing->commandsFor(s(u"folder:Rock")), QStringList({ s(u"new") }));
    QCOMPARE(r.editing->commandsFor(s(u"playlist:Morning")), QStringList({ s(u"rename"), s(u"move"), s(u"delete") }));
    QCOMPARE(r.editing->commandsFor(s(u"playlist:Rock/Live/Sets")).size(), 3);
    for (const char16_t *row : { u"playlist:No Such", u"playlist:", u"library:Music", u"playing", u"folder:", u"", u"auto:Morning" })
        QVERIFY2(r.editing->commandsFor(s(row)).isEmpty(), qPrintable(s(row)));
    QCOMPARE(PlaylistEditing::label(s(u"new")), s(u"New Playlist\u2026"));
    QCOMPARE(PlaylistEditing::label(s(u"rename")), s(u"Rename\u2026"));
    QCOMPARE(PlaylistEditing::label(s(u"move")), s(u"Move to Folder\u2026"));
    QCOMPARE(PlaylistEditing::label(s(u"delete")), s(u"Delete"));
    QCOMPARE(PlaylistEditing::label(s(u"explode")), QString());
    // A command the row does not offer does nothing and asks nothing.
    QVERIFY(!r.editing->run(s(u"delete"), s(u"playlists")));
    QVERIFY(!r.editing->run(s(u"new"), s(u"playlist:Morning")));
    QVERIFY(!r.editing->run(s(u"rename"), s(u"playlist:No Such")));
    QVERIFY(!r.editing->run(s(u"explode"), s(u"playlist:Morning")));
    QCOMPARE(r.asked, QStringList());
    QCOMPARE(r.ids().size(), 3);
    QCOMPARE(r.editing->lastMessage(), QString());
}

void PlaylistEditingTest::create()
{
    // PE-COMMANDS, new
    Rig r;
    NEED_PARTS(r);
    r.name = s(u"Evening");
    QVERIFY(r.editing->run(s(u"new"), s(u"playlists")));
    QCOMPARE(r.asked, QStringList({ s(u"name:New Playlist:New Playlist") }));
    QVERIFY(r.playlists.exists(s(u"Evening")));
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"playlist:Evening")); // chosen
    QCOMPARE(r.editing->lastMessage(), s(u"Created the playlist \"Evening\""));
    QCOMPARE(r.status(), r.editing->lastMessage());
    // In a folder.
    r.name = s(u"Loud");
    QVERIFY(r.editing->run(s(u"new"), s(u"folder:Rock/Live")));
    QVERIFY(r.playlists.exists(s(u"Rock/Live/Loud")));
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"playlist:Rock/Live/Loud"));
    QCOMPARE(r.editing->lastMessage(), s(u"Created the playlist \"Rock/Live/Loud\""));
    // Giving up: nothing, and the message stays.
    r.name = std::nullopt;
    QVERIFY(!r.editing->run(s(u"new"), s(u"playlists")));
    QCOMPARE(r.ids().size(), 5);
    QCOMPARE(r.editing->lastMessage(), s(u"Created the playlist \"Rock/Live/Loud\""));
}

void PlaylistEditingTest::createRefused()
{
    // PE-MESSAGE: the store's reason is passed on.
    Rig r;
    NEED_PARTS(r);
    QString reason;
    QVERIFY(PlaylistStore(r.playlists.folder()).create(s(u"morning"), QString(), &reason).isEmpty());
    QVERIFY(!reason.isEmpty());
    r.name = s(u"morning"); // differs from "Morning" only by case
    QVERIFY(!r.editing->run(s(u"new"), s(u"playlists")));
    QCOMPARE(r.editing->lastMessage(), s(u"Could not create the playlist: ") + reason);
    QCOMPARE(r.status(), r.editing->lastMessage());
    QCOMPARE(r.ids().size(), 3);
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"library:Music"));
    r.name = s(u"a/b");
    QVERIFY(!r.editing->run(s(u"new"), s(u"playlists")));
    QVERIFY(r.editing->lastMessage().startsWith(s(u"Could not create the playlist: ")));
    QVERIFY(r.editing->lastMessage().size() > 32);
    QCOMPARE(r.ids().size(), 3);
}

void PlaylistEditingTest::rename()
{
    // PE-COMMANDS, rename
    Rig r;
    NEED_PARTS(r);
    QVERIFY(r.playlists.setTracks(s(u"Rock/Best of"), { s(u"/music/a.flac"), s(u"/music/b.flac") }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:Rock/Best of")));
    r.name = s(u"Greatest");
    QVERIFY(r.editing->run(s(u"rename"), s(u"playlist:Rock/Best of")));
    QCOMPARE(r.asked, QStringList({ s(u"name:Rename Playlist:Best of") })); // the name, not the id
    QVERIFY(!r.playlists.exists(s(u"Rock/Best of")));
    QCOMPARE(r.playlists.tracks(s(u"Rock/Greatest")).size(), 2); // the folder is kept, and the tracks
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"playlist:Rock/Greatest")); // it was the choice: still is
    QCOMPARE(r.editing->lastMessage(), s(u"Renamed the playlist to \"Rock/Greatest\""));
    QCOMPARE(r.status(), r.editing->lastMessage());
    // One that is not the choice: the choice stays where it is.
    r.name = s(u"Dawn");
    QVERIFY(r.editing->run(s(u"rename"), s(u"playlist:Morning")));
    QVERIFY(r.playlists.exists(s(u"Dawn")));
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"playlist:Rock/Greatest"));
    // Refused.
    r.name = s(u"greatest ");
    QVERIFY(!r.editing->run(s(u"rename"), s(u"playlist:Dawn")));
    QVERIFY(r.editing->lastMessage().startsWith(s(u"Could not rename the playlist: ")));
    QVERIFY(r.playlists.exists(s(u"Dawn")));
    r.name = std::nullopt;
    const QString before = r.editing->lastMessage();
    QVERIFY(!r.editing->run(s(u"rename"), s(u"playlist:Dawn")));
    QCOMPARE(r.editing->lastMessage(), before);
}

void PlaylistEditingTest::move()
{
    // PE-COMMANDS, move
    Rig r;
    NEED_PARTS(r);
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:Morning")));
    r.folder = s(u"Rock/Live");
    QVERIFY(r.editing->run(s(u"move"), s(u"playlist:Morning")));
    QCOMPARE(r.asked, QStringList({ s(u"folder:Rock|Rock/Live") }));
    QVERIFY(r.playlists.exists(s(u"Rock/Live/Morning")));
    QVERIFY(!r.playlists.exists(s(u"Morning")));
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"playlist:Rock/Live/Morning"));
    QCOMPARE(r.editing->lastMessage(), s(u"Moved the playlist to \"Rock/Live/Morning\""));
    QCOMPARE(r.status(), r.editing->lastMessage());
    // To the top.
    r.folder = QString();
    QVERIFY(r.editing->run(s(u"move"), s(u"playlist:Rock/Best of")));
    QVERIFY(r.playlists.exists(s(u"Best of")));
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"playlist:Rock/Live/Morning")); // not the one moved
    // Into a folder that is not there yet.
    r.folder = s(u"Quiet/Late");
    QVERIFY(r.editing->run(s(u"move"), s(u"playlist:Best of")));
    QVERIFY(r.playlists.exists(s(u"Quiet/Late/Best of")));
    QVERIFY(r.playlists.folders().contains(s(u"Quiet/Late")));
    // Refused: a folder name that cannot be.
    r.folder = s(u".hidden");
    QVERIFY(!r.editing->run(s(u"move"), s(u"playlist:Quiet/Late/Best of")));
    QVERIFY(r.editing->lastMessage().startsWith(s(u"Could not move the playlist: ")));
    QVERIFY(r.playlists.exists(s(u"Quiet/Late/Best of")));
    QVERIFY(!QDir(r.playlists.folder() + s(u"/.hidden")).exists());
    r.folder = std::nullopt;
    QVERIFY(!r.editing->run(s(u"move"), s(u"playlist:Quiet/Late/Best of")));
}

void PlaylistEditingTest::remove()
{
    // PE-COMMANDS, delete
    Rig r;
    NEED_PARTS(r);
    r.yes = false;
    QVERIFY(!r.editing->run(s(u"delete"), s(u"playlist:Rock/Best of")));
    QCOMPARE(r.asked, QStringList({ s(u"confirm:Delete the playlist \"Rock/Best of\"? Its tracks stay in the library.") }));
    QVERIFY(r.playlists.exists(s(u"Rock/Best of")));
    QCOMPARE(r.editing->lastMessage(), QString());
    r.yes = true;
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:Rock/Best of")));
    QVERIFY(r.editing->run(s(u"delete"), s(u"playlist:Rock/Best of")));
    QVERIFY(!r.playlists.exists(s(u"Rock/Best of")));
    QCOMPARE(r.editing->lastMessage(), s(u"Deleted the playlist \"Rock/Best of\""));
    QCOMPARE(r.status(), r.editing->lastMessage());
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"library:Music")); // navigator.md, V-PLAYLISTS
    QVERIFY(!r.shell->navigator()->nodes().contains(s(u"playlist:Rock/Best of")));
}

void PlaylistEditingTest::fromTheMenu()
{
    // PE-WIRING: a right click on the navigator.
    Rig r;
    NEED_PARTS(r);
    r.window.resize(1000, 700);
    r.menuChoice = QString(); // the menu is closed without a choice
    QVERIFY(r.rightClick(s(u"playlist:Morning")));
    QCOMPARE(r.asked, QStringList({ s(u"menu:rename,move,delete|Rename\u2026,Move to Folder\u2026,Delete") }));
    QCOMPARE(r.ids().size(), 3);
    r.asked.clear();
    r.menuChoice = s(u"delete");
    QVERIFY(r.rightClick(s(u"playlist:Morning")));
    QCOMPARE(r.asked.size(), 2); // the menu, then the question
    QVERIFY(!r.playlists.exists(s(u"Morning")));
    r.asked.clear();
    r.menuChoice = s(u"new");
    r.name = s(u"Louder");
    QVERIFY(r.rightClick(s(u"folder:Rock")));
    QCOMPARE(r.asked.value(0), s(u"menu:new|New Playlist\u2026"));
    QVERIFY(r.playlists.exists(s(u"Rock/Louder")));
    r.asked.clear();
    QVERIFY(r.rightClick(s(u"playlists")));
    QCOMPARE(r.asked.value(0), s(u"menu:new|New Playlist\u2026"));
    // A row with no commands shows no menu.
    r.asked.clear();
    QVERIFY(r.rightClick(s(u"library:Inbox")));
    QVERIFY(r.rightClick(s(u"playing")));
    QCOMPARE(r.asked, QStringList());
}

void PlaylistEditingTest::fromTheFileMenu()
{
    // PE-WIRING: File > Playlists > New Playlist
    Rig r;
    NEED_PARTS(r);
    QVERIFY(r.actions.isImplemented(s(u"file.playlists.new")));
    r.name = s(u"From The Menu");
    r.actions.action(s(u"file.playlists.new"))->trigger();
    QCOMPARE(r.asked, QStringList({ s(u"name:New Playlist:New Playlist") }));
    QVERIFY(r.playlists.exists(s(u"From The Menu")));
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"playlist:From The Menu"));
}

void PlaylistEditingTest::gone()
{
    // PE-WIRING: destroyed, it leaves nothing behind.
    Rig r;
    NEED_PARTS(r);
    r.editing.reset();
    QVERIFY(!r.actions.isImplemented(s(u"file.playlists.new")));
    r.window.resize(1000, 700);
    QVERIFY(r.rightClick(s(u"playlist:Morning")));
    QCOMPARE(r.asked, QStringList());
    // And made again, it works again.
    r.editing = std::make_unique<PlaylistEditing>(&r.window, &r.actions, r.shell.get(), &r.playlists, r.hooks());
    QVERIFY(r.actions.isImplemented(s(u"file.playlists.new")));
}

QTEST_MAIN(PlaylistEditingTest)
#include "tst_ui_playlistediting.moc"
