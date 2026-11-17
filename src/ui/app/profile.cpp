#include "app/profile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>

#include <fcntl.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "app/appinfo.h"

namespace linuxbee::ui {

QString profileSettingsFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString profileDataFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

ProfileLock::ProfileLock(const QString &dataFolder)
{
    const QString lockPath = dataFolder + QStringLiteral("/profile.lock");
    if (!QDir().mkpath(dataFolder)) {
        m_message = QCoreApplication::translate("profile", "linuxbee: cannot make the folder %1")
                        .arg(dataFolder);
        return;
    }
    m_lock = std::make_unique<QLockFile>(lockPath);
    // A lock whose owner is alive is never stale, however old: only a dead owner frees it.
    m_lock->setStaleLockTime(0);
    // Do not wait: another program holding the profile ends this start at once.
    if (m_lock->tryLock(0)) {
        m_state = State::Held;
        return;
    }
    if (m_lock->error() == QLockFile::LockFailedError) {
        m_state = State::Busy;
        m_message = QCoreApplication::translate(
            "profile", "linuxbee: already running on this profile (%1)")
                        .arg(QDir::toNativeSeparators(lockPath));
    } else {
        m_message = QCoreApplication::translate("profile", "linuxbee: cannot lock the profile (%1)")
                        .arg(QDir::toNativeSeparators(lockPath));
    }
    m_lock.reset();
}

ProfileLock::~ProfileLock() = default;

QStringList adoptOlderFiles(const QString &folder)
{
    QStringList left;
    const QString inner = folder + QLatin1Char('/') + applicationSettingsName();
    const QFileInfo innerInfo(inner);
    // A link is not followed and not touched; anything but a folder is not an older profile.
    if (innerInfo.isSymLink() || !innerInfo.isDir())
        return left;

    const QDir innerDir(inner);
    const QStringList names = innerDir.entryList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QString from = inner + QLatin1Char('/') + name;
        const QString to = folder + QLatin1Char('/') + name;
        // RENAME_NOREPLACE: a name that is there (a file, a folder, a link, even a dangling
        // one) is never replaced, and the check and the move are one step.
        const int renamed = ::renameat2(AT_FDCWD, QFile::encodeName(from).constData(), AT_FDCWD,
            QFile::encodeName(to).constData(), RENAME_NOREPLACE);
        if (renamed == 0)
            continue;
        const int why = errno;
        QString reason;
        if (why == EEXIST)
            reason = QCoreApplication::translate("profile", "%1 is there").arg(to);
        else
            reason = QString::fromLocal8Bit(std::strerror(why));
        left.append(QCoreApplication::translate("profile", "linuxbee: left in place: %1 (%2)")
                .arg(from, reason));
    }
    // rmdir takes away an empty folder and nothing else.
    QDir().rmdir(inner);
    return left;
}

}
