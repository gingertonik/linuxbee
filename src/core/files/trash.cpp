#include "files/trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>

#include <cerrno>
#include <vector>

#include <fcntl.h>
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

// The checks and the two removals of takeBackSecondName, on handles already opened.
static void removeSecondName(int filesFd, int infoFd, const QString &real,
    const struct stat &source, const QString &placeNamed, const QString &name)
{
    // The folder of the place named is that very `files` folder (same device and file number).
    struct stat files;
    struct stat placeFolder;
    if (::fstat(filesFd, &files) != 0
        || ::stat(QFile::encodeName(QFileInfo(placeNamed).absolutePath()).constData(), &placeFolder)
            != 0
        || placeFolder.st_dev != files.st_dev || placeFolder.st_ino != files.st_ino) {
        return;
    }
    // Never the source's own name: a file sent from inside `files` is its own place.
    struct stat sourceFolder;
    if (::stat(QFile::encodeName(QFileInfo(real).absolutePath()).constData(), &sourceFolder) == 0
        && sourceFolder.st_dev == files.st_dev && sourceFolder.st_ino == files.st_ino
        && name == QFileInfo(real).fileName()) {
        return;
    }
    // Not a link, and the very same file as the source: same device, same file number.
    const QByteArray bare = QFile::encodeName(name);
    struct stat other;
    if (::fstatat(filesFd, bare.constData(), &other, AT_SYMLINK_NOFOLLOW) != 0
        || S_ISLNK(other.st_mode) || other.st_dev != source.st_dev
        || other.st_ino != source.st_ino) {
        return;
    }
    if (::unlinkat(filesFd, bare.constData(), 0) != 0)
        return;
    const QByteArray record = QFile::encodeName(name + QStringLiteral(".trashinfo"));
    ::unlinkat(infoFd, record.constData(), 0);
}

// FD-STAYED: the library said "moved" and the file is still at `path`. If the place it named is a
// second name for this very file inside this trash's own files folder, that name and its record
// are taken away. Every condition must hold, or nothing is removed. Both removals go through
// handles on the trash's own `files` and `info` folders, opened so that a link is refused, with a
// bare name: the two unlinkat calls below are the only removals in this file, and neither can
// lead out of the trash. The source's own name is refused outright.
static void takeBackSecondName(
    const QString &trash, const QString &real, const struct stat &source, const QString &placeNamed)
{
    if (placeNamed.isEmpty())
        return;
    const QString name = QFileInfo(placeNamed).fileName();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return;
    // The trash folder itself, or a folder above it, may be a link: that is still this trash.
    const int trashFd = ::open(QFile::encodeName(trash).constData(), O_RDONLY | O_DIRECTORY);
    if (trashFd < 0)
        return;
    // `files` and `info` are entries of the trash and must be real folders, not links.
    const int filesFd = ::openat(trashFd, "files", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    const int infoFd = ::openat(trashFd, "info", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    ::close(trashFd);
    if (filesFd >= 0 && infoFd >= 0)
        removeSecondName(filesFd, infoFd, real, source, placeNamed, name);
    if (filesFd >= 0)
        ::close(filesFd);
    if (infoFd >= 0)
        ::close(infoFd);
}

// FD-TAKEN: the desktop library knows which names in a trash are taken by the records in `info`
// alone, so a file in `files` with no record would be written over. Where there is such a name, an
// empty record is made for it (new, never one that is there) and kept open while the library
// works; letGo takes each record made here away again, only while the name still holds the very
// file made here and it is still empty. Both folders are opened so that a link is refused.
namespace {

class NameHolds {
public:
    NameHolds() = default;
    NameHolds(const NameHolds &) = delete;
    NameHolds &operator=(const NameHolds &) = delete;
    ~NameHolds() { letGo(); }

    // False when a name in this trash is taken with no record and the record cannot be made.
    bool hold(const QString &trash, const QByteArray &name, const struct stat *source)
    {
        // A trash that cannot be opened is one the library cannot write into either.
        const int trashFd = ::open(QFile::encodeName(trash).constData(), O_RDONLY | O_DIRECTORY);
        if (trashFd < 0)
            return true;
        const int filesFd = ::openat(trashFd, "files", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        const int infoFd = ::openat(trashFd, "info", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        ::close(trashFd);
        bool ok = true;
        struct stat there;
        // lstat: a link or a folder of that name counts, and so does a link that points at nothing.
        const bool taken = filesFd >= 0
            && ::fstatat(filesFd, name.constData(), &there, AT_SYMLINK_NOFOLLOW) == 0
            && !(source && there.st_dev == source->st_dev && there.st_ino == source->st_ino);
        if (filesFd >= 0)
            ::close(filesFd);
        if (taken) {
            if (infoFd < 0) {
                ok = false;
            } else {
                const size_t before = m_held.size();
                ok = keep(infoFd, name + QByteArrayLiteral(".trashinfo"));
                if (ok && m_held.size() > before)
                    return true; // the folder stays open with the record that was made
            }
        }
        if (infoFd >= 0)
            ::close(infoFd);
        return ok;
    }

    void letGo()
    {
        for (const Held &held : m_held) {
            struct stat now;
            if (::fstatat(held.infoFd, held.record.constData(), &now, AT_SYMLINK_NOFOLLOW) == 0
                && S_ISREG(now.st_mode) && now.st_dev == held.made.st_dev
                && now.st_ino == held.made.st_ino && now.st_size == 0) {
                ::unlinkat(held.infoFd, held.record.constData(), 0);
            }
            ::close(held.fd);
            ::close(held.infoFd);
        }
        m_held.clear();
    }

private:
    struct Held {
        int infoFd;
        int fd;
        QByteArray record;
        struct stat made;
    };

    // The record of the name, made empty and new. True also when one is already there.
    bool keep(int infoFd, const QByteArray &record)
    {
        struct stat st;
        if (::fstatat(infoFd, record.constData(), &st, AT_SYMLINK_NOFOLLOW) == 0)
            return true;
        const int fd = ::openat(
            infoFd, record.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0)
            return errno == EEXIST;
        Held held { infoFd, fd, record, { } };
        if (::fstat(fd, &held.made) != 0) {
            ::unlinkat(infoFd, record.constData(), 0); // the empty file this call just made
            ::close(fd);
            return false;
        }
        m_held.push_back(held);
        return true;
    }

    std::vector<Held> m_held;
};

}

// Sends one file to the desktop's trash, or to the stand-in for it, and says what became of it.
// QFile::moveToTrash fails while the trash folders are missing, as on a fresh account, so they are
// made first, the way a file manager makes them.
SendResult sendToTrashWith(const QString &path, QString *pathInTrash, const TrashMover &mover)
{
    // FD-SEND: a name that is empty, ends in "/" or is a folder is never handed over.
    if (path.isEmpty() || path.endsWith(QLatin1Char('/')) || QFileInfo(path).isDir())
        return SendResult::NotTaken;
    const QString folder = usableFolder(path);
    if (folder.isEmpty())
        return SendResult::NotTaken;
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        return SendResult::NotTaken;
    const QString trash = dataHome + QStringLiteral("/Trash");
    if (!QDir().mkpath(trash + QStringLiteral("/files"))
        || !QDir().mkpath(trash + QStringLiteral("/info"))) {
        return SendResult::NotTaken;
    }
    // The folder's real path and the file's name: the trash record names where the file was.
    const QString real = folder + QLatin1Char('/') + QFileInfo(path).fileName();
    // FD-TAKEN: every trash the file can go to, the one of the data folder and the two on the
    // file's own disk, all three whichever the library picks. A name that is there without a record
    // is held.
    struct stat sent;
    const bool haveSent = ::lstat(QFile::encodeName(real).constData(), &sent) == 0;
    QStringList trashes { trash };
    const QString top = QStorageInfo(folder).rootPath();
    if (!top.isEmpty()) {
        const QString uid = QString::number(::getuid());
        struct stat shared;
        if (::lstat(QFile::encodeName(QDir::cleanPath(top + QStringLiteral("/.Trash"))).constData(),
                &shared)
                == 0
            && S_ISDIR(shared.st_mode)) {
            trashes.append(QDir::cleanPath(top + QStringLiteral("/.Trash/") + uid));
        }
        trashes.append(QDir::cleanPath(top + QStringLiteral("/.Trash-") + uid));
    }
    NameHolds holds;
    const QByteArray bareName = QFile::encodeName(QFileInfo(path).fileName());
    for (const QString &one : trashes) {
        if (!holds.hold(one, bareName, haveSent ? &sent : nullptr))
            return SendResult::NotTaken;
    }
    QString placeNamed;
    const bool said = mover ? mover(real, &placeNamed) : QFile::moveToTrash(real, &placeNamed);
    holds.letGo(); // whatever the library answered
    if (!said)
        return SendResult::NotTaken;
    // FD-SEND 3: a file still at the path was not moved, whatever was said.
    struct stat st;
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0) {
        if (pathInTrash)
            *pathInTrash = placeNamed;
        return SendResult::Went;
    }
    // FD-STAYED: take back the second name the trash may hold for it, and nothing else.
    takeBackSecondName(trash, real, st, placeNamed);
    return SendResult::StillThere;
}

bool sendToTrash(const QString &path, QString *pathInTrash)
{
    return sendToTrashWith(path, pathInTrash, TrashMover()) == SendResult::Went;
}

// FD-TRASH. A file is never opened or read to decide its outcome. Nothing here removes a file or a
// folder: a regular file goes to sendToTrashWith, and every other answer leaves the path as it was.
QList<TrashOutcome> moveFilesToTrashWith(const QStringList &paths, const TrashMover &mover)
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
            } else {
                const SendResult sent = sendToTrashWith(path, &inTrash, mover);
                if (sent == SendResult::Went) {
                    outcome.status = TrashOutcome::Status::Trashed;
                    outcome.pathInTrash = inTrash;
                } else {
                    outcome.status = TrashOutcome::Status::Failed;
                    outcome.reason = sent == SendResult::StillThere
                        ? QStringLiteral("the file is still in its place")
                        : QStringLiteral("the trash did not take it");
                }
            }
        }
        outcomes.append(outcome);
    }
    return outcomes;
}

QList<TrashOutcome> moveFilesToTrash(const QStringList &paths)
{
    return moveFilesToTrashWith(paths, TrashMover());
}

}
