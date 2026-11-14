#include "shell/autoplaylistnodes.h"

// Stub. Plan 11e replaces every body in this file.
namespace linuxbee::ui {

struct AutoPlaylistNodes::Private { };

AutoPlaylistNodes::AutoPlaylistNodes(MainWindow *window, LibraryShell *shell, Library *library,
    AutoPlaylistStore *store, Hooks hooks, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(window);
    Q_UNUSED(shell);
    Q_UNUSED(library);
    Q_UNUSED(store);
    Q_UNUSED(hooks);
}

AutoPlaylistNodes::~AutoPlaylistNodes()
{
    Q_UNUSED(d);
}

QStringList AutoPlaylistNodes::commandsFor(const QString &rowId) const
{
    Q_UNUSED(rowId);
    return { };
}

QString AutoPlaylistNodes::label(const QString &command)
{
    Q_UNUSED(command);
    return { };
}

bool AutoPlaylistNodes::run(const QString &command, const QString &rowId)
{
    Q_UNUSED(command);
    Q_UNUSED(rowId);
    return false;
}

QString AutoPlaylistNodes::lastMessage() const
{
    return { };
}

}
