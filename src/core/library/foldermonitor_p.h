#pragma once

#include "library/foldermonitor.h"

#include <QDir>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>

namespace linuxbee {

// A folder as the monitor keeps it: absolute and clean, no trailing slash.
inline QString cleanFolderPath(const QString &path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}

// The state behind FolderMonitor. Shared by foldermonitor.cpp and
// foldermonitor_watch.cpp; not part of the public API.
struct FolderMonitor::Private {
    Private(FolderMonitor *owner, FolderMonitor::Scanner scanner);

    FolderMonitor *owner;
    FolderMonitor::Scanner scanner;
    int settleMs = 2000; // FM-SETTLE
    int directoryLimit = 4096; // FM-DIRS
    QStringList folders; // the roots as set: cleaned, once each, in the order given
    QStringList watched; // the folders being watched now, sorted
    bool limited = false;
    QFileSystemWatcher watcher;

    QTimer timer; // the one single-shot wait (FM-SETTLE)
    QSet<QString> changed; // folders where something changed since the last scan
    // What each changed folder held at its last change or last look (FM-SETTLE).
    QHash<QString, QStringList> snapshots;
    bool pending = false; // a change was seen and its scan has not happened yet
    bool scanning = false; // the scanner is running: it is never asked again meanwhile

    // Makes `watched` match `folders` and the limit, and the watcher match
    // `watched`. Reports the folders that came and the ones that went.
    void reconcile(QStringList *arrived = nullptr, QStringList *departed = nullptr);

    // Records a folder as changed, and what it holds now.
    void markChanged(const QString &folder);
    // FM-WATCH and FM-SETTLE: a watched folder changed.
    void onDirectoryChanged(const QString &path);
    // The wait is over, unless the changed folders still differ from their
    // last look: then wait again. Otherwise scan the roots of the changed folders.
    void onSettled();
    // Calls the scanner with `roots`, then forgets the changes and emits scanned.
    void scanNow(const QStringList &roots);
};

}
