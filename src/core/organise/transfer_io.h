#pragma once
#include <QString>

#include <functional>

#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

// The system calls a transfer makes, so that a test can make any one of them
// fail and see that no file is lost. Contract: docs/spec/organise.md, O-IO.
// Nothing but the organiser's transfer and its tests uses this.
namespace linuxbee::organise {

// Each member answers as the system call of its name does: 0 or a count, or
// -1 with errno set. A default-made TransferIo is the real calls.
struct TransferIo {
    std::function<int(const char *from, const char *to)>
        renameNoReplace; // renameat2(RENAME_NOREPLACE)
    std::function<int(const char *from, const char *to)> link;
    std::function<int(const char *from, const char *to)> rename;
    std::function<ssize_t(int fd, void *buffer, size_t size)> read;
    std::function<ssize_t(int fd, const void *buffer, size_t size)> write;
    std::function<int(int fd, mode_t mode)> fchmod;
    std::function<int(int fd, const struct timespec times[2])> futimens;
    std::function<int(int fd)> fsync;
    std::function<int(int fd)> close;
    std::function<int(const char *path)> unlink;
    // ioctl(FICLONE): the disk is asked to share the bytes of `from` with `to`. All or nothing.
    std::function<int(int to, int from)> clone;

    TransferIo(); // the real calls
};

// transferFile (transfer.h), making every one of the calls above through `io`.
QString transferFileWith(
    const TransferIo &io, const QString &source, const QString &target, bool move);

}
