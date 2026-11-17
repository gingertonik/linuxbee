#include "transfer.h"
#include "transfer_io.h"

#include <QByteArray>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>

#include <cerrno>
#include <cstdio>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace linuxbee::organise {

namespace {

// Closes a descriptor when it goes out of scope.
struct Fd {
    int fd = -1;
    explicit Fd(int value)
        : fd(value)
    {
    }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    ~Fd()
    {
        if (fd >= 0)
            ::close(fd);
    }
    // Closes now, and says whether the close itself went well.
    bool close()
    {
        const int value = fd;
        fd = -1;
        return value >= 0 && ::close(value) == 0;
    }
};

bool writeAll(int fd, const char *data, qint64 size)
{
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size_t(size));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += n;
        size -= n;
    }
    return true;
}

// Copies the bytes of `from` to `to` and returns how many went. -1 on any failure.
qint64 copyBytes(int from, int to)
{
    std::vector<char> buffer(1024 * 1024);
    qint64 total = 0;
    for (;;) {
        const ssize_t n = ::read(from, buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return total;
        if (!writeAll(to, buffer.data(), n))
            return -1;
        total += n;
    }
}

// O-ACROSS, the target is on another file system. Copies, flushes, checks the
// size, gives the copy the source's mode and time, and only then removes the
// source. The target is made here (it did not exist), so a failure removes it.
QString moveAcross(const QString &source, const QString &target)
{
    const QByteArray from = QFile::encodeName(source);
    const QByteArray to = QFile::encodeName(target);

    // A link is not followed and copied: it stays where it is, and the move fails.
    struct stat before { };
    if (::lstat(from.constData(), &before) != 0 || !S_ISREG(before.st_mode))
        return QStringLiteral("could not copy the file");

    Fd in(::open(from.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (in.fd < 0)
        return QStringLiteral("could not copy the file");

    // O_EXCL: never an existing file, and never through a link at the target.
    Fd out(::open(to.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    if (out.fd < 0)
        return QStringLiteral("could not copy the file");

    // From here the target is ours (it did not exist), and every failure removes it.
    const auto undo = [&to](const QString &said) {
        ::unlink(to.constData());
        return said;
    };

    // 1. the bytes
    const qint64 copied = copyBytes(in.fd, out.fd);
    if (copied < 0)
        return undo(QStringLiteral("could not copy the file"));

    // 2. the copy, then its folder, are flushed to the disk
    if (::fsync(out.fd) != 0)
        return undo(QStringLiteral("could not flush the copy to the disk"));
    const QByteArray folder = QFile::encodeName(QFileInfo(target).absolutePath());
    {
        Fd dir(::open(folder.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (dir.fd < 0 || ::fsync(dir.fd) != 0)
            return undo(QStringLiteral("could not flush the copy's folder to the disk"));
    }

    // 3. the copy has the size of the source
    struct stat now { }, written { };
    if (::fstat(in.fd, &now) != 0 || ::fstat(out.fd, &written) != 0)
        return undo(QStringLiteral("could not read the size of the copy"));
    if (written.st_size != now.st_size || copied != now.st_size || now.st_size != before.st_size)
        return undo(QStringLiteral("the copy is not the size of the file"));

    // 4. the source's mode and time (set on the open descriptor, so a mode
    // with no write bit does not get in the way)
    if (::fchmod(out.fd, before.st_mode & 07777) != 0)
        return undo(QStringLiteral("could not keep the mode"));
    const struct timespec times[2] = { before.st_atim, before.st_mtim };
    if (::futimens(out.fd, times) != 0)
        return undo(QStringLiteral("could not keep the modification time"));
    if (!out.close())
        return undo(QStringLiteral("could not close the copy"));

    // 5. only now the source goes; if it cannot, the copy is taken back
    if (::unlink(from.constData()) != 0)
        return undo(QStringLiteral("could not remove the original after copying"));
    return { };
}

}

QString transferFile(const QString &source, const QString &target, bool move)
{
    if (QFileInfo::exists(target))
        return QStringLiteral("the target already exists");

    if (move) {
        // The system's own rename, asked never to replace. It never copies:
        // across file systems it answers EXDEV and nothing else happens.
        const QByteArray from = QFile::encodeName(source);
        const QByteArray to = QFile::encodeName(target);
        if (::renameat2(AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), RENAME_NOREPLACE)
            == 0)
            return { };
        if (errno != EXDEV)
            return QStringLiteral("could not move the file");
        return moveAcross(source, target);
    }

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
    return { };
}

// O-IO. (Stub: the real calls, and the calls given are not used yet.)
TransferIo::TransferIo()
    : renameNoReplace([](const char *from, const char *to) { return ::renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE); })
    , link([](const char *from, const char *to) { return ::link(from, to); })
    , rename([](const char *from, const char *to) { return ::rename(from, to); })
    , read([](int fd, void *buffer, size_t size) { return ::read(fd, buffer, size); })
    , write([](int fd, const void *buffer, size_t size) { return ::write(fd, buffer, size); })
    , fchmod([](int fd, mode_t mode) { return ::fchmod(fd, mode); })
    , futimens([](int fd, const struct timespec times[2]) { return ::futimens(fd, times); })
    , fsync([](int fd) { return ::fsync(fd); })
    , close([](int fd) { return ::close(fd); })
    , unlink([](const char *path) { return ::unlink(path); })
{
}

QString transferFileWith(const TransferIo &, const QString &source, const QString &target, bool move)
{
    return transferFile(source, target, move);
}

}
