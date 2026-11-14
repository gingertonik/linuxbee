#include "shell/playlistediting.h"

// Stub. Plan 11d replaces every body in this file.
namespace linuxbee::ui {

struct PlaylistEditing::Private { };

PlaylistEditing::PlaylistEditing(MainWindow *window, ActionRegistry *actions, LibraryShell *shell,
    PlaylistStore *playlists, Hooks hooks, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(window);
    Q_UNUSED(actions);
    Q_UNUSED(shell);
    Q_UNUSED(playlists);
    Q_UNUSED(hooks);
}

PlaylistEditing::~PlaylistEditing()
{
    Q_UNUSED(d);
}

QStringList PlaylistEditing::commandsFor(const QString &rowId) const
{
    Q_UNUSED(rowId);
    return { };
}

QString PlaylistEditing::label(const QString &command)
{
    Q_UNUSED(command);
    return { };
}

bool PlaylistEditing::run(const QString &command, const QString &rowId)
{
    Q_UNUSED(command);
    Q_UNUSED(rowId);
    return false;
}

QString PlaylistEditing::lastMessage() const
{
    return { };
}

}
