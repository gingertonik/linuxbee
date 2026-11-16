#include "files/trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

#include <sys/stat.h>
#include <unistd.h>

// Contract: docs/spec/delete-files.md.
namespace linuxbee {

// FD-SEND 1 and 2: the real path of the folder a file is in, or an empty string when there is none
// or it cannot be written and searched.
static QString usableFolder(const QString &path)
{
    const QString folder = QFileInfo(QFileInfo(path).absolutePath()).canonicalFilePath();
    if (folder.isEmpty() || ::access(QFile::encodeName(folder).constData(), W_OK | X_OK) != 0)
        return QString();
    return folder;
}

// Sends a file to the desktop's trash, and says where it went. QFile::moveToTrash fails while the
// trash folders are missing, as on a fresh account, so they are made first, the way a file manager
// makes them.
bool sendToTrash(const QString &path, QString *pathInTrash)
{
    const QString folder = usableFolder(path);
    if (folder.isEmpty())
        return false;
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        return false;
    const QString trash = dataHome + QStringLiteral("/Trash");
    if (!QDir().mkpath(trash + QStringLiteral("/files"))
        || !QDir().mkpath(trash + QStringLiteral("/info"))) {
        return false;
    }
    // The folder's real path and the file's name: the trash record names where the file was.
    if (!QFile::moveToTrash(folder + QLatin1Char('/') + QFileInfo(path).fileName(), pathInTrash))
        return false;
    // FD-SEND 3: a file still at the path was not moved, whatever was said. Nothing is removed.
    struct stat st;
    return ::lstat(QFile::encodeName(path).constData(), &st) != 0;
}

// Stub from the seed: the builder replaces these two. They ignore the mover.
SendResult sendToTrashWith(const QString &path, QString *pathInTrash, const TrashMover &mover)
{
    (void)mover;
    return sendToTrash(path, pathInTrash) ? SendResult::Went : SendResult::NotTaken;
}

QList<TrashOutcome> moveFilesToTrashWith(const QStringList &paths, const TrashMover &mover)
{
    (void)mover;
    return moveFilesToTrash(paths);
}

// FD-TRASH. A file is never opened or read to decide its outcome. Nothing here removes a file or a
// folder: a regular file goes to sendToTrash, and every other answer leaves the path as it was.
QList<TrashOutcome> moveFilesToTrash(const QStringList &paths)
{
    QList<TrashOutcome> outcomes;
    QSet<QString> seen;
    for (const QString &given : paths) {
        if (given.isEmpty())
            continue; // an empty path is no path
        const QString path = QDir::cleanPath(QFileInfo(given).absoluteFilePath());
        if (seen.contains(path))
            continue; // however it is spelt, a path is looked at once
        seen.insert(path);

        TrashOutcome outcome;
        outcome.path = path;
        const QFileInfo info(path);
        // The table's order. QFileInfo::isSymLink does not follow the link, so a link, even one
        // that points at nothing, is seen before "not there".
        if (info.isSymLink()) {
            outcome.status = TrashOutcome::Status::Refused;
            outcome.reason = QStringLiteral("a link");
        } else if (!info.exists()) {
            outcome.status = TrashOutcome::Status::NotThere;
        } else if (info.isDir()) {
            outcome.status = TrashOutcome::Status::Refused;
            outcome.reason = QStringLiteral("a folder");
        } else if (!info.isFile()) {
            outcome.status = TrashOutcome::Status::Refused;
            outcome.reason = QStringLiteral("not a regular file");
        } else {
            QString inTrash;
            if (usableFolder(path).isEmpty()) {
                outcome.status = TrashOutcome::Status::Failed;
                outcome.reason = QStringLiteral("its folder cannot be written");
            } else if (sendToTrash(path, &inTrash)) {
                outcome.status = TrashOutcome::Status::Trashed;
                outcome.pathInTrash = inTrash;
            } else {
                outcome.status = TrashOutcome::Status::Failed;
                outcome.reason = QStringLiteral("the trash did not take it");
            }
        }
        outcomes.append(outcome);
    }
    return outcomes;
}

}
