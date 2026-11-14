#include "shell/libraryshell.h"

// Stub. Plan 30 replaces every body in this file.
namespace linuxbee::ui {

struct LibraryShell::Private { };

LibraryShell::LibraryShell(MainWindow *window, ActionRegistry *actions, Library *library,
    PlaylistStore *playlists, QSettings *settings, ShellHooks hooks, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(window);
    Q_UNUSED(actions);
    Q_UNUSED(library);
    Q_UNUSED(playlists);
    Q_UNUSED(settings);
    Q_UNUSED(hooks);
}

LibraryShell::~LibraryShell()
{
    Q_UNUSED(d);
}

NavigatorPanel *LibraryShell::navigator() const
{
    return nullptr;
}

TrackListModel *LibraryShell::model() const
{
    return nullptr;
}

TrackListPage *LibraryShell::trackList() const
{
    return nullptr;
}

QString LibraryShell::currentNode() const
{
    return { };
}

QList<qint64> LibraryShell::selectedTrackIds() const
{
    return { };
}

QStringList LibraryShell::selectedPaths() const
{
    return { };
}

QStringList LibraryShell::shownPaths() const
{
    return { };
}

QString LibraryShell::lastMessage() const
{
    return { };
}

void LibraryShell::setPlayingPaths(const QStringList &paths)
{
    Q_UNUSED(paths);
}

QList<LibraryTrack> LibraryShell::nodeTracks() const
{
    return { };
}

void LibraryShell::setNarrowing(std::function<bool(const LibraryTrack &)> keep)
{
    Q_UNUSED(keep);
}

void LibraryShell::setBrowser(QWidget *browser)
{
    Q_UNUSED(browser);
}

QWidget *LibraryShell::browser() const
{
    return nullptr;
}

void LibraryShell::addNodeSource(NodeSource source)
{
    Q_UNUSED(source);
}

void LibraryShell::reloadNode()
{
}

void LibraryShell::saveState()
{
}

}
