#include "transfer.h"

#include <QFile>
#include <QFileDevice>
#include <QFileInfo>

namespace linuxbee::organise {

QString transferFile(const QString &source, const QString &target, bool move)
{
    if (QFileInfo::exists(target))
        return QStringLiteral("the target already exists");

    // A rename keeps the file as it is. It fails across file systems, and
    // then the file is copied below.
    if (move && QFile::rename(source, target))
        return { };

    if (!QFile::copy(source, target)) {
        // A copy that failed part way can leave a partial target. The target
        // did not exist when this call began.
        if (QFileInfo::exists(target))
            QFile::remove(target);
        return QStringLiteral("could not copy the file");
    }

    // A copy keeps the source's modification time. Setting it needs the file
    // open for writing, and a read-only source may have made the copy
    // read-only, so the owner gets write permission for this step only.
    const QFileInfo from(source);
    const QFile::Permissions permissions = from.permissions();
    QFile::setPermissions(target, permissions | QFile::WriteOwner);
    bool timeKept = false;
    {
        QFile written(target);
        timeKept = written.open(QIODevice::ReadWrite)
            && written.setFileTime(from.lastModified(), QFileDevice::FileModificationTime);
    }
    QFile::setPermissions(target, permissions);
    if (!timeKept) {
        QFile::remove(target);
        return QStringLiteral("could not keep the modification time");
    }

    if (!move)
        return { };

    // Not a rename, so the copy must be complete before the original goes.
    if (QFileInfo(target).size() != from.size()) {
        QFile::remove(target);
        return QStringLiteral("the copy is not the size of the file");
    }
    if (!QFile::remove(source)) {
        QFile::remove(target);
        return QStringLiteral("could not remove the original after copying");
    }
    return { };
}

}
