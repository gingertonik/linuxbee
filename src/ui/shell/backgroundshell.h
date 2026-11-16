#pragma once
#include <QDBusConnection>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "library/library.h"

class QSettings;

// What linuxBee does without being asked: it answers the desktop's media
// controls and takes in files that arrive in the monitored folders.
// Contract: docs/spec/background-shell.md (BG-x). It connects the MPRIS
// service and the folder monitor to the rest; it owns no rule of its own.
namespace linuxbee {
class FolderMonitor;
}

namespace linuxbee::ui {

class MainWindow;
class MprisService;
class PlaybackShell;
class ScanRunner;

class BackgroundShell : public QObject {
    Q_OBJECT
public:
    struct Hooks {
        std::function<QDateTime()> now; // UTC
        std::function<void()> quit; // the desktop asked linuxBee to close
    };

    // Nothing handed in is owned; all of it must outlive the shell. `bus` is
    // the session bus (tests hand in a bus of their own).
    BackgroundShell(MainWindow *window, Library *library, PlaybackShell *playback,
        QSettings *settings, Hooks hooks, const QDBusConnection &bus, QObject *parent = nullptr);
    ~BackgroundShell() override;

    MprisService *mpris() const;
    FolderMonitor *monitor() const;
    ScanRunner *scanner() const; // BG-MONITOR: the runner the monitor's scans go through

    // BG-FOLDERS
    QStringList monitoredFolders() const;
    void setMonitoredFolders(const QStringList &folders);

    // BG-RELOAD: the settings were changed by someone else (the preferences).
    void reload();

    QString lastMessage() const; // BG-MESSAGE: what the status bar was last told

private:
    struct Private;
    Private *d;
};

}
