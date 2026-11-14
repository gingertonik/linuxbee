#include "shell/backgroundshell.h"

#include "desktop/mprisservice.h"
#include "library/foldermonitor.h"
#include "mainwindow/mainwindow.h"
#include "shell/playbackshell.h"

#include <QSettings>

#include <memory>

namespace linuxbee::ui {

// The state of the shell: the folder monitor and the MPRIS service it owns. The
// helpers live here, not in the header, which is fixed by the seed.
struct BackgroundShell::Private {
    MainWindow *window = nullptr;
    Library *library = nullptr;
    QSettings *settings = nullptr;
    std::unique_ptr<FolderMonitor> monitor;
    std::unique_ptr<MprisService> mpris;
};

BackgroundShell::BackgroundShell(MainWindow *window, Library *library, PlaybackShell *playback,
    QSettings *settings, Hooks hooks, const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->window = window;
    d->library = library;
    d->settings = settings;

    // BG-MONITOR: the stored folders, and the settle time when it is a number from 0
    // to 60000. Anything else leaves the monitor's own default in place.
    d->monitor = std::make_unique<FolderMonitor>(
        FolderMonitor::Scanner([](const QStringList &) { return ScanReport(); }));
    d->monitor->setFolders(settings->value(QStringLiteral("library/monitored")).toStringList());
    bool settleOk = false;
    const int settle = settings->value(QStringLiteral("library/settleMs")).toInt(&settleOk);
    if (settleOk && settle >= 0 && settle <= 60000)
        d->monitor->setSettleMs(settle);

    // BG-MPRIS: the desktop's requests reach the window and the player. Each hook
    // captures what it needs by value, so nothing points at the shell.
    MprisService::Hooks mpris;
    mpris.raise = [window] {
        if (window->isMinimized())
            window->showNormal();
        else
            window->show();
        window->raise();
        window->activateWindow();
    };
    mpris.quit = [quit = hooks.quit] {
        if (quit)
            quit();
    };
    mpris.describe = [library](const QString &path) {
        MprisService::Track track;
        if (const auto known = library->trackByPath(path)) {
            track.title = known->tags.first(QStringLiteral("Title"));
            track.artists = known->tags.fields.value(QStringLiteral("Artist"));
            track.album = known->tags.first(QStringLiteral("Album"));
        }
        return track;
    };
    d->mpris = std::make_unique<MprisService>(playback->player(), playback->queue(), mpris);
    d->mpris->start(bus);
}

BackgroundShell::~BackgroundShell()
{
    // Off the bus first, then the watching stops. The player and the library are untouched.
    d->mpris.reset();
    d->monitor.reset();
    delete d;
}

MprisService *BackgroundShell::mpris() const
{
    return d->mpris.get();
}

FolderMonitor *BackgroundShell::monitor() const
{
    return d->monitor.get();
}

QStringList BackgroundShell::monitoredFolders() const
{
    return d->monitor->folders();
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
