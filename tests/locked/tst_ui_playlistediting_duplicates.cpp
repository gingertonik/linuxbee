// LOCKED ACCEPTANCE TEST (playlists: adding a track that is there already asks first). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/playlist-editing.md, PE-ADD and PE-DUPLICATES.
//   W-GP  MusicBee wiki, "General Preferences", confirmations: "warn when duplicate tracks are
//         added to a playlist", ticked by default
//   PE-x  a linuxBee decision in playlist-editing.md: the answers "Add them again", "Skip those
//         already in" and Cancel (the orchestrator, 2026-11-21 06:13)
// Runs on the offscreen platform with a real window, navigator, playlist store and library shell,
// in a temporary folder of its own; the music files are copies of fixtures.
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
        shell = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        editing = std::make_unique<PlaylistEditing>(
            &window, &actions, shell.get(), &playlists, hooks());
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
        h.showMenu
            = [this](const QStringList &commands, const QStringList &labels, const QPoint &) {
                  asked.append(QStringLiteral("menu:") + commands.join(QLatin1Char(','))
                      + QLatin1Char('|') + labels.join(QLatin1Char(',')));
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

#define NEED_PARTS(r)                                                                              \
    do {                                                                                           \
        if (!(r).shell->navigator() || (r).editing->commandsFor(s(u"playlists")).isEmpty())        \
            QFAIL("the playlist commands have not been built");                                    \
    } while (false)

// The rig with three tracks in the library, and the playlist question answered by `choice`.
struct Adding {
    Rig r;
    QString a, b, c;
    std::optional<QString> choice;
    PlaylistEditing::DuplicateChoice answer = PlaylistEditing::DuplicateChoice::AddAgain;
    bool warn = true;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

    explicit Adding(bool warnOnDuplicates = true)
    {
        warn = warnOnDuplicates;
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
        h.askDuplicates = [this](const QString &id, int count) {
            r.asked.append(QStringLiteral("duplicates:%1:%2").arg(id).arg(count));
            return answer;
        };
        if (!warn)
            h.warnOnDuplicates = [] { return false; };
        r.editing = std::make_unique<PlaylistEditing>(
            &r.window, &r.actions, r.shell.get(), &r.playlists, h);
    }
    QString put(const char *from, const char *to)
    {
        const QString target = r.dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from),
                target))
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
using Choice = PlaylistEditing::DuplicateChoice;

}

class PlaylistDuplicatesTest : public QObject {
    Q_OBJECT
private slots:
    void defaults();
    void noDuplicatesNoQuestion();
    void addThemAgain();
    void skipThoseAlreadyIn();
    void skipLeavesNothing();
    void cancel();
    void notWarned();
};

void PlaylistDuplicatesTest::defaults()
{
    // W-GP: ticked by default. No answer to "warn?" means ticked; every test below but the last
    // leaves it so.
    QVERIFY(!PlaylistEditing::Hooks().warnOnDuplicates);
}

void PlaylistDuplicatesTest::noDuplicatesNoQuestion()
{
    // PE-DUPLICATES: asked only when a track is in the playlist already; a new playlist never is.
    Adding t;
    Rig &r = t.r;
    NEED_PARTS(r);
    (void)&Rig::rightClick;
    const QStringList two = t.select({ t.a, t.b });
    t.choice = s(u"Morning");
    QVERIFY(r.editing->addSelected());
    QCOMPARE(r.asked, QStringList({ kAll }));
    QCOMPARE(r.playlists.tracks(s(u"Morning")), two);
    r.asked.clear();
    t.choice = QString();
    r.name = s(u"Evening");
    QVERIFY(r.editing->addSelected());
    QCOMPARE(r.asked, QStringList({ kAll, s(u"name:New Playlist:New Playlist") }));
}

void PlaylistDuplicatesTest::addThemAgain()
{
    // PE-DUPLICATES: asked once, with the playlist and how many are in it already; "Add them
    // again" adds every selected track, as before, in one write.
    Adding t;
    Rig &r = t.r;
    NEED_PARTS(r);
    t.choice = s(u"Morning");
    const QStringList two = t.select({ t.a, t.b });
    QVERIFY(r.editing->addSelected());
    r.asked.clear();
    QSignalSpy written(&r.playlists, &PlaylistStore::tracksChanged);
    const QStringList three = t.select({ t.a, t.b, t.c });
    QVERIFY(r.editing->addSelected());
    QCOMPARE(r.asked, QStringList({ kAll, s(u"duplicates:Morning:2") }));
    QCOMPARE(r.playlists.tracks(s(u"Morning")), two + three);
    QCOMPARE(written.count(), 1);
    QCOMPARE(r.editing->lastMessage(), s(u"Added 3 tracks to \"Morning\""));
}

void PlaylistDuplicatesTest::skipThoseAlreadyIn()
{
    // PE-DUPLICATES: "Skip those already in" adds only the others, in the order shown.
    Adding t;
    Rig &r = t.r;
    NEED_PARTS(r);
    t.choice = s(u"Morning");
    const QStringList one = t.select({ t.b });
    QVERIFY(r.editing->addSelected());
    r.asked.clear();
    t.answer = Choice::Skip;
    const QStringList three = t.select({ t.a, t.b, t.c });
    QVERIFY(r.editing->addSelected());
    QCOMPARE(r.asked, QStringList({ kAll, s(u"duplicates:Morning:1") }));
    QStringList others = three;
    others.removeAll(t.b);
    QCOMPARE(r.playlists.tracks(s(u"Morning")), one + others);
    QCOMPARE(r.editing->lastMessage(), s(u"Added 2 tracks to \"Morning\""));
    QCOMPARE(r.status(), r.editing->lastMessage());
}

void PlaylistDuplicatesTest::skipLeavesNothing()
{
    // PE-DUPLICATES: when every track is there already, Skip adds nothing and says so; no write.
    Adding t;
    Rig &r = t.r;
    NEED_PARTS(r);
    t.choice = s(u"Morning");
    const QStringList two = t.select({ t.a, t.c });
    QVERIFY(r.editing->addSelected());
    t.answer = Choice::Skip;
    QSignalSpy written(&r.playlists, &PlaylistStore::tracksChanged);
    QVERIFY(!r.editing->addSelected());
    QCOMPARE(r.playlists.tracks(s(u"Morning")), two);
    QCOMPARE(written.count(), 0);
    QCOMPARE(
        r.editing->lastMessage(), s(u"Nothing added to \"Morning\": every track is in it already"));
    QCOMPARE(r.status(), r.editing->lastMessage());
}

void PlaylistDuplicatesTest::cancel()
{
    // PE-DUPLICATES: Cancel changes nothing and leaves the message as it was.
    Adding t;
    Rig &r = t.r;
    NEED_PARTS(r);
    t.choice = s(u"Morning");
    const QStringList one = t.select({ t.a });
    QVERIFY(r.editing->addSelected());
    const QString said = r.editing->lastMessage();
    t.answer = Choice::Cancel;
    QSignalSpy written(&r.playlists, &PlaylistStore::tracksChanged);
    t.select({ t.a, t.b });
    QVERIFY(!r.editing->addSelected());
    QCOMPARE(r.playlists.tracks(s(u"Morning")), one);
    QCOMPARE(written.count(), 0);
    QCOMPARE(r.editing->lastMessage(), said);
}

void PlaylistDuplicatesTest::notWarned()
{
    // W-GP: unticked, nothing is asked and a track is added once more, as before.
    Adding t(false);
    Rig &r = t.r;
    NEED_PARTS(r);
    t.choice = s(u"Morning");
    const QStringList one = t.select({ t.a });
    QVERIFY(r.editing->addSelected());
    r.asked.clear();
    QVERIFY(r.editing->addSelected());
    QCOMPARE(r.asked, QStringList({ kAll }));
    QCOMPARE(r.playlists.tracks(s(u"Morning")), one + one);
}

QTEST_MAIN(PlaylistDuplicatesTest)
#include "tst_ui_playlistediting_duplicates.moc"
