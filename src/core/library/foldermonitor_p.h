#pragma once

#include "library/foldermonitor.h"

#include <QDir>
#include <QFileSystemWatcher>
#include <QStringList>

namespace linuxbee {

// A folder as the monitor keeps it: absolute and clean, no trailing slash.
inline QString cleanFolderPath(const QString &path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}

// The state behind FolderMonitor. Shared by foldermonitor.cpp and
// foldermonitor_watch.cpp; not part of the public API.
struct FolderMonitor::Private {
    explicit Private(FolderMonitor::Scanner scanner);

    FolderMonitor::Scanner scanner;
    int settleMs = 2000; // FM-SETTLE
    int directoryLimit = 4096; // FM-DIRS
    QStringList folders; // the roots as set: cleaned, once each, in the order given
    QStringList watched; // the folders being watched now, sorted
    bool limited = false;
    QFileSystemWatcher watcher;

    // Makes `watched` match `folders` and the limit, and the watcher match
    // `watched`. Reports the folders that came and the ones that went.
    void reconcile(QStringList *arrived = nullptr, QStringList *departed = nullptr);
};

}
