#include "library/foldermonitor.h"

// Stub. Plan 19 replaces every body in this file.
namespace linuxbee {

QStringList directoriesToWatch(const QStringList &roots, int limit, bool *limited)
{
    Q_UNUSED(roots);
    Q_UNUSED(limit);
    if (limited)
        *limited = false;
    return { };
}

QStringList rootsFor(const QStringList &changedDirectories, const QStringList &roots)
{
    Q_UNUSED(changedDirectories);
    Q_UNUSED(roots);
    return { };
}

struct FolderMonitor::Private { };

FolderMonitor::FolderMonitor(Scanner scanner, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(scanner);
}

FolderMonitor::~FolderMonitor()
{
    Q_UNUSED(d);
}

int FolderMonitor::settleMs() const
{
    return 0;
}

void FolderMonitor::setSettleMs(int ms)
{
    Q_UNUSED(ms);
}

int FolderMonitor::directoryLimit() const
{
    return 0;
}

void FolderMonitor::setDirectoryLimit(int limit)
{
    Q_UNUSED(limit);
}

QStringList FolderMonitor::folders() const
{
    return { };
}

void FolderMonitor::setFolders(const QStringList &roots)
{
    Q_UNUSED(roots);
}

QStringList FolderMonitor::watched() const
{
    return { };
}

bool FolderMonitor::limited() const
{
    return false;
}

bool FolderMonitor::isPending() const
{
    return false;
}

void FolderMonitor::checkNow()
{
}

FolderMonitor::Scanner libraryScanner(
    Library *library, const QString &category, std::function<QDateTime()> now)
{
    Q_UNUSED(library);
    Q_UNUSED(category);
    Q_UNUSED(now);
    return [](const QStringList &) { return ScanReport { }; };
}

}
