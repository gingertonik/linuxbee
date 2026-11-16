#include "files/trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

// Contract: docs/spec/delete-files.md.
namespace linuxbee {

// Sends a file to the desktop's trash, and says where it went. QFile::moveToTrash fails while the
// trash folders are missing, as on a fresh account, so they are made first, the way a file manager
// makes them.
bool sendToTrash(const QString &path, QString *pathInTrash)
{
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        return false;
    const QString trash = dataHome + QStringLiteral("/Trash");
    if (!QDir().mkpath(trash + QStringLiteral("/files"))
        || !QDir().mkpath(trash + QStringLiteral("/info"))) {
        return false;
    }
    return QFile::moveToTrash(path, pathInTrash);
}

QList<TrashOutcome> moveFilesToTrash(const QStringList &)
{
    return { };
}

}
