#include "library/foldermonitor_p.h"

#include <QSet>

#include <algorithm>
#include <utility>

namespace linuxbee {

FolderMonitor::Private::Private(FolderMonitor *owner, FolderMonitor::Scanner scanner)
    : owner(owner)
    , scanner(std::move(scanner))
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

void FolderMonitor::Private::onDirectoryChanged(const QString &path)
{
    // A folder that appeared is watched from now on, so that what is put
    // into it is seen too; it counts as changed.
    QStringList arrived;
    QStringList departed;
    reconcile(&arrived, &departed);

    changed.insert(cleanFolderPath(path));
    for (const QString &folder : std::as_const(arrived))
        changed.insert(folder);
    for (const QString &folder : std::as_const(departed))
        changed.insert(folder);

    pending = true;
    timer.start(settleMs); // a further change starts the wait again
}

void FolderMonitor::Private::onSettled()
{
    if (!pending)
        return;
    QStringList changedFolders(changed.cbegin(), changed.cend());
    std::sort(changedFolders.begin(), changedFolders.end());
    scanNow(rootsFor(changedFolders, folders));
}

void FolderMonitor::Private::scanNow(const QStringList &roots)
{
    if (!scanner || scanning)
        return;

    scanning = true;
    const ScanReport report = scanner(roots);
    scanning = false;

    timer.stop();
    changed.clear();
    pending = false;
    reconcile(); // after every scan the list is made afresh
    emit owner->scanned(report, roots);
}

FolderMonitor::FolderMonitor(Scanner scanner, QObject *parent)
    : QObject(parent)
    , d(new Private(this, std::move(scanner)))
{
    d->timer.setSingleShot(true);
    connect(&d->watcher, &QFileSystemWatcher::directoryChanged, this,
        [this](const QString &path) { d->onDirectoryChanged(path); });
    connect(&d->timer, &QTimer::timeout, this, [this] { d->onSettled(); });
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
    return d->pending;
}

void FolderMonitor::checkNow()
{
}

}
