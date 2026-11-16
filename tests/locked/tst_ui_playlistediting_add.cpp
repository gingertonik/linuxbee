// LOCKED ACCEPTANCE TEST (playlists: the selected tracks are added to one). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/playlist-editing.md (PE-ADD). Runs
// on the offscreen platform with a real window, navigator, playlist store and
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

// The rig with three tracks in the library, and the playlist question answered by `choice`.
struct Adding {
    Rig r;
    QString a, b, c;
    std::optional<QString> choice;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

    Adding()
    {
        r.made(QStringLiteral("music"));
        a = put("basic.mp3", "music/a.mp3");
        b = put("basic.flac", "music/b.flac");
        c = put("basic.m4a", "music/c.m4a");
        for (const QString &p : { a, b, c })
            if (r.library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        r.editing.reset();
        PlaylistEditing::Hooks h = r.hooks();
        h.choosePlaylist = [this](const QStringList &ids) {
            r.asked.append(QStringLiteral("playlist:") + ids.join(QLatin1Char('|')));
            return choice;
        };
        r.editing = std::make_unique<PlaylistEditing>(&r.window, &r.actions, r.shell.get(), &r.playlists, h);
    }
    QString put(const char *from, const char *to)
    {
        const QString target = r.dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    // Selects these files in the list and returns them in the order shown.
    QStringList select(const QStringList &paths)
    {
        QList<qint64> ids;
        for (const QString &p : paths) {
            if (const auto track = r.library->trackByPath(p))
                ids.append(track->id);
        }
        r.shell->trackList()->selectTracks(ids);
        return r.shell->selectedPaths();
    }
};

const QString kAll = QStringLiteral("playlist:Morning|Rock/Best of|Rock/Live/Sets");

}

class PlaylistAddTest : public QObject {
    Q_OBJECT
private slots:
    void addsTheSelection();
    void toANewPlaylist();
    void refusals();
};

void PlaylistAddTest::addsTheSelection()
{
    // PE-ADD: the selected tracks go to the end of the playlist chosen, in the order shown, in one write.
    Adding t;
    Rig &r = t.r;
    NEED_PARTS(r);
    (void)&Rig::rightClick;
    QVERIFY(r.actions.isImplemented(s(u"track.addToPlaylist")));
    const QString node = r.shell->currentNode();
    const QStringList two = t.select({ t.c, t.a });
    QCOMPARE(two.size(), 2);
    QSignalSpy written(&r.playlists, &PlaylistStore::tracksChanged);
    QSignalSpy listed(&r.playlists, &PlaylistStore::listChanged);

    t.choice = s(u"Morning");
    QVERIFY(r.editing->addSelected());
    QCOMPARE(r.asked, QStringList({ kAll })); // asked once, with every playlist, and nothing else
    QCOMPARE(r.playlists.tracks(s(u"Morning")), two);
    QCOMPARE(written.count(), 1); // one write for the whole selection
    QCOMPARE(written.at(0).at(0).toString(), s(u"Morning"));
    QCOMPARE(listed.count(), 0);
    QCOMPARE(r.editing->lastMessage(), s(u"Added 2 tracks to \"Morning\""));
    QCOMPARE(r.status(), r.editing->lastMessage());
    QCOMPARE(r.shell->currentNode(), node); // the list stays where it was
    QCOMPARE(r.shell->selectedPaths(), two); // and so does the selection

    // Added again: a track that is there already is added once more, at the end.
    const QStringList one = t.select({ t.a });
    QVERIFY(r.editing->addSelected());
    QCOMPARE(r.playlists.tracks(s(u"Morning")), two + one);
    QCOMPARE(written.count(), 2);
    QCOMPARE(r.editing->lastMessage(), s(u"Added 1 track to \"Morning\""));

    // A playlist in a folder is named by its id; the command does the same as the call.
    r.asked.clear();
    t.choice = s(u"Rock/Best of");
    const QStringList all = t.select({ t.a, t.b, t.c });
    QCOMPARE(all.size(), 3);
    QAction *action = r.actions.action(s(u"track.addToPlaylist"));
    QVERIFY(action);
    action->trigger();
    QCOMPARE(r.asked, QStringList({ kAll }));
    QCOMPARE(r.playlists.tracks(s(u"Rock/Best of")), all);
    QCOMPARE(r.editing->lastMessage(), s(u"Added 3 tracks to \"Rock/Best of\""));
    QCOMPARE(r.playlists.tracks(s(u"Morning")), two + one); // the other is untouched

    // Destroying the object frees the command.
    r.editing.reset();
    QVERIFY(!r.actions.isImplemented(s(u"track.addToPlaylist")));
}

void PlaylistAddTest::toANewPlaylist()
{
    // PE-ADD: the empty answer means a new playlist, which is asked a name as PE-COMMANDS does.
    Adding t;
    Rig &r = t.r;
    NEED_PARTS(r);
    const QString node = r.shell->currentNode();
    const QStringList two = t.select({ t.a, t.b });
    t.choice = QString();
    r.name = s(u"Evening");
    QVERIFY(r.editing->addSelected());
    QCOMPARE(r.asked, QStringList({ kAll, s(u"name:New Playlist:New Playlist") }));
    QVERIFY(r.playlists.exists(s(u"Evening"))); // at the top, in no folder
    QCOMPARE(r.playlists.tracks(s(u"Evening")), two);
    QCOMPARE(r.editing->lastMessage(), s(u"Added 2 tracks to \"Evening\""));
    QCOMPARE(r.shell->currentNode(), node); // unlike `new`, the list stays where it was

    // No name: nothing is made and nothing is said.
    const int before = int(r.ids().size());
    r.name = std::nullopt;
    QVERIFY(!r.editing->addSelected());
    QCOMPARE(int(r.ids().size()), before);
    QCOMPARE(r.editing->lastMessage(), s(u"Added 2 tracks to \"Evening\""));

    // A name the store refuses: its reason, and nothing is made.
    r.name = s(u"Evening"); // taken
    QVERIFY(!r.editing->addSelected());
    QCOMPARE(int(r.ids().size()), before);
    QVERIFY2(r.editing->lastMessage().startsWith(s(u"Could not create the playlist: ")), qPrintable(r.editing->lastMessage()));
    QVERIFY(r.editing->lastMessage().size() > 31);
    QCOMPARE(r.status(), r.editing->lastMessage());
    QCOMPARE(r.playlists.tracks(s(u"Evening")), two);
}

void PlaylistAddTest::refusals()
{
    // PE-ADD: nothing selected asks nothing; no answer changes nothing; a playlist that is not there is said.
    Adding t;
    Rig &r = t.r;
    NEED_PARTS(r);
    QSignalSpy written(&r.playlists, &PlaylistStore::tracksChanged);
    t.choice = s(u"Morning");
    r.shell->trackList()->selectTracks({ });
    QVERIFY(!r.editing->addSelected());
    QCOMPARE(r.asked, QStringList());
    QCOMPARE(r.editing->lastMessage(), QString());

    t.select({ t.b });
    t.choice = std::nullopt; // gave up
    QVERIFY(!r.editing->addSelected());
    QCOMPARE(r.asked, QStringList({ kAll }));
    QCOMPARE(r.editing->lastMessage(), QString());
    QCOMPARE(written.count(), 0);

    t.choice = s(u"Gone");
    QVERIFY(!r.editing->addSelected());
    QCOMPARE(r.editing->lastMessage(), s(u"Could not add to the playlist \"Gone\": there is no such playlist"));
    QCOMPARE(r.status(), r.editing->lastMessage());
    QVERIFY(!r.playlists.exists(s(u"Gone")));
    QCOMPARE(written.count(), 0);
    QCOMPARE(r.playlists.tracks(s(u"Morning")), QStringList());
}

QTEST_MAIN(PlaylistAddTest)
#include "tst_ui_playlistediting_add.moc"
