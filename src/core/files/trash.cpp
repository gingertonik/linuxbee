#include "files/trash.h"

#include <QDir>
#include <QFileInfo>

// Stub from the seed: the builder replaces this file. Contract: docs/spec/delete-files.md.
namespace linuxbee {

bool sendToTrash(const QString &, QString *)
{
    return false;
}

QList<TrashOutcome> moveFilesToTrash(const QStringList &)
{
    return { };
}

}
