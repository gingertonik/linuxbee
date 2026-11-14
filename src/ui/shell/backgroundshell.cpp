#include "shell/backgroundshell.h"

// Stub. Plan 33 replaces every body in this file.
namespace linuxbee::ui {

struct BackgroundShell::Private { };

BackgroundShell::BackgroundShell(MainWindow *window, Library *library, PlaybackShell *playback,
    QSettings *settings, Hooks hooks, const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(window);
    Q_UNUSED(library);
    Q_UNUSED(playback);
    Q_UNUSED(settings);
    Q_UNUSED(hooks);
    Q_UNUSED(bus);
}

BackgroundShell::~BackgroundShell()
{
    Q_UNUSED(d);
}

MprisService *BackgroundShell::mpris() const
{
    return nullptr;
}

FolderMonitor *BackgroundShell::monitor() const
{
    return nullptr;
}

QStringList BackgroundShell::monitoredFolders() const
{
    return { };
}

void BackgroundShell::setMonitoredFolders(const QStringList &folders)
{
    Q_UNUSED(folders);
}

void BackgroundShell::reload()
{
}

QString BackgroundShell::lastMessage() const
{
    return { };
}

}
