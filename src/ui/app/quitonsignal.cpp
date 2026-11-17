#include "app/quitonsignal.h"

#include <QCoreApplication>
#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>

namespace linuxbee::ui {

namespace {

// The write end of the pipe, read by the signal handler. Set before the
// handler is installed; sig_atomic_t is async-signal-safe to read.
volatile sig_atomic_t gWriteFd = -1;

// Does nothing but write one byte: no Qt call, no allocation, no lock.
// errno is kept, since the interrupted code may be about to read it.
extern "C" void onSignal(int)
{
    const int saved = errno;
    const int fd = gWriteFd;
    if (fd >= 0) {
        const char byte = 1;
        const ssize_t written = write(fd, &byte, 1); // full pipe: the quit is already pending
        (void)written;
    }
    errno = saved;
}

bool makeNonBlockingCloseOnExec(int fd)
{
    const int flags = fcntl(fd, F_GETFL);
    const int fdFlags = fcntl(fd, F_GETFD);
    if (flags < 0 || fdFlags < 0)
        return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0
        && fcntl(fd, F_SETFD, fdFlags | FD_CLOEXEC) == 0;
}

}

void quitOnSignals(QCoreApplication *application)
{
    static bool done = false; // called once; a second call changes nothing
    if (done || !application)
        return;

    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
        return;
    if (!makeNonBlockingCloseOnExec(fds[0]) || !makeNonBlockingCloseOnExec(fds[1])) {
        close(fds[0]);
        close(fds[1]);
        return;
    }
    done = true;
    const int readFd = fds[0];
    gWriteFd = fds[1];

    // Parented to the application, so it goes with it. It empties the pipe and
    // quits; the first byte starts the quit, a later one finds quit() already
    // pending or the loop already gone and changes nothing.
    auto *notifier = new QSocketNotifier(readFd, QSocketNotifier::Read, application);
    QObject::connect(notifier, &QSocketNotifier::activated, application, [readFd, application] {
        char buffer[64];
        while (read(readFd, buffer, sizeof buffer) > 0) { }
        application->quit();
    });

    struct sigaction action { };
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (const int signalNumber : { SIGTERM, SIGINT, SIGHUP })
        sigaction(signalNumber, &action, nullptr);
}

}
