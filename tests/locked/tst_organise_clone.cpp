// LOCKED ACCEPTANCE TEST (the organiser: a copy shares the bytes where the disk can). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are O-SPARE ("The bytes are shared where the disk can") in
// docs/spec/organise.md. The fault this holds shut (the review of the careful copy, 2026-11-18):
// a Copy on one disk read and wrote every byte, where the copy before it had asked the disk to
// share them. A gigabyte took eighty times as long, and its room a second time.
//
// No test can time that honestly, so the work is counted: with a disk that shares, no byte is
// read and none written. The disk is played by the call the transfer makes (TransferIo::clone):
// one that shares (it copies the bytes itself, past the calls that are counted), one that says
// no, and one that says "done" and has not done it. All files are made here, in a temporary
// folder.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "organise/transfer.h"
#include "organise/transfer_io.h"

using namespace linuxbee;
using organise::TransferIo;

namespace {

const QDateTime kOld = QDateTime(QDate(2001, 2, 3), QTime(4, 5, 6), QTimeZone::UTC);

QByteArray bytes(int n)
{
    QByteArray out(n, Qt::Uninitialized);
    quint32 x = 4711;
    for (int i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        out[i] = char(x >> 24);
    }
    return out;
}

QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
}

quint64 numberOf(int fd)
{
    struct stat st { };
    return ::fstat(fd, &st) == 0 ? quint64(st.st_ino) : 0;
}

quint64 numberOf(const QString &path)
{
    struct stat st { };
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? quint64(st.st_ino) : 0;
}

// The name a descriptor's file has at this moment.
QString nameOf(int fd)
{
    return QFileInfo(QFile::symLinkTarget(QStringLiteral("/proc/self/fd/%1").arg(fd))).fileName();
}

// What a disk that shares does, done by hand and past the counted calls: the first `limit`
// bytes of `from` are in `to` afterwards (all of them when `limit` is below 0).
bool share(int to, int from, qint64 limit = -1)
{
    char buffer[65536];
    qint64 at = 0;
    for (;;) {
        qint64 want = sizeof buffer;
        if (limit >= 0)
            want = qMin(want, limit - at);
        if (want <= 0)
            return true;
        const ssize_t n = ::pread(from, buffer, size_t(want), at);
        if (n < 0)
            return false;
        if (n == 0)
            return true;
        if (::pwrite(to, buffer, size_t(n), at) != n)
            return false;
        at += n;
    }
}

struct Box {
    QTemporaryDir dir;
    QByteArray content = bytes(300 * 1024 + 17);
    QString source, target;
    int reads = 0;
    int writes = 0;
    int clones = 0;
    QStringList clonedInto; // the name the file had that was to get the bytes
    QList<quint64> clonedFrom; // the number of the file the bytes were to come from

    Box()
    {
        QDir(dir.path()).mkpath(QStringLiteral("from"));
        QDir(dir.path()).mkpath(QStringLiteral("to"));
        source = dir.path() + QStringLiteral("/from/song.flac");
        target = dir.path() + QStringLiteral("/to/song.flac");
        QFile f(source);
        if (!f.open(QIODevice::WriteOnly) || f.write(content) != content.size())
            QTest::qFail("could not write the source", __FILE__, __LINE__);
        f.close();
        QFile::setPermissions(source, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
        QFile again(source);
        if (!again.open(QIODevice::ReadWrite)
            || !again.setFileTime(kOld, QFileDevice::FileModificationTime))
            QTest::qFail("could not set the time", __FILE__, __LINE__);
    }
    // The real calls, with the reads and the writes counted; a move's first rename answers
    // "another file system", so that the copy is made; and the disk does with a clone what
    // `disk` says: 0 it shares, above 0 it says no with that errno, -1 it says "done" and has
    // done nothing, -2 it says "done" and has shared the first half.
    TransferIo io(int disk)
    {
        const TransferIo real;
        TransferIo calls;
        calls.renameNoReplace = [this, real](const char *a, const char *b) {
            if (!QFile::decodeName(a).startsWith(dir.path() + QStringLiteral("/to/"))) {
                errno = EXDEV;
                return -1;
            }
            return real.renameNoReplace(a, b);
        };
        calls.read = [this, real](int fd, void *b, size_t n) {
            ++reads;
            return real.read(fd, b, n);
        };
        calls.write = [this, real](int fd, const void *b, size_t n) {
            ++writes;
            return real.write(fd, b, n);
        };
        calls.clone = [this, disk](int to, int from) {
            ++clones;
            clonedInto.append(nameOf(to));
            clonedFrom.append(numberOf(from));
            if (disk > 0) {
                errno = disk;
                return -1;
            }
            if (disk == 0 && !share(to, from)) {
                errno = EIO;
                return -1;
            }
            if (disk == -2 && !share(to, from, content.size() / 2)) {
                errno = EIO;
                return -1;
            }
            return 0;
        };
        return calls;
    }
    QStringList inTheTargetFolder() const
    {
        return QDir(dir.path() + QStringLiteral("/to"))
            .entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
    }
    // The copy is whole, with the source's time and mode.
    bool wholeAtTheTarget() const
    {
        const QFileInfo info(target);
        return read(target) == content
            && info.fileTime(QFileDevice::FileModificationTime).toSecsSinceEpoch()
            == kOld.toSecsSinceEpoch()
            && info.permissions().testFlag(QFile::ReadGroup)
            && !info.permissions().testFlag(QFile::ReadOther);
    }
};

void moveAndCopy()
{
    QTest::addColumn<bool>("move");
    QTest::newRow("a move to another disk") << true;
    QTest::newRow("a copy") << false;
}

}

class OrganiseCloneTest : public QObject {
    Q_OBJECT
private slots:
    void theRealCallIsThere();
    void aDiskThatSharesReadsAndWritesNothing_data();
    void aDiskThatSharesReadsAndWritesNothing();
    void aDiskThatDoesNotShare_data();
    void aDiskThatDoesNotShare();
    void aDiskThatSaysDoneAndHasNot_data();
    void aDiskThatSaysDoneAndHasNot();
    void aLaterStepFailsAfterTheSharing();
    void aMoveOnOneDiskAsksNothing();
};

void OrganiseCloneTest::theRealCallIsThere()
{
    // O-IO: a default-made TransferIo has the call. What this disk answers is its own affair:
    // "done", and then the bytes are there; or no, and then nothing was written.
    Box box;
    const TransferIo real;
    QVERIFY2(static_cast<bool>(real.clone), "a default-made TransferIo has no clone");
    const int from = ::open(QFile::encodeName(box.source).constData(), O_RDONLY | O_CLOEXEC);
    const int to = ::open(
        QFile::encodeName(box.target).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    QVERIFY(from >= 0 && to >= 0);
    const int answer = real.clone(to, from);
    ::close(from);
    ::close(to);
    if (answer == 0)
        QCOMPARE(read(box.target), box.content);
    else
        QCOMPARE(QFileInfo(box.target).size(), qint64(0));
    QCOMPARE(read(box.source), box.content);
}

void OrganiseCloneTest::aDiskThatSharesReadsAndWritesNothing_data()
{
    moveAndCopy();
}

void OrganiseCloneTest::aDiskThatSharesReadsAndWritesNothing()
{
    // The disk is asked once, for the source's bytes into the spare file, before the file has
    // the target's name. It shares them: no byte is read, none is written, and the copy is
    // whole all the same, with the source's time and mode.
    QFETCH(bool, move);
    Box box;
    const quint64 was = numberOf(box.source);
    QCOMPARE(organise::transferFileWith(box.io(0), box.source, box.target, move), QString());
    QCOMPARE(box.clones, 1);
    QCOMPARE(box.clonedFrom, QList<quint64> { was });
    QVERIFY2(box.clonedInto.value(0).startsWith(QStringLiteral(".lbmove-")),
        qPrintable(box.clonedInto.value(0)));
    QCOMPARE(box.reads, 0);
    QCOMPARE(box.writes, 0);
    QVERIFY(box.wholeAtTheTarget());
    QCOMPARE(QFileInfo::exists(box.source), !move);
    if (!move)
        QCOMPARE(read(box.source), box.content);
    QCOMPARE(box.inTheTargetFolder(), QStringList({ QStringLiteral("song.flac") }));
}

void OrganiseCloneTest::aDiskThatDoesNotShare_data()
{
    QTest::addColumn<int>("error");
    QTest::addColumn<bool>("move");
    QTest::newRow("EOPNOTSUPP, a copy") << EOPNOTSUPP << false;
    QTest::newRow("EXDEV, a move to another disk") << EXDEV << true;
    QTest::newRow("EINVAL, a copy") << EINVAL << false;
    QTest::newRow("ENOTTY, a copy") << ENOTTY << false;
    QTest::newRow("ENOSYS, a copy") << ENOSYS << false;
    QTest::newRow("EPERM, a copy") << EPERM << false;
    QTest::newRow("EIO, a copy") << EIO << false;
}

void OrganiseCloneTest::aDiskThatDoesNotShare()
{
    // Whatever the disk says instead of "done", the bytes are read and written as before, and
    // the refusal is nothing to report.
    QFETCH(int, error);
    QFETCH(bool, move);
    Box box;
    QCOMPARE(organise::transferFileWith(box.io(error), box.source, box.target, move), QString());
    QCOMPARE(box.clones, 1);
    QVERIFY(box.reads > 0 && box.writes > 0);
    QVERIFY(box.wholeAtTheTarget());
    QCOMPARE(QFileInfo::exists(box.source), !move);
    QCOMPARE(box.inTheTargetFolder(), QStringList({ QStringLiteral("song.flac") }));
}

void OrganiseCloneTest::aDiskThatSaysDoneAndHasNot_data()
{
    QTest::addColumn<int>("disk");
    QTest::addColumn<bool>("move");
    QTest::newRow("nothing is there, a copy") << -1 << false;
    QTest::newRow("nothing is there, a move to another disk") << -1 << true;
    QTest::newRow("half is there, a copy") << -2 << false;
    QTest::newRow("half is there, a move to another disk") << -2 << true;
}

void OrganiseCloneTest::aDiskThatSaysDoneAndHasNot()
{
    // The disk's "done" is not taken at its word for the size. The transfer fails as any copy
    // of the wrong size does: the source is as it was, and nothing is left at the target.
    QFETCH(int, disk);
    QFETCH(bool, move);
    Box box;
    QCOMPARE(organise::transferFileWith(box.io(disk), box.source, box.target, move),
        QStringLiteral("the copy is not the size of the file"));
    QCOMPARE(box.clones, 1);
    QCOMPARE(read(box.source), box.content);
    QCOMPARE(box.inTheTargetFolder(), QStringList());
}

void OrganiseCloneTest::aLaterStepFailsAfterTheSharing()
{
    // The bytes were shared, and the flush of the copy fails. As after a copy by reading and
    // writing: the copy is taken back, the source stays.
    Box box;
    TransferIo io = box.io(0);
    io.fsync = [](int) {
        errno = EIO;
        return -1;
    };
    QCOMPARE(organise::transferFileWith(io, box.source, box.target, true),
        QStringLiteral("could not flush the copy to the disk"));
    QCOMPARE(box.clones, 1);
    QCOMPARE(read(box.source), box.content);
    QCOMPARE(box.inTheTargetFolder(), QStringList());
}

void OrganiseCloneTest::aMoveOnOneDiskAsksNothing()
{
    // A move within one file system is a rename, as before: the disk is not asked to share.
    Box box;
    TransferIo io = box.io(0);
    io.renameNoReplace = TransferIo().renameNoReplace;
    const quint64 was = numberOf(box.source);
    QCOMPARE(organise::transferFileWith(io, box.source, box.target, true), QString());
    QCOMPARE(box.clones, 0);
    QCOMPARE(box.reads, 0);
    QCOMPARE(numberOf(box.target), was);
}

QTEST_GUILESS_MAIN(OrganiseCloneTest)
#include "tst_organise_clone.moc"
