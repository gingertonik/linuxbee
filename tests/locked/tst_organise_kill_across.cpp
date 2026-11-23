// LOCKED ACCEPTANCE TEST (a cross-disk move killed at any step leaves the file whole somewhere;
// fix-h1). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/organise.md (O-ACROSS, O-SPARE) and the orchestrator's ruling on H:
// `.lbmove-` is a copy we were making only if no kill at any step of a move to another disk can
// leave it the only copy. A child process makes the move through TransferIo and is killed
// (_exit, as SIGKILL leaves things) at the entry of its K-th call, or straight after it returns,
// for every K until the move ends by itself. After each, the source or the target holds the whole
// file. "Another disk" is the rig's: a rename from outside the target's folder answers EXDEV. All
// files are made here, in a temporary folder.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "organise/transfer_io.h"

using linuxbee::organise::TransferIo;

namespace {

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray folderOf(const char *path)
{
    const QByteArray p(path);
    return p.left(p.lastIndexOf('/'));
}

// The rig's io: every call counted; at call `killAt` the child ends, before the call or after it.
TransferIo killing(int killAt, bool after, bool clone, int *count)
{
    const TransferIo real;
    TransferIo io;
    // Every call goes through `around`: counted, then ended at, or straight after, call killAt.
    const auto around = [=](auto call) {
        const int mine = *count + 1;
        if (mine == killAt && !after)
            ::_exit(9);
        ++*count;
        const auto result = call();
        if (mine == killAt && after)
            ::_exit(9);
        return result;
    };
    io.renameNoReplace = [=](const char *from, const char *to) {
        return around([&] {
            if (folderOf(from) != folderOf(to)) {
                errno = EXDEV;
                return -1;
            }
            return real.renameNoReplace(from, to);
        });
    };
    io.link = [=](const char *from, const char *to) {
        return around([&] { return real.link(from, to); });
    };
    io.rename = [=](const char *from, const char *to) {
        return around([&] { return real.rename(from, to); });
    };
    io.read
        = [=](int fd, void *b, size_t n) { return around([&] { return real.read(fd, b, n); }); };
    io.write = [=](int fd, const void *b, size_t n) {
        return around([&] { return real.write(fd, b, n); });
    };
    io.fchmod = [=](int fd, mode_t m) { return around([&] { return real.fchmod(fd, m); }); };
    io.futimens = [=](int fd, const struct timespec t[2]) {
        return around([&] { return real.futimens(fd, t); });
    };
    io.fsync = [=](int fd) { return around([&] { return real.fsync(fd); }); };
    io.close = [=](int fd) { return around([&] { return real.close(fd); }); };
    io.unlink = [=](const char *p) { return around([&] { return real.unlink(p); }); };
    io.clone = [=](int to, int from) {
        return around([&] {
            if (!clone) {
                errno = EOPNOTSUPP;
                return -1;
            }
            return real.clone(to, from);
        });
    };
    return io;
}

}

class OrganiseKillAcrossTest : public QObject {
    Q_OBJECT
private slots:
    void everyStep_data();
    void everyStep();
};

void OrganiseKillAcrossTest::everyStep_data()
{
    QTest::addColumn<bool>("after");
    QTest::addColumn<bool>("clone");
    QTest::newRow("killed at a call, bytes copied") << false << false;
    QTest::newRow("killed after a call, bytes copied") << true << false;
    QTest::newRow("killed at a call, the disk asked to share") << false << true;
    QTest::newRow("killed after a call, the disk asked to share") << true << true;
}

void OrganiseKillAcrossTest::everyStep()
{
    QFETCH(bool, after);
    QFETCH(bool, clone);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray whole(3 * 1024 * 1024 + 17, '\0');
    for (char &c : whole)
        c = char(QRandomGenerator::global()->bounded(256));
    int ended = 0;
    int steps = 0;
    for (int killAt = 1; killAt <= 2000 && ended == 0; ++killAt) {
        const QString round = dir.filePath(QStringLiteral("k%1").arg(killAt));
        const QString source = round + QStringLiteral("/from/a.mp3");
        const QString target = round + QStringLiteral("/to/a.mp3");
        QVERIFY(QDir().mkpath(round + QStringLiteral("/from")));
        QVERIFY(QDir().mkpath(round + QStringLiteral("/to")));
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(whole), qint64(whole.size()));
        }
        std::fflush(nullptr);
        const pid_t child = ::fork();
        QVERIFY(child >= 0);
        if (child == 0) {
            int count = 0;
            const QString said = linuxbee::organise::transferFileWith(
                killing(killAt, after, clone, &count), source, target, true);
            ::_exit(said.isEmpty() ? 0 : 3);
        }
        int status = 0;
        QCOMPARE(::waitpid(child, &status, 0), child);
        QVERIFY(WIFEXITED(status));
        const int code = WEXITSTATUS(status);
        QVERIFY2(code == 9 || code == 0,
            qPrintable(QStringLiteral("step %1: the move failed (%2)").arg(killAt).arg(code)));
        if (code == 0)
            ended = killAt;
        else
            ++steps;
        // The ruling's question: is the file whole at the source or at the target?
        const bool sourceWhole = bytesOf(source) == whole;
        const bool targetWhole = bytesOf(target) == whole;
        const QStringList spares
            = QDir(round + QStringLiteral("/to"))
                  .entryList({ QStringLiteral(".lbmove-*") }, QDir::Files | QDir::Hidden);
        QVERIFY2(sourceWhole || targetWhole,
            qPrintable(
                QStringLiteral("step %1 (%2): neither the source nor the target is whole; left: %3")
                    .arg(killAt)
                    .arg(after ? QStringLiteral("after") : QStringLiteral("at"))
                    .arg(spares.join(QLatin1Char(' ')))));
        if (code == 0) {
            QVERIFY(targetWhole && !QFileInfo::exists(source));
            QCOMPARE(spares, QStringList());
        }
        QDir(round).removeRecursively();
    }
    QVERIFY2(ended > 0, "the move never ended by itself");
    QVERIFY2(steps >= 8, qPrintable(QStringLiteral("only %1 steps were tried").arg(steps)));
}

QTEST_APPLESS_MAIN(OrganiseKillAcrossTest)
#include "tst_organise_kill_across.moc"
