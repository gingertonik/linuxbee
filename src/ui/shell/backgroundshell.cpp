#include "shell/backgroundshell.h"

#include "desktop/mprisservice.h"
#include "library/foldermonitor.h"
#include "mainwindow/mainwindow.h"
#include "shell/playbackshell.h"
#include "shell/scanrunner.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>

#include <deque>
#include <memory>

namespace linuxbee::ui {

namespace {

// BG-MONITOR: the section the settings name for new files when it is a library section;
// anything else is the Inbox. It is read at each scan, not once.
QString sectionFrom(QSettings *settings)
{
    const QString name = settings->value(QStringLiteral("library/monitoredCategory")).toString();
    return Library::categories().contains(name) ? name : QStringLiteral("Inbox");
}

}

// The state of the shell: the folder monitor and the MPRIS service it owns. The
// helpers live here, not in the header, which is fixed by the seed.
struct BackgroundShell::Private {
    MainWindow *window = nullptr;
    Library *library = nullptr;
    QSettings *settings = nullptr;
    std::unique_ptr<ScanRunner> runner;
    std::unique_ptr<FolderMonitor> monitor;
    std::unique_ptr<MprisService> mpris;
    // The section each scan the runner has not yet ended was asked with, oldest first.
    std::deque<QString> sections;
    QString message;

    // BG-MESSAGE: one line in the window's status bar, kept as lastMessage().
    void showMessage(const QString &text)
    {
        message = text;
        window->statusBar()->showMessage(text);
    }
};

BackgroundShell::BackgroundShell(MainWindow *window, Library *library, PlaybackShell *playback,
    QSettings *settings, Hooks hooks, const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->window = window;
    d->library = library;
    d->settings = settings;

    // BG-MONITOR: the shell's own runner. A scan the monitor asks for goes to it with the
    // section the settings name at that moment, kept until that scan ends.
    d->runner = std::make_unique<ScanRunner>(library);
    connect(d->runner.get(), &ScanRunner::finished, this,
        [this](const ScanReport &report, const QStringList &) {
            if (d->sections.empty())
                return;
            const QString section = d->sections.front();
            d->sections.pop_front();
            // A scan that added files says how many, in its section. A scan that added
            // nothing says nothing and leaves the last message as it was.
            const int added = report.added.size();
            if (added == 1)
                d->showMessage(tr("%1: 1 new file").arg(section));
            else if (added > 1)
                d->showMessage(tr("%1: %2 new files").arg(section).arg(added));
        });

    // BG-MONITOR: the stored folders, and the settle time when it is a number from 0
    // to 60000. Anything else leaves the monitor's own default in place. The monitor's
    // scanner only asks the runner and returns an empty report at once.
    d->monitor = std::make_unique<FolderMonitor>(
        [this, settings, now = hooks.now](const QStringList &folders) {
            ScanOptions options;
            options.category = sectionFrom(settings);
            options.now = now ? now() : QDateTime();
            d->sections.push_back(options.category);
            d->runner->scan(folders, options);
            return ScanReport();
        });
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
    // BG-MPRIS: without a bus, or with the name taken, the media keys are off and
    // everything else works. The service's own reason is what the message says.
    QString error;
    if (!d->mpris->start(bus, &error))
        d->showMessage(QCoreApplication::translate(
            "linuxbee::ui::BackgroundShell", "Media keys are not available: %1")
                .arg(error));

    // BG-STARTUP: with checkOnStartup set, every monitored folder is checked once, as soon
    // as the event loop runs, so the window comes up first. "true" and "1" in any case
    // mean yes; anything else means no.
    const QString startup
        = settings->value(QStringLiteral("library/checkOnStartup")).toString().toLower();
    if (startup == QStringLiteral("true") || startup == QStringLiteral("1"))
        QTimer::singleShot(0, this, [this] { d->monitor->checkNow(); });
}

BackgroundShell::~BackgroundShell()
{
    // Off the bus first, then the watching stops. The player and the library are untouched.
    d->mpris.reset();
    d->monitor.reset();
    d->runner.reset();
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

ScanRunner *BackgroundShell::scanner() const
{
    return d->runner.get();
}

QStringList BackgroundShell::monitoredFolders() const
{
    return d->monitor->folders();
}

void BackgroundShell::setMonitoredFolders(const QStringList &folders)
{
    // BG-FOLDERS: the monitor cleans the list, and what it kept is stored at once.
    // Choosing folders scans nothing.
    d->monitor->setFolders(folders);
    d->settings->setValue(QStringLiteral("library/monitored"), d->monitor->folders());
}

void BackgroundShell::reload()
{
    // BG-RELOAD: the stored folders are given to the monitor again. Nothing is scanned
    // here, and checkOnStartup is not acted on here. When a folder the monitor did not
    // have before the call is now monitored, every folder is checked once the event loop
    // runs, as BG-STARTUP does it; a reload that adds no folder checks nothing.
    const QStringList before = d->monitor->folders();
    d->monitor->setFolders(d->settings->value(QStringLiteral("library/monitored")).toStringList());
    bool added = false;
    for (const QString &folder : d->monitor->folders()) {
        if (!before.contains(folder)) {
            added = true;
            break;
        }
    }
    if (added)
        QTimer::singleShot(0, this, [this] { d->monitor->checkNow(); });
}

QString BackgroundShell::lastMessage() const
{
    return d->message;
}

}
