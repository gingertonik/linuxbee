// LOCKED ACCEPTANCE TEST (Add to Playlist says why a playlist was not changed). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: PE-ADD in docs/spec/playlist-editing.md, Y-EDIT in docs/spec/playlists.md.
// Offscreen, with the real window, library and store, in a temporary folder. The rig is the one of
// tst_ui_playlistediting_add.cpp.
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

}

class PlaylistAddRefusedTest : public QObject {
    Q_OBJECT
private slots:
    void aPlaylistThatIsNotUtf8();
    void aPlaylistThatCannotBeRead();
};

namespace {

QString byHand(Rig &r, const QByteArray &bytes)
{
    const QString path = r.dir.path() + s(u"/playlists/Old.m3u8");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        QTest::qFail("could not write a playlist", __FILE__, __LINE__);
    return path;
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArrayLiteral("<cannot open>");
}

}

void PlaylistAddRefusedTest::aPlaylistThatIsNotUtf8()
{
    // PE-ADD: a playlist the store will not edit (Y-EDIT) is not added to, and the message says
    // why.
    (void)kAll;
    Adding a;
    NEED_PARTS(a.r);
    const QByteArray bytes("/m/Bj\xF6rk/J\xF3ga.mp3\n/m/c.mp3\n");
    const QString path = byHand(a.r, bytes);
    a.select({ a.a, a.b });
    a.choice = s(u"Old");
    QVERIFY(!a.r.editing->addSelected());
    QCOMPARE(a.r.editing->lastMessage(),
        s(u"Could not add to the playlist \"Old\": the file is not UTF-8"));
    QCOMPARE(a.r.status(), a.r.editing->lastMessage());
    QCOMPARE(bytesOf(path), bytes);
}

void PlaylistAddRefusedTest::aPlaylistThatCannotBeRead()
{
    // The audit's case: three entries, a file that can be written and not read. It used to say
    // "Added 1 track" and hold that one track.
    Adding a;
    NEED_PARTS(a.r);
    const QByteArray bytes("/m/a.mp3\n/m/b.mp3\n/m/c.mp3\n");
    const QString path = byHand(a.r, bytes);
    QVERIFY(QFile::setPermissions(path, QFile::WriteOwner));
    QVERIFY2(!QFile(path).open(QIODevice::ReadOnly), "this test cannot be run as root");
    a.select({ a.a });
    a.choice = s(u"Old");
    QVERIFY(!a.r.editing->addSelected());
    QCOMPARE(a.r.editing->lastMessage(),
        s(u"Could not add to the playlist \"Old\": the file could not be read"));
    QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
    QCOMPARE(bytesOf(path), bytes);
}

QTEST_MAIN(PlaylistAddRefusedTest)
#include "tst_ui_playlistediting_refused.moc"
