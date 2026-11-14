// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/auto-playlist-nodes.md (AN-x) and
// docs/spec/navigator.md (V-AUTO). Runs on the offscreen platform, with a
// real library (in memory), a real store in a temporary folder, the real
// window and the real library shell.
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
#include "playlists/autoplayliststore.h"
#include "shell/autoplaylistnodes.h"
#include "shell/libraryshell.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

AutoPlaylist titled(const char16_t *part)
{
    AutoPlaylist p;
    Rule r;
    r.field = s(u"Title");
    r.op = RuleOperator::Contains;
    r.values = { s(part) };
    p.rules.rules = { r };
    return p;
}

AutoPlaylist oneAtRandom()
{
    AutoPlaylist p; // no rules: every track
    p.limitEnabled = true;
    p.limitAmount = 1;
    p.limitUnit = AutoPlaylist::LimitUnit::Items;
    p.selectedBy = AutoPlaylist::SelectedBy::Random;
    return p;
}

struct Rig {
    QTemporaryDir dir;
    QString mp3, flac, m4a, ogg, inboxOgg;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    AutoPlaylistStore store;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;
    std::unique_ptr<AutoPlaylistNodes> nodes;
    quint32 seed = 7;
    int seedAsked = 0;
    bool yes = true;
    QString menuChoice;
    QStringList asked;

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , store(dir.path() + QStringLiteral("/auto"))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        mp3 = put("basic.mp3", "music/a.mp3"); // Title: Someone like You
        flac = put("basic.flac", "music/b.flac"); // Flac Title
        m4a = put("basic.m4a", "music/c.m4a"); // M4A Title
        ogg = put("basic.ogg", "music/d.ogg"); // Ogg Title
        inboxOgg = put("basic.ogg", "music/e.ogg"); // Ogg Title, in the Inbox
        for (const QString &p : { mp3, flac, m4a, ogg })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        if (library->addFile(inboxOgg, QStringLiteral("Inbox"), now) == 0)
            QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        playlists.create(s(u"Morning"));
        shell = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
    }
    void start() { nodes = std::make_unique<AutoPlaylistNodes>(&window, shell.get(), library.get(), &store, hooks()); }
    AutoPlaylistNodes::Hooks hooks()
    {
        AutoPlaylistNodes::Hooks h;
        h.now = [this] { return now; };
        h.seed = [this] {
            ++seedAsked;
            return seed;
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
    QString made(const QString &sub)
    {
        QDir(dir.path()).mkpath(sub);
        return dir.path() + QLatin1Char('/') + sub;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    QStringList shown() const
    {
        QStringList list = shell->shownPaths();
        list.sort();
        return list;
    }
    QString status() { return window.statusBar()->currentMessage(); }
    bool rightClick(const QString &rowId)
    {
        auto *tree = window.findChild<QTreeWidget *>(s(u"navigatorTree"));
        NavigatorPanel *nav = shell->navigator();
        if (!tree || !nav)
            return false;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            const QPoint at = tree->visualItemRect(*it).center();
            if (nav->rowAt(at) != rowId)
                continue;
            QContextMenuEvent event(QContextMenuEvent::Mouse, at, tree->viewport()->mapToGlobal(at));
            QCoreApplication::sendEvent(tree->viewport(), &event);
            return true;
        }
        return false;
    }
};

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

#define NEED_NODES(r) \
    do { \
        if (!(r).shell->navigator() || !(r).shell->navigator()->nodes().contains(s(u"auto:Titles"))) \
            QFAIL("the auto-playlist nodes have not been built"); \
    } while (false)

}

class AutoPlaylistNodesTest : public QObject {
    Q_OBJECT
private slots:
    void storeBasics();
    void storeNames();
    void storeRename();
    void storeSignals();
    void navigatorRows();
    void listed();
    void shows();
    void followsLibraryAndStore();
    void seedOnce();
    void broken();
    void deleting();
    void gone();
};

void AutoPlaylistNodesTest::storeBasics()
{
    // AN-STORE
    QTemporaryDir dir;
    AutoPlaylistStore store(dir.path() + s(u"/not/yet/there"));
    QCOMPARE(store.folder(), dir.path() + s(u"/not/yet/there"));
    QCOMPARE(store.names(), QStringList());
    QVERIFY(!store.exists(s(u"Titles")));
    QString error;
    AutoPlaylist got = oneAtRandom();
    QVERIFY(!store.load(s(u"Titles"), &got, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(autoPlaylistToJson(got), autoPlaylistToJson(oneAtRandom())); // left as it was
    QVERIFY(!store.remove(s(u"Titles")));

    QVERIFY(store.save(s(u"Titles"), titled(u"title"), &error)); // the folder is made
    QVERIFY(store.exists(s(u"Titles")));
    QVERIFY(!store.exists(s(u"titles"))); // exact
    QFile file(store.folder() + s(u"/Titles.lbauto"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), autoPlaylistToJson(titled(u"title")));
    file.close();
    QVERIFY(store.load(s(u"Titles"), &got, &error));
    QCOMPARE(autoPlaylistToJson(got), autoPlaylistToJson(titled(u"title")));
    // Saving again replaces.
    QVERIFY(store.save(s(u"Titles"), oneAtRandom()));
    QVERIFY(store.load(s(u"Titles"), &got));
    QCOMPARE(autoPlaylistToJson(got), autoPlaylistToJson(oneAtRandom()));
    QCOMPARE(QDir(store.folder()).entryList(QDir::Files | QDir::Hidden), QStringList({ s(u"Titles.lbauto") })); // nothing left over
    // A file that cannot be understood.
    QFile bad(store.folder() + s(u"/Bad.lbauto"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("{\"version\":2}");
    bad.close();
    QVERIFY(store.exists(s(u"Bad")));
    AutoPlaylist untouched = titled(u"x");
    error.clear();
    QVERIFY(!store.load(s(u"Bad"), &untouched, &error));
    QString why;
    AutoPlaylist scratch;
    QVERIFY(!autoPlaylistFromJson(s(u"{\"version\":2}"), &scratch, &why));
    QCOMPARE(error, why);
    QCOMPARE(autoPlaylistToJson(untouched), autoPlaylistToJson(titled(u"x")));
    QVERIFY(store.remove(s(u"Bad")));
    QVERIFY(!store.exists(s(u"Bad")));
}

void AutoPlaylistNodesTest::storeNames()
{
    // AN-STORE: which names, in which order, and which are refused.
    QTemporaryDir dir;
    AutoPlaylistStore store(dir.path());
    for (const char16_t *name : { u"zebra", u"Alpha", u"beta", u"\u00c9t\u00e9 90s", u"With.Dots" })
        QVERIFY2(store.save(s(name), oneAtRandom()), qPrintable(s(name)));
    // Other things in the folder are not looked at.
    for (const char *other : { "notes.txt", "Old.m3u", ".hidden.lbauto", "lbauto" }) {
        QFile f(dir.path() + QLatin1Char('/') + QLatin1String(other));
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    QVERIFY(QDir(dir.path()).mkdir(s(u"Folder.lbauto")));
    QCOMPARE(store.names(), QStringList({ s(u"Alpha"), s(u"beta"), s(u"With.Dots"), s(u"zebra"), s(u"\u00c9t\u00e9 90s") }));
    QString error;
    for (const char16_t *name : { u"", u" ", u" lead", u"trail ", u"a/b", u"a\\b", u".dot", u"tab\there" }) {
        error.clear();
        QVERIFY2(!store.save(s(name), oneAtRandom(), &error), qPrintable(s(name)));
        QCOMPARE(error, PlaylistStore::validateName(s(name)));
        QVERIFY(!error.isEmpty());
    }
    QVERIFY(!store.save(s(u"ALPHA"), oneAtRandom(), &error)); // only the case differs
    QCOMPARE(error, s(u"an auto-playlist named \"Alpha\" is already there"));
    QCOMPARE(store.names().size(), 5);
}

void AutoPlaylistNodesTest::storeRename()
{
    // AN-STORE, rename
    QTemporaryDir dir;
    AutoPlaylistStore store(dir.path());
    QVERIFY(store.save(s(u"One"), titled(u"one")));
    QVERIFY(store.save(s(u"Two"), titled(u"two")));
    QString error;
    QVERIFY(store.rename(s(u"One"), s(u"First"), &error));
    QCOMPARE(store.names(), QStringList({ s(u"First"), s(u"Two") }));
    AutoPlaylist got;
    QVERIFY(store.load(s(u"First"), &got));
    QCOMPARE(autoPlaylistToJson(got), autoPlaylistToJson(titled(u"one")));
    QVERIFY(store.rename(s(u"First"), s(u"FIRST"))); // its own name in another case
    QCOMPARE(store.names(), QStringList({ s(u"FIRST"), s(u"Two") }));
    QVERIFY(store.rename(s(u"FIRST"), s(u"FIRST"))); // the very same
    QVERIFY(!store.rename(s(u"FIRST"), s(u"two"), &error));
    QCOMPARE(error, s(u"an auto-playlist named \"Two\" is already there"));
    QVERIFY(!store.rename(s(u"FIRST"), s(u"a/b"), &error));
    QCOMPARE(error, PlaylistStore::validateName(s(u"a/b")));
    QVERIFY(!store.rename(s(u"Nobody"), s(u"Somebody"), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.names(), QStringList({ s(u"FIRST"), s(u"Two") }));
    QVERIFY(store.load(s(u"Two"), &got));
    QCOMPARE(autoPlaylistToJson(got), autoPlaylistToJson(titled(u"two")));
}

void AutoPlaylistNodesTest::storeSignals()
{
    // AN-STORE: what is announced.
    QTemporaryDir dir;
    AutoPlaylistStore store(dir.path());
    QSignalSpy list(&store, &AutoPlaylistStore::listChanged);
    QSignalSpy changed(&store, &AutoPlaylistStore::changed);
    QVERIFY(store.save(s(u"One"), titled(u"one")));
    QCOMPARE(list.size(), 1);
    QCOMPARE(changed.size(), 0);
    QVERIFY(store.save(s(u"One"), titled(u"other")));
    QCOMPARE(list.size(), 1);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(changed.at(0).at(0).toString(), s(u"One"));
    QVERIFY(store.rename(s(u"One"), s(u"Uno")));
    QCOMPARE(list.size(), 2);
    QVERIFY(store.rename(s(u"Uno"), s(u"Uno"))); // nothing changed
    QCOMPARE(list.size(), 2);
    QVERIFY(!store.save(s(u"a/b"), titled(u"x")));
    QVERIFY(!store.rename(s(u"Uno"), s(u".x")));
    QVERIFY(!store.remove(s(u"Nobody")));
    QCOMPARE(list.size(), 2);
    QVERIFY(store.remove(s(u"Uno")));
    QCOMPARE(list.size(), 3);
    QCOMPARE(changed.size(), 1);
}

void AutoPlaylistNodesTest::navigatorRows()
{
    // V-AUTO, by the navigator alone.
    NavigatorPanel nav;
    nav.setPlaylists({ s(u"Morning"), s(u"Rock/Best of") });
    QSignalSpy chosen(&nav, &NavigatorPanel::nodeSelected);
    nav.setAutoPlaylists({ s(u"Recently Added"), s(u" Top Rated "), s(u""), s(u"Recently Added"), s(u"A/B") });
    const QStringList fixed { s(u"playing"), s(u"library:Music"), s(u"library:Inbox"), s(u"library:Audiobooks"), s(u"library:Podcasts"),
        s(u"library:Videos") };
    QCOMPARE(nav.nodes(), fixed + QStringList({ s(u"auto:Recently Added"), s(u"auto:Top Rated"), s(u"auto:A/B"),
                                      s(u"playlist:Morning"), s(u"playlist:Rock/Best of") }));
    QVERIFY(nav.rows().contains(s(u"auto:A/B")));
    QVERIFY(!nav.rows().contains(s(u"folder:A"))); // a slash is part of the name
    auto *tree = nav.findChild<QTreeWidget *>(s(u"navigatorTree"));
    QVERIFY(tree);
    QStringList texts;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        texts.append((*it)->text(0));
    QCOMPARE(texts.mid(texts.indexOf(s(u"Playlists"))),
        QStringList({ s(u"Playlists"), s(u"Recently Added"), s(u"Top Rated"), s(u"A/B"), s(u"Morning"), s(u"Rock"), s(u"Best of") }));
    QCOMPARE(chosen.size(), 0);
    // Chosen like any other node.
    QVERIFY(nav.selectNode(s(u"auto:Top Rated")));
    QCOMPARE(nav.currentNode(), s(u"auto:Top Rated"));
    QCOMPARE(chosen.size(), 1);
    // The playlists are replaced: the auto-playlists stay, and so does the choice.
    nav.setPlaylists({ s(u"Evening") });
    QCOMPARE(nav.nodes(), fixed + QStringList({ s(u"auto:Recently Added"), s(u"auto:Top Rated"), s(u"auto:A/B"), s(u"playlist:Evening") }));
    QCOMPARE(nav.currentNode(), s(u"auto:Top Rated"));
    QCOMPARE(chosen.size(), 1);
    // The auto-playlists are replaced: the playlists stay.
    nav.setAutoPlaylists({ s(u"Top Rated") });
    QCOMPARE(nav.nodes(), fixed + QStringList({ s(u"auto:Top Rated"), s(u"playlist:Evening") }));
    QCOMPARE(chosen.size(), 1);
    // The chosen one goes: Music, and that is said.
    nav.setAutoPlaylists({ });
    QCOMPARE(nav.currentNode(), s(u"library:Music"));
    QCOMPARE(chosen.size(), 2);
    QCOMPARE(chosen.at(1).at(0).toString(), s(u"library:Music"));
    QCOMPARE(nav.nodes(), fixed + QStringList({ s(u"playlist:Evening") }));
}

void AutoPlaylistNodesTest::listed()
{
    // AN-NODES
    Rig r;
    QVERIFY(r.store.save(s(u"Titles"), titled(u"title")));
    QVERIFY(r.store.save(s(u"Everything"), AutoPlaylist()));
    r.start();
    NEED_NODES(r);
    const QStringList nodes = r.shell->navigator()->nodes();
    QCOMPARE(nodes.mid(nodes.indexOf(s(u"auto:Everything"))),
        QStringList({ s(u"auto:Everything"), s(u"auto:Titles"), s(u"playlist:Morning") }));
    // The list follows the store.
    QVERIFY(r.store.save(s(u"Later"), titled(u"x")));
    QVERIFY(r.shell->navigator()->nodes().contains(s(u"auto:Later")));
    QVERIFY(r.store.rename(s(u"Later"), s(u"Sooner")));
    QVERIFY(!r.shell->navigator()->nodes().contains(s(u"auto:Later")));
    QVERIFY(r.shell->navigator()->nodes().contains(s(u"auto:Sooner")));
    QVERIFY(r.store.remove(s(u"Sooner")));
    QVERIFY(!r.shell->navigator()->nodes().contains(s(u"auto:Sooner")));
    QCOMPARE(r.shown().size(), 4); // still on Music, untouched
}

void AutoPlaylistNodesTest::shows()
{
    // AN-SHOW
    Rig r;
    QVERIFY(r.store.save(s(u"Titles"), titled(u"title")));
    QVERIFY(r.store.save(s(u"Everything"), AutoPlaylist()));
    AutoPlaylist recent;
    Rule added;
    added.field = s(u"Date Added");
    added.op = RuleOperator::InTheLast;
    added.values = { s(u"1"), s(u"days") };
    recent.rules.rules = { added };
    QVERIFY(r.store.save(s(u"Recent"), recent));
    r.start();
    NEED_NODES(r);
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Titles")));
    // Every section is asked: the Inbox's track too. Not the one whose title has no "title".
    QCOMPARE(r.shown(), sorted({ r.flac, r.m4a, r.ogg, r.inboxOgg }));
    QCOMPARE(r.shell->nodeTracks().size(), 4);
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Everything")));
    QCOMPARE(r.shown().size(), 5);
    // "Now" is asked at the moment of showing.
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Recent")));
    QCOMPARE(r.shown().size(), 5); // all were added at r.now
    r.now = r.now.addDays(30);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Recent")));
    QCOMPARE(r.shown(), QStringList());
    QCOMPARE(r.status().contains(s(u"Cannot show")), false);
}

void AutoPlaylistNodesTest::followsLibraryAndStore()
{
    // AN-SHOW: read again when the library changes, and when the one shown is saved again.
    Rig r;
    QVERIFY(r.store.save(s(u"Titles"), titled(u"title")));
    QVERIFY(r.store.save(s(u"Other"), titled(u"flac")));
    r.start();
    NEED_NODES(r);
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Titles")));
    QCOMPARE(r.shown().size(), 4);
    const QString more = r.put("basic.flac", "music/f.flac");
    QVERIFY(r.library->addFile(more, s(u"Music"), r.now) != 0);
    QCOMPARE(r.shown(), sorted({ r.flac, r.m4a, r.ogg, r.inboxOgg, more }));
    const auto gone = r.library->trackByPath(r.m4a);
    QVERIFY(gone.has_value());
    QCOMPARE(r.library->remove({ gone->id }), 1);
    QCOMPARE(r.shown(), sorted({ r.flac, r.ogg, r.inboxOgg, more }));
    // Saved again with another rule.
    QVERIFY(r.store.save(s(u"Titles"), titled(u"someone")));
    QCOMPARE(r.shown(), QStringList({ r.mp3 }));
    // Another one saved again: the one shown is not disturbed.
    QSignalSpy reread(r.shell.get(), &LibraryShell::nodeTracksChanged);
    QVERIFY(r.store.save(s(u"Other"), titled(u"ogg")));
    QCOMPARE(reread.size(), 0);
    QCOMPARE(r.shown(), QStringList({ r.mp3 }));
}

void AutoPlaylistNodesTest::seedOnce()
{
    // AN-SHOW: one seed for the object's life.
    Rig r;
    QVERIFY(r.store.save(s(u"Titles"), titled(u"title")));
    QVERIFY(r.store.save(s(u"Lucky"), oneAtRandom()));
    r.start();
    NEED_NODES(r);
    QCOMPARE(r.seedAsked, 1);
    RuleContext context;
    context.now = r.now;
    const auto expected = [&](quint32 seed) {
        const QList<qint64> ids = evaluateAutoPlaylist(oneAtRandom(), r.library->tracks(), context, seed);
        QStringList paths;
        for (qint64 id : ids)
            if (const auto t = r.library->track(id))
                paths.append(t->path);
        return paths;
    };
    QCOMPARE(expected(7).size(), 1);
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Lucky")));
    QCOMPARE(r.shown(), expected(7));
    // Shown again, and again: the same one; the hook is not asked again.
    r.seed = 8;
    for (int i = 0; i < 5; ++i) {
        QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
        QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Lucky")));
        QCOMPARE(r.shown(), expected(7));
    }
    QCOMPARE(r.seedAsked, 1);
    // Some seed below 64 picks a different track than seed 7 does, so the
    // seed handed in is the one used.
    quint32 other = 0;
    while (other < 64 && expected(other) == expected(7))
        ++other;
    QVERIFY(other < 64);
    r.nodes.reset();
    r.seed = other;
    r.start();
    QCOMPARE(r.seedAsked, 2);
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Lucky")));
    QCOMPARE(r.shown(), expected(other));
}

void AutoPlaylistNodesTest::broken()
{
    // AN-SHOW: one that cannot be loaded.
    Rig r;
    QVERIFY(r.store.save(s(u"Titles"), titled(u"title")));
    QFile bad(r.store.folder() + s(u"/Broken.lbauto"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("not json at all");
    bad.close();
    r.start();
    NEED_NODES(r);
    QVERIFY(r.shell->navigator()->nodes().contains(s(u"auto:Broken")));
    QString why;
    AutoPlaylist scratch;
    QVERIFY(!r.store.load(s(u"Broken"), &scratch, &why));
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Broken")));
    QCOMPARE(r.shown(), QStringList());
    QCOMPARE(r.status(), s(u"Cannot show the auto-playlist \"Broken\": ") + why);
    // The others still work.
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Titles")));
    QCOMPARE(r.shown().size(), 4);
}

void AutoPlaylistNodesTest::deleting()
{
    // AN-MENU
    Rig r;
    QVERIFY(r.store.save(s(u"Titles"), titled(u"title")));
    QVERIFY(r.store.save(s(u"Everything"), AutoPlaylist()));
    r.start();
    NEED_NODES(r);
    r.window.resize(1000, 700);
    QCOMPARE(r.nodes->commandsFor(s(u"auto:Titles")), QStringList({ s(u"delete") }));
    for (const char16_t *row : { u"auto:Nobody", u"auto:", u"playlist:Morning", u"playlists", u"library:Music", u"" })
        QVERIFY2(r.nodes->commandsFor(s(row)).isEmpty(), qPrintable(s(row)));
    QCOMPARE(AutoPlaylistNodes::label(s(u"delete")), s(u"Delete"));
    QCOMPARE(AutoPlaylistNodes::label(s(u"rename")), QString());
    QVERIFY(!r.nodes->run(s(u"delete"), s(u"playlist:Morning")));
    QVERIFY(!r.nodes->run(s(u"rename"), s(u"auto:Titles")));
    QCOMPARE(r.asked, QStringList());

    r.yes = false;
    QVERIFY(!r.nodes->run(s(u"delete"), s(u"auto:Titles")));
    QCOMPARE(r.asked, QStringList({ s(u"confirm:Delete the auto-playlist \"Titles\"? Its tracks stay in the library.") }));
    QVERIFY(r.store.exists(s(u"Titles")));
    QCOMPARE(r.nodes->lastMessage(), QString());
    r.yes = true;
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Titles")));
    QVERIFY(r.nodes->run(s(u"delete"), s(u"auto:Titles")));
    QVERIFY(!r.store.exists(s(u"Titles")));
    QCOMPARE(r.nodes->lastMessage(), s(u"Deleted the auto-playlist \"Titles\""));
    QCOMPARE(r.status(), r.nodes->lastMessage());
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"library:Music")); // V-AUTO
    QCOMPARE(r.shown().size(), 4);
    QVERIFY(r.playlists.exists(s(u"Morning")));

    // From the row's menu.
    r.asked.clear();
    r.menuChoice = QString();
    QVERIFY(r.rightClick(s(u"auto:Everything")));
    QCOMPARE(r.asked, QStringList({ s(u"menu:delete|Delete") }));
    QVERIFY(r.store.exists(s(u"Everything")));
    r.asked.clear();
    r.menuChoice = s(u"delete");
    QVERIFY(r.rightClick(s(u"auto:Everything")));
    QCOMPARE(r.asked.size(), 2);
    QVERIFY(!r.store.exists(s(u"Everything")));
    // Rows that are not auto-playlists get no menu from this object.
    r.asked.clear();
    QVERIFY(r.rightClick(s(u"playlist:Morning")));
    QVERIFY(r.rightClick(s(u"playlists")));
    QCOMPARE(r.asked, QStringList());
}

void AutoPlaylistNodesTest::gone()
{
    // AN-NODES: destroyed, it leaves nothing behind.
    Rig r;
    QVERIFY(r.store.save(s(u"Titles"), titled(u"title")));
    r.start();
    NEED_NODES(r);
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Titles")));
    QCOMPARE(r.shown().size(), 4);
    r.nodes.reset();
    QVERIFY(!r.shell->navigator()->nodes().contains(s(u"auto:Titles")));
    QCOMPARE(r.shell->navigator()->currentNode(), s(u"library:Music"));
    QCOMPARE(r.shown(), sorted({ r.mp3, r.flac, r.m4a, r.ogg }));
    // The library goes on changing with the old source still registered in the shell: no crash, no effect.
    const QString more = r.put("basic.flac", "music/f.flac");
    QVERIFY(r.library->addFile(more, s(u"Music"), r.now) != 0);
    QCOMPARE(r.shown().size(), 5);
    QVERIFY(r.store.save(s(u"After"), titled(u"x"))); // nobody listens
    QVERIFY(!r.shell->navigator()->nodes().contains(s(u"auto:After")));
}

QTEST_MAIN(AutoPlaylistNodesTest)
#include "tst_ui_autoplaylistnodes.moc"
