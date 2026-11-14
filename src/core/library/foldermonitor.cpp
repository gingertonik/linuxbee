#include "library/foldermonitor.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

// Stub. Plan 19 replaces every body in this file.
namespace linuxbee {

namespace {

// A folder as the monitor keeps it: absolute and clean.
QString cleanFolder(const QString &path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}

// Adds the folders below `folder` (not `folder` itself), depth first.
// Returns false when the limit stopped it before the end.
bool addBelow(const QString &folder, int limit, QStringList &listed, QSet<QString> &seen)
{
    const QFileInfoList children = QDir(folder).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo &child : children) {
        if (child.fileName().startsWith(QLatin1Char('.')))
            continue; // a hidden folder, and everything below it
        const QString sub = child.absoluteFilePath();
        if (seen.contains(sub))
            continue; // a root: its own turn covers the folders below it
        if (listed.size() >= limit)
            return false;
        seen.insert(sub);
        listed.append(sub);
        if (!addBelow(sub, limit, listed, seen))
            return false;
    }
    return true;
}

}

QStringList directoriesToWatch(const QStringList &roots, int limit, bool *limited)
{
    if (limited)
        *limited = false;

    QStringList rootFolders;
    for (const QString &root : roots) {
        if (root.trimmed().isEmpty())
            continue;
        const QString folder = cleanFolder(root);
        if (QFileInfo(folder).isDir() && !rootFolders.contains(folder))
            rootFolders.append(folder);
    }

    QStringList listed;
    QSet<QString> seen;
    bool cut = false;
    // Every root is taken before the folders below any root.
    for (const QString &root : rootFolders) {
        if (listed.size() >= limit) {
            cut = true;
            break;
        }
        seen.insert(root);
        listed.append(root);
    }
    if (!cut) {
        for (const QString &root : rootFolders) {
            if (!addBelow(root, limit, listed, seen)) {
                cut = true;
                break;
            }
        }
    }

    if (limited)
        *limited = cut;
    std::sort(listed.begin(), listed.end());
    return listed;
}

QStringList rootsFor(const QStringList &changedDirectories, const QStringList &roots)
{
    QStringList rootFolders;
    for (const QString &root : roots) {
        if (root.trimmed().isEmpty())
            continue;
        const QString folder = cleanFolder(root);
        if (!rootFolders.contains(folder))
            rootFolders.append(folder);
    }

    QStringList changed;
    for (const QString &folder : changedDirectories) {
        if (!folder.trimmed().isEmpty())
            changed.append(cleanFolder(folder));
    }

    QStringList result;
    for (const QString &root : rootFolders) {
        // "Inside" means equal, or starts with the root and a slash.
        const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
        for (const QString &folder : changed) {
            if (folder == root || folder.startsWith(prefix)) {
                result.append(root);
                break;
            }
        }
    }
    return result;
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
