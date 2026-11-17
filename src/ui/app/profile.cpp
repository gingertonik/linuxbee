#include "app/profile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>

#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "app/appinfo.h"

namespace linuxbee::ui {

namespace {

// W-ADOPT: the names a database's own files end in.
const QStringList &companionEndings()
{
    static const QStringList endings
        = { QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal") };
    return endings;
}

// The name a companion belongs to, or empty when the name is not a companion's.
QString baseOf(const QString &name)
{
    for (const QString &ending : companionEndings()) {
        if (name.size() > ending.size() && name.endsWith(ending))
            return name.left(name.size() - ending.size());
    }
    return QString();
}

// Something is at the path, a link (even a dangling one) included.
bool present(const QString &path)
{
    const QFileInfo info(path);
    return info.isSymLink() || info.exists();
}

// A regular file that begins as an SQLite database does (its first sixteen bytes).
bool beginsAsDatabase(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.read(16) == QByteArray("SQLite format 3\0", 16);
}

// Opens the database as one, has what its log holds written into the file, and closes it, so
// SQLite itself takes the companions away. Only called for a file that begins as a database.
bool settle(const QString &path)
{
    static int count = 0;
    const QString connection = QStringLiteral("linuxbee-adopt-%1").arg(++count);
    bool settled = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open()) {
            {
                QSqlQuery query(db);
                // A read first: it is what makes SQLite recover a log or a journal left behind.
                settled = query.exec(QStringLiteral("SELECT count(*) FROM sqlite_master"));
                query.finish();
                if (settled)
                    settled = query.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
                query.finish();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return settled;
}

}

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

QString olderFilesInUse(const QString &folder)
{
    const QString inner = folder + QLatin1Char('/') + applicationSettingsName();
    const QFileInfo innerInfo(inner);
    // A link is never touched, so nothing under it is in the way.
    if (innerInfo.isSymLink() || !innerInfo.isDir())
        return QString();
    // /proc shows the real path of an open file.
    const QString real = innerInfo.canonicalFilePath();
    if (real.isEmpty())
        return QString();
    const QString prefix = real + QLatin1Char('/');
    const QString self = QString::number(QCoreApplication::applicationPid());

    const QStringList processes
        = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &process : processes) {
        bool numeric = false;
        process.toLongLong(&numeric);
        if (!numeric || process == self)
            continue;
        const QString fdFolder = QStringLiteral("/proc/") + process + QStringLiteral("/fd");
        // Another user's programs cannot be read, and are not seen.
        const QStringList descriptors = QDir(fdFolder).entryList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QString &descriptor : descriptors) {
            char target[PATH_MAX];
            const ssize_t length = ::readlink(
                QFile::encodeName(fdFolder + QLatin1Char('/') + descriptor).constData(), target,
                sizeof(target) - 1);
            if (length <= 0)
                continue;
            const QString opened = QFile::decodeName(QByteArray(target, length));
            if (opened.startsWith(prefix)) {
                return QCoreApplication::translate("profile",
                    "linuxbee: already running: another program has %1 open (nothing was moved)")
                    .arg(opened);
            }
        }
    }
    return QString();
}

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
    const QSet<QString> have(names.cbegin(), names.cend());

    const auto leave = [&](const QString &name, const QString &reason) {
        left.append(QCoreApplication::translate("profile", "linuxbee: left in place: %1 (%2)")
                .arg(inner + QLatin1Char('/') + name, reason));
    };
    // The companions of a database entry that are in the older folder.
    const auto companionsOf = [&](const QString &name) {
        QStringList found;
        for (const QString &ending : companionEndings()) {
            if (have.contains(name + ending))
                found.append(name + ending);
        }
        return found;
    };

    for (const QString &name : names) {
        const QString base = baseOf(name);
        if (!base.isEmpty()) {
            // A companion goes with its database, and never by itself.
            if (!have.contains(base) || !baseOf(base).isEmpty())
                leave(
                    name, QCoreApplication::translate("profile", "there is no database beside it"));
            continue;
        }

        const QString from = inner + QLatin1Char('/') + name;
        const QString to = folder + QLatin1Char('/') + name;
        const QStringList companions = companionsOf(name);
        const auto leaveWhole = [&](const QString &reason) {
            leave(name, reason);
            for (const QString &companion : companions)
                leave(
                    companion, QCoreApplication::translate("profile", "it goes with %1").arg(name));
        };

        // Nothing of the name, and no companion's name of it, may be in the folder above.
        QString taken;
        if (present(to)) {
            taken = to;
        } else {
            for (const QString &ending : companionEndings()) {
                if (present(to + ending)) {
                    taken = to + ending;
                    break;
                }
            }
        }
        if (!taken.isEmpty()) {
            leaveWhole(QCoreApplication::translate("profile", "%1 is there").arg(taken));
            continue;
        }

        // Settled first: only a file that begins as a database is ever opened as one.
        if (!companions.isEmpty()) {
            if (!beginsAsDatabase(from)) {
                leaveWhole(QCoreApplication::translate(
                    "profile", "it has a companion beside it and does not begin as a database"));
                continue;
            }
            const bool settled = settle(from);
            bool stays = false;
            for (const QString &ending : companionEndings())
                stays = stays || present(from + ending);
            if (!settled || stays) {
                // What stays, stays whole; a name taken away by SQLite is not named again.
                QStringList remaining;
                for (const QString &companion : companions) {
                    if (present(inner + QLatin1Char('/') + companion))
                        remaining.append(companion);
                }
                leave(name, QCoreApplication::translate("profile", "it could not be settled"));
                for (const QString &companion : std::as_const(remaining))
                    leave(companion,
                        QCoreApplication::translate("profile", "it goes with %1").arg(name));
                continue;
            }
        }

        // RENAME_NOREPLACE: a name that is there (a file, a folder, a link, even a dangling
        // one) is never replaced, and the check and the move are one step.
        const int renamed = ::renameat2(AT_FDCWD, QFile::encodeName(from).constData(), AT_FDCWD,
            QFile::encodeName(to).constData(), RENAME_NOREPLACE);
        if (renamed == 0)
            continue;
        const int why = errno;
        if (why == EEXIST)
            leaveWhole(QCoreApplication::translate("profile", "%1 is there").arg(to));
        else
            leaveWhole(QString::fromLocal8Bit(std::strerror(why)));
    }
    // rmdir takes away an empty folder and nothing else.
    QDir().rmdir(inner);
    return left;
}

QString libraryNotTakenOver(const QString &dataFolder)
{
    const QString waiting
        = dataFolder + QLatin1Char('/') + applicationSettingsName() + QStringLiteral("/library.db");
    const QString wanted = dataFolder + QStringLiteral("/library.db");
    // exists() follows links: the older folder may be one, and is looked through this once.
    if (!QFileInfo::exists(waiting) || QFileInfo::exists(wanted))
        return QString();
    return QCoreApplication::translate("profile",
        "linuxbee: the library of an older build was not taken over: %1 waits there and %2 "
        "is not made, because it could not be moved (see the lines above); nothing was started "
        "so that no empty library is shown")
        .arg(QDir::toNativeSeparators(waiting), QDir::toNativeSeparators(wanted));
}

}
