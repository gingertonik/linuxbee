#include "transfer.h"
#include "transfer_io.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

#include <cerrno>
#include <cstdio>
#include <vector>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
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

// O-EXECUTE, a source that is a link: true when the source is a link that leads to a file
// now and would not lead to that same file (device and file number) from the target's
// folder. A text that begins at the root leads where it led; a relative one is read from the
// target's folder. A link that leads nowhere already, a source that is not a link, and a
// target on another file system (the move across refuses links itself) are not asked.
bool linkWouldBreak(const QByteArray &from, const QByteArray &to)
{
    struct stat own { };
    if (::lstat(from.constData(), &own) != 0 || !S_ISLNK(own.st_mode))
        return false;
    struct stat leadsTo { };
    if (::stat(from.constData(), &leadsTo) != 0)
        return false;
    QByteArray text(4096, '\0');
    const ssize_t n = ::readlink(from.constData(), text.data(), size_t(text.size()));
    if (n <= 0 || n >= text.size())
        return false;
    text.truncate(int(n));
    if (text.startsWith('/'))
        return false;
    const qsizetype slash = to.lastIndexOf('/');
    const QByteArray folder
        = slash > 0 ? to.left(slash) : (slash == 0 ? QByteArray("/") : QByteArray("."));
    struct stat folderInfo { };
    if (::stat(folder.constData(), &folderInfo) == 0 && folderInfo.st_dev != own.st_dev)
        return false;
    struct stat there { };
    return ::stat((folder + '/' + text).constData(), &there) != 0 || !sameFile(there, leadsTo);
}

// The spare name of a copy: `.lbmove-` and six characters of a-z and 0-9, in the target's
// folder (O-SPARE). Nothing of the file's own name is in it.
QByteArray spareName(const QByteArray &folder)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    QByteArray name = folder + "/.lbmove-";
    for (int i = 0; i < 6; ++i)
        name.append(alphabet[QRandomGenerator::system()->bounded(36)]);
    return name;
}

// O-ACROSS and O-SPARE: a move to another file system, and every Copy. Copies the bytes to
// a file made new under a spare name in the target's folder, checks that the source did not
// change and the copy has its size, gives the copy the source's mode and time, flushes it
// and closes it, and only then gives it the target's name, by a call that never replaces.
// Then the folder is flushed, the source closed and, for a move, removed (after it only
// descriptors are let go). The spare file is made here (O_EXCL), so a failure removes it by
// the name it has at that moment, and a refusal to make it removes nothing. A descriptor of
// the move's own stays on the copy until the call returns, so the name is looked at and
// removed while the file is held.
QString moveAcross(const TransferIo &io, const QString &source, const QString &target, bool move)
{
    const QByteArray from = QFile::encodeName(source);
    const QByteArray to = QFile::encodeName(target);
    const QString couldNotCopy = QStringLiteral("could not copy the file");
    const QString targetThere = QStringLiteral("the target already exists");

    // A move does not follow a link: it stays where it is, and the move fails. A Copy reads
    // what a link points to.
    struct stat before { };
    if ((move ? ::lstat(from.constData(), &before) : ::stat(from.constData(), &before)) != 0
        || !S_ISREG(before.st_mode))
        return couldNotCopy;

    Fd in(io, ::open(from.constData(), O_RDONLY | O_CLOEXEC | (move ? O_NOFOLLOW : 0)));
    if (in.fd < 0)
        return couldNotCopy;

    // One look before anything is made: anything at the target (a link too, a dangling one
    // too) fails the transfer, and nothing is read.
    {
        struct stat there { };
        if (::lstat(to.constData(), &there) == 0 || errno != ENOENT)
            return targetThere;
    }

    // O_EXCL: never an existing file, and never through a link at the spare name.
    const QByteArray folder = QFile::encodeName(QFileInfo(target).absolutePath());
    QByteArray name;
    Fd out(io, -1);
    for (int attempt = 0; attempt < 100 && out.fd < 0; ++attempt) {
        name = spareName(folder);
        out.fd = ::open(name.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (out.fd < 0 && errno != EEXIST)
            return couldNotCopy;
    }
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
        if (::lstat(name.constData(), &now) == 0 && S_ISREG(now.st_mode) && now.st_size == 0)
            io.unlink(name.constData());
        return couldNotCopy;
    }

    // From here the copy is ours, and every failure removes it. `name` is the name the copy
    // has now: the spare one, and after the naming the target's. It is looked at and removed
    // only while `held` is open, and only while it still holds the file made here; the
    // descriptor is let go after that.
    const auto undo = [&](const QString &said) {
        out.close();
        struct stat now { };
        if (::lstat(name.constData(), &now) == 0 && sameFile(now, made))
            io.unlink(name.constData());
        held.close();
        return said;
    };

    // 1. the bytes: first the disk is asked to share them with the spare file (to, then
    // from). When it does, nothing is read or written. Any other answer, and nothing is
    // said of it: the bytes are copied. The checks below stay for both.
    int why = 0;
    const qint64 copied
        = io.clone(out.fd, in.fd) == 0 ? qint64(before.st_size) : copyBytes(io, in.fd, out.fd, why);
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

    // 4. the copy is flushed to the disk and closed: it is whole
    if (io.fsync(out.fd) != 0)
        return undo(QStringLiteral("could not flush the copy to the disk"));
    if (!out.close())
        return undo(QStringLiteral("could not close the copy"));

    // 5. O-SPARE: the copy is given the target's name, by a call that never replaces.
    // Something that has come to be there meanwhile is somebody else's: it is left as it is.
    {
        const QByteArray spare = name;
        bool named = false;
        if (io.renameNoReplace(spare.constData(), to.constData()) == 0) {
            named = true;
        } else {
            switch (errno) {
            case EEXIST:
                return undo(targetThere);
            case EINVAL:
                // The never-replacing rename is not known: a second name (link never
                // replaces), then the spare name removed.
                if (io.link(spare.constData(), to.constData()) == 0) {
                    named = true;
                    // The file now has two names. Taking the spare one away failing loses
                    // nothing: the target's name holds the whole file.
                    io.unlink(spare.constData());
                } else {
                    const int why = errno;
                    if (why == EEXIST)
                        return undo(targetThere);
                    if (why == EPERM || why == EOPNOTSUPP || why == ENOSYS) {
                        // No second names here. One more look that nothing is at the
                        // target, then the plain rename.
                        struct stat there { };
                        if (::lstat(to.constData(), &there) == 0 || errno != ENOENT)
                            return undo(targetThere);
                        named = io.rename(spare.constData(), to.constData()) == 0;
                    }
                }
                break;
            default:
                break;
            }
        }
        if (!named)
            return undo(couldNotCopy);
        name = to;
    }

    // 6. the folder is flushed, which puts the name on the disk; then the original is closed
    {
        Fd dir(io, ::open(folder.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (dir.fd < 0 || io.fsync(dir.fd) != 0)
            return undo(QStringLiteral("could not flush the copy's folder to the disk"));
        if (!dir.close())
            return undo(QStringLiteral("could not close the copy's folder"));
    }
    if (!in.close())
        return undo(QStringLiteral("could not close the original"));

    // 7. a Copy leaves the source alone. A move removes it, last; if it cannot, the copy is
    // taken back (still held). After the removal only the move's own descriptor is let go.
    if (move && io.unlink(from.constData()) != 0) {
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
            return moveAcross(io, source, target, true);
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
        if (linkWouldBreak(from, to))
            return QStringLiteral("the link would not lead to its file from there");
        if (io.renameNoReplace(from.constData(), to.constData()) == 0)
            return { };
        switch (errno) {
        case EXDEV:
            return moveAcross(io, source, target, true);
        case EINVAL:
            return moveByLink(io, source, target);
        default:
            return QStringLiteral("could not move the file");
        }
    }

    // A Copy is made the careful way of a move to another file system (O-SPARE), less the
    // rename of the source and the removal of it.
    return moveAcross(io, source, target, false);
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
    , clone([](int to, int from) { return ::ioctl(to, FICLONE, from); })
{
}

}
