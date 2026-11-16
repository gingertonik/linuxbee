// LOCKED ACCEPTANCE TEST (showing an auto-playlist reads each track of the library once). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/auto-playlist-nodes.md (AN-SHOW,
// "Speed") and docs/spec/library.md (L-COUNT). The cost is counted, not timed.
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

class AutoPlaylistNodesReadsTest : public QObject {
    Q_OBJECT
private slots:
    void eachTrackOnce();
};

void AutoPlaylistNodesReadsTest::eachTrackOnce()
{
    // AN-SHOW, speed: every track of the library is handed out once for a showing,
    // however many of them the playlist holds.
    (void)&oneAtRandom;
    (void)&sorted;
    Rig r;
    QVERIFY(r.store.save(s(u"Titles"), titled(u"title"))); // 4 of the 5
    QVERIFY(r.store.save(s(u"Everything"), AutoPlaylist())); // all 5
    QVERIFY(r.store.save(s(u"Nothing"), titled(u"no such words"))); // none
    r.start();
    NEED_NODES(r);
    QCOMPARE(r.library->count(), 5);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    struct Case {
        const char16_t *node;
        int shown;
    };
    for (const Case &c : { Case { u"auto:Nothing", 0 }, Case { u"auto:Titles", 4 }, Case { u"auto:Everything", 5 } }) {
        const qint64 before = r.library->tracksRead();
        QVERIFY(r.shell->navigator()->selectNode(s(c.node)));
        const qint64 read = r.library->tracksRead() - before; // taken before the test itself asks anything
        QCOMPARE(r.shell->nodeTracks().size(), c.shown);
        QVERIFY2(read == 5, qPrintable(s(c.node) + QStringLiteral(": %1 tracks read").arg(read)));
    }
    // What is shown is the track as the library has it now.
    QVERIFY(r.shell->navigator()->selectNode(s(u"auto:Titles")));
    QCOMPARE(r.shell->nodeTracks().size(), 4);
    for (const LibraryTrack &t : r.shell->nodeTracks()) {
        const auto stored = r.library->trackByPath(t.path);
        QVERIFY(stored.has_value());
        QCOMPARE(t.id, stored->id);
        QCOMPARE(t.tags.fields, stored->tags.fields);
    }
}

QTEST_MAIN(AutoPlaylistNodesReadsTest)
#include "tst_ui_autoplaylistnodes_reads.moc"
