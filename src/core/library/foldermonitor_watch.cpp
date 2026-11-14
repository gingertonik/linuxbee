#include "library/foldermonitor_p.h"

#include <QSet>

#include <algorithm>
#include <utility>

namespace linuxbee {

FolderMonitor::Private::Private(FolderMonitor::Scanner scanner)
    : scanner(std::move(scanner))
{
}

void FolderMonitor::Private::reconcile(QStringList *arrived, QStringList *departed)
{
    const QStringList next = directoriesToWatch(folders, directoryLimit, &limited);
    const QSet<QString> nextSet(next.cbegin(), next.cend());
    const QSet<QString> nowSet(watched.cbegin(), watched.cend());

    QStringList gone;
    for (const QString &folder : std::as_const(watched)) {
        if (!nextSet.contains(folder))
            gone.append(folder);
    }
    QStringList came;
    for (const QString &folder : next) {
        if (!nowSet.contains(folder))
            came.append(folder);
    }

    if (!gone.isEmpty())
        watcher.removePaths(gone);
    if (!came.isEmpty())
        watcher.addPaths(came);
    watched = next;

    if (arrived)
        *arrived = came;
    if (departed)
        *departed = gone;
}

FolderMonitor::FolderMonitor(Scanner scanner, QObject *parent)
    : QObject(parent)
    , d(new Private(std::move(scanner)))
{
}

FolderMonitor::~FolderMonitor()
{
    delete d;
}

int FolderMonitor::settleMs() const
{
    return d->settleMs;
}

void FolderMonitor::setSettleMs(int ms)
{
    d->settleMs = std::max(0, ms);
}

int FolderMonitor::directoryLimit() const
{
    return d->directoryLimit;
}

void FolderMonitor::setDirectoryLimit(int limit)
{
    d->directoryLimit = limit;
    d->reconcile();
}

QStringList FolderMonitor::folders() const
{
    return d->folders;
}

void FolderMonitor::setFolders(const QStringList &roots)
{
    QStringList cleaned;
    for (const QString &root : roots) {
        if (root.trimmed().isEmpty())
            continue;
        const QString folder = cleanFolderPath(root);
        if (!cleaned.contains(folder))
            cleaned.append(folder);
    }
    d->folders = cleaned;
    d->reconcile();
}

QStringList FolderMonitor::watched() const
{
    return d->watched;
}

bool FolderMonitor::limited() const
{
    return d->limited;
}

bool FolderMonitor::isPending() const
{
    return false;
}

void FolderMonitor::checkNow()
{
}

}
