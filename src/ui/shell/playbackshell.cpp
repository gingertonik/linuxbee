#include "shell/playbackshell.h"

// Stub. Plan 31 replaces every body in this file.
namespace linuxbee::ui {

struct PlaybackShell::Private { };

PlaybackShell::PlaybackShell(MainWindow *window, ActionRegistry *actions, Library *library,
    LibraryShell *libraryShell, AudioBackend *backend, QSettings *settings,
    std::function<QDateTime()> now, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(window);
    Q_UNUSED(actions);
    Q_UNUSED(library);
    Q_UNUSED(libraryShell);
    Q_UNUSED(backend);
    Q_UNUSED(settings);
    Q_UNUSED(now);
}

PlaybackShell::~PlaybackShell()
{
    Q_UNUSED(d);
}

PlayQueue *PlaybackShell::queue() const
{
    return nullptr;
}

Player *PlaybackShell::player() const
{
    return nullptr;
}

PlayerBar *PlaybackShell::bar() const
{
    return nullptr;
}

QString PlaybackShell::titleFor(const QString &path) const
{
    Q_UNUSED(path);
    return { };
}

QString PlaybackShell::lastMessage() const
{
    return { };
}

void PlaybackShell::saveState()
{
}

}
