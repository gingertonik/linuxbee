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

// A descriptor that is closed once, through the calls given. close() says whether the
// close itself went well; the descriptor counts as closed either way (a close that
// reports an error has still closed). What is left open goes out of scope closed.
struct Fd {
    const TransferIo &io;
    int fd = -1;
    Fd(const TransferIo &calls, int value)
        : io(calls)
        , fd(value)
    {
    }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    ~Fd() { close(); }
    bool close()
    {
        const int value = fd;
        fd = -1;
        return value < 0 || io.close(value) == 0;
    }
};

bool writeAll(const TransferIo &io, int fd, const char *data, qint64 size)
{
    while (size > 0) {
        const ssize_t n = io.write(fd, data, size_t(size));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false; // no progress: never loop on it
        data += n;
        size -= n;
    }
    return true;
}

// Copies the bytes of `from` to `to` and returns how many went. -1 on any failure, and
// `why` is then the errno of the call that failed.
qint64 copyBytes(const TransferIo &io, int from, int to, int &why)
{
    std::vector<char> buffer(1024 * 1024);
    qint64 total = 0;
    for (;;) {
        const ssize_t n = io.read(from, buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EINTR)
                continue;
            why = errno;
            return -1;
        }
        if (n == 0)
            return total;
        if (!writeAll(io, to, buffer.data(), n)) {
            why = errno;
            return -1;
        }
        total += n;
    }
}

bool sameFile(const struct stat &a, const struct stat &b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

// The source could not be removed. Normally it is still there and the copy (or the
// second name) is taken back. If the source is gone (somebody else removed it, whatever
// the call said), the copy is the only file left and stays.
bool sourceStillThere(const QByteArray &from)
{
    struct stat st { };
    return ::lstat(from.constData(), &st) == 0 || errno != ENOENT;
}

// O-ACROSS, the target is on another file system. Copies, checks that the source did not
// change and the copy has its size, gives the copy the source's mode and time, flushes it
// and its folder, closes everything, and only then removes the source (after it only
// descriptors are let go). The target is made here (O_EXCL), so a failure removes it,
// and a refusal to make it removes nothing. A descriptor of the move's own stays on the
// copy until the move returns, so the name is looked at and removed while the file is held.
QString moveAcross(const TransferIo &io, const QString &source, const QString &target)
{
    const QByteArray from = QFile::encodeName(source);
    const QByteArray to = QFile::encodeName(target);
    const QString couldNotCopy = QStringLiteral("could not copy the file");

    // A link is not followed and copied: it stays where it is, and the move fails.
    struct stat before { };
    if (::lstat(from.constData(), &before) != 0 || !S_ISREG(before.st_mode))
        return couldNotCopy;

    Fd in(io, ::open(from.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (in.fd < 0)
        return couldNotCopy;

    // O_EXCL: never an existing file, and never through a link at the target.
    Fd out(io, ::open(to.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    if (out.fd < 0)
        return couldNotCopy;
    // The move's own descriptor on the copy, kept until the move returns: while it is open
    // the file's number cannot pass to another file.
    Fd held(io, ::fcntl(out.fd, F_DUPFD_CLOEXEC, 0));
    struct stat made { };
    if (held.fd < 0 || ::fstat(out.fd, &made) != 0) {
        // The file is ours, but it cannot be held or told from another one later: nothing
        // has been written, so take it back only while the name still holds an empty file.
        out.close();
        struct stat now { };
        if (::lstat(to.constData(), &now) == 0 && S_ISREG(now.st_mode) && now.st_size == 0)
            io.unlink(to.constData());
        return couldNotCopy;
    }

    // From here the target is ours, and every failure removes it. The name is looked at
    // and removed only while `held` is open, and only while it still holds the file made
    // here; the descriptor is let go after that.
    const auto undo = [&](const QString &said) {
        out.close();
        struct stat now { };
        if (::lstat(to.constData(), &now) == 0 && sameFile(now, made))
            io.unlink(to.constData());
        held.close();
        return said;
    };

    // 1. the bytes
    int why = 0;
    const qint64 copied = copyBytes(io, in.fd, out.fd, why);
    if (copied < 0)
        return undo(why == ENOSPC || why == EDQUOT
                ? QStringLiteral("there is no room for the file at the target")
                : couldNotCopy);

    // 2. the source is as it was (size, modification and change time), and the copy has
    // its size
    struct stat now { }, written { };
    if (::fstat(in.fd, &now) != 0 || ::fstat(out.fd, &written) != 0)
        return undo(QStringLiteral("could not read the size of the copy"));
    if (now.st_mtim.tv_sec != before.st_mtim.tv_sec || now.st_mtim.tv_nsec != before.st_mtim.tv_nsec
        || now.st_ctim.tv_sec != before.st_ctim.tv_sec
        || now.st_ctim.tv_nsec != before.st_ctim.tv_nsec)
        return undo(QStringLiteral("the file changed while it was copied"));
    if (written.st_size != now.st_size || copied != now.st_size || now.st_size != before.st_size)
        return undo(QStringLiteral("the copy is not the size of the file"));

    // 3. the source's mode and time (set on the open descriptor, so a mode with no
    // write bit does not get in the way)
    if (io.fchmod(out.fd, before.st_mode & 07777) != 0)
        return undo(QStringLiteral("could not keep the mode"));
    const struct timespec times[2] = { before.st_atim, before.st_mtim };
    if (io.futimens(out.fd, times) != 0)
        return undo(QStringLiteral("could not keep the modification time"));

    // 4. the copy, then its folder, are flushed to the disk
    if (io.fsync(out.fd) != 0)
        return undo(QStringLiteral("could not flush the copy to the disk"));
    {
        const QByteArray folder = QFile::encodeName(QFileInfo(target).absolutePath());
        Fd dir(io, ::open(folder.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (dir.fd < 0 || io.fsync(dir.fd) != 0)
            return undo(QStringLiteral("could not flush the copy's folder to the disk"));
        if (!dir.close())
            return undo(QStringLiteral("could not close the copy's folder"));
    }

    // 5. every close is checked, before the source is touched
    if (!out.close())
        return undo(QStringLiteral("could not close the copy"));
    if (!in.close())
        return undo(QStringLiteral("could not close the original"));

    // 6. only now the source goes; if it cannot, the copy is taken back (still held). After
    // the removal only the move's own descriptor is let go.
    if (io.unlink(from.constData()) != 0) {
        if (!sourceStillThere(from))
            return { };
        return undo(QStringLiteral("could not remove the original after copying"));
    }
    return { };
}

// O-ACROSS, where the never-replacing rename is not known (EINVAL): the file is given a
// second name at the target (link, which never replaces) and the first name removed;
// if that fails the second name is taken back.
QString moveByLink(const TransferIo &io, const QString &source, const QString &target)
{
    const QByteArray from = QFile::encodeName(source);
    const QByteArray to = QFile::encodeName(target);

    if (io.link(from.constData(), to.constData()) != 0) {
        const int why = errno;
        if (why == EXDEV)
            return moveAcross(io, source, target);
        if (why == EPERM || why == EOPNOTSUPP || why == ENOSYS) {
            // No second names here. One more look that nothing is at the target (a link
            // counts, and a dangling one too), then the plain rename.
            struct stat st { };
            if (::lstat(to.constData(), &st) == 0 || errno != ENOENT)
                return QStringLiteral("the target already exists");
            if (io.rename(from.constData(), to.constData()) == 0)
                return { };
        }
        return QStringLiteral("could not move the file");
    }

    if (io.unlink(from.constData()) == 0)
        return { };
    // The first name stays: take the second back, but only if it is still the same
    // file, and never when the first name is gone (then the second is all that is left).
    if (!sourceStillThere(from))
        return { };
    struct stat a { }, b { };
    if (::lstat(from.constData(), &a) == 0 && ::lstat(to.constData(), &b) == 0 && sameFile(a, b))
        io.unlink(to.constData());
    return QStringLiteral("could not move the file");
}

}

QString transferFile(const QString &source, const QString &target, bool move)
{
    return transferFileWith(TransferIo(), source, target, move);
}

QString transferFileWith(
    const TransferIo &io, const QString &source, const QString &target, bool move)
{
    if (QFileInfo::exists(target))
        return QStringLiteral("the target already exists");

    if (move) {
        // The system's own rename, asked never to replace. It never copies: across
        // file systems it answers EXDEV and nothing else happens.
        const QByteArray from = QFile::encodeName(source);
        const QByteArray to = QFile::encodeName(target);
        if (io.renameNoReplace(from.constData(), to.constData()) == 0)
            return { };
        switch (errno) {
        case EXDEV:
            return moveAcross(io, source, target);
        case EINVAL:
            return moveByLink(io, source, target);
        default:
            return QStringLiteral("could not move the file");
        }
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

// O-IO: the real calls.
TransferIo::TransferIo()
    : renameNoReplace([](const char *from, const char *to) {
        return ::renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE);
    })
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

}
