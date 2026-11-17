// LOCKED ACCEPTANCE TEST (the organiser: a move that goes wrong at any step loses no file). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/organise.md (O-ACROSS, O-IO). The system calls go through TransferIo, and the
// test makes them fail one at a time. All files are made here, in a temporary folder.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <cerrno>
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

quint64 inode(const QString &path)
{
    struct stat st { };
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? quint64(st.st_ino) : 0;
}

struct Box {
    QTemporaryDir dir;
    QByteArray content = bytes(3 * 1024 * 1024 + 17);
    QString source, target;
    QStringList log; // the calls, in order

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
        if (!again.open(QIODevice::ReadWrite) || !again.setFileTime(kOld, QFileDevice::FileModificationTime))
            QTest::qFail("could not set the time", __FILE__, __LINE__);
    }
    // The real calls, each written down; and the rename answering "another file system", so that the copy is made.
    TransferIo across()
    {
        TransferIo real;
        TransferIo io;
        io.renameNoReplace = [this](const char *, const char *) { log.append(QStringLiteral("renameNoReplace")); errno = EXDEV; return -1; };
        io.link = [this, real](const char *a, const char *b) { log.append(QStringLiteral("link")); return real.link(a, b); };
        io.rename = [this, real](const char *a, const char *b) { log.append(QStringLiteral("rename")); return real.rename(a, b); };
        io.read = [this, real](int fd, void *b, size_t n) { log.append(QStringLiteral("read")); return real.read(fd, b, n); };
        io.write = [this, real](int fd, const void *b, size_t n) { log.append(QStringLiteral("write")); return real.write(fd, b, n); };
        io.fchmod = [this, real](int fd, mode_t m) { log.append(QStringLiteral("fchmod")); return real.fchmod(fd, m); };
        io.futimens = [this, real](int fd, const struct timespec t[2]) { log.append(QStringLiteral("futimens")); return real.futimens(fd, t); };
        io.fsync = [this, real](int fd) { log.append(QStringLiteral("fsync")); return real.fsync(fd); };
        io.close = [this, real](int fd) { log.append(QStringLiteral("close")); return real.close(fd); };
        io.unlink = [this, real](const char *p) { log.append(QStringLiteral("unlink ") + QFileInfo(QFile::decodeName(p)).fileName() + (QFile::decodeName(p) == source ? QStringLiteral(" (source)") : QStringLiteral(" (target)"))); return real.unlink(p); };
        return io;
    }
    bool sourceIntact() const
    {
        return read(source) == content && QFileInfo(source).lastModified().toUTC() == kOld;
    }
    QStringList inTo() const { return QDir(dir.path() + QStringLiteral("/to")).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot); }
};

}

class OrganiseAcross2Test : public QObject {
    Q_OBJECT
private slots:
    void theOrder();
    void anyStepFailing_data();
    void anyStepFailing();
    void aFileThatAppearsMeanwhile();
    void aLinkIsNotCopied();
    void whereTheNoReplaceRenameIsNotKnown();
    void theRealCalls();
};

void OrganiseAcross2Test::theOrder()
{
    // O-ACROSS: bytes; time and mode; the flush of the file and of its folder; then, last, the source goes.
    Box box;
    QCOMPARE(organise::transferFileWith(box.across(), box.source, box.target, true), QString());
    QVERIFY(read(box.target) == box.content);
    QVERIFY(!QFileInfo::exists(box.source));
    QCOMPARE(QFileInfo(box.target).lastModified().toUTC(), kOld);
    const QStringList &log = box.log;
    QVERIFY2(!log.isEmpty(), "the calls given were not used");
    QCOMPARE(log.first(), QStringLiteral("renameNoReplace"));
    QCOMPARE(log.count(QStringLiteral("fsync")), 2); // the copy, and its folder
    QCOMPARE(log.count(QStringLiteral("fchmod")), 1);
    QCOMPARE(log.count(QStringLiteral("futimens")), 1);
    const qsizetype firstFlush = log.indexOf(QStringLiteral("fsync"));
    QVERIFY(log.lastIndexOf(QStringLiteral("write")) < firstFlush); // nothing is written after the flush
    QVERIFY2(log.indexOf(QStringLiteral("fchmod")) < firstFlush, qPrintable(log.join(QLatin1Char(' '))));
    QVERIFY2(log.indexOf(QStringLiteral("futimens")) < firstFlush, qPrintable(log.join(QLatin1Char(' '))));
    QCOMPARE(log.last(), QStringLiteral("unlink song.flac (source)")); // nothing comes after the source goes
    QCOMPARE(log.count(QStringLiteral("unlink song.flac (target)")), 0);
    QVERIFY(log.lastIndexOf(QStringLiteral("fsync")) < log.size() - 1);
    QVERIFY(!log.contains(QStringLiteral("link")) && !log.contains(QStringLiteral("rename")));
}

void OrganiseAcross2Test::anyStepFailing_data()
{
    QTest::addColumn<QString>("step");
    for (const char *step : { "read fails", "read ends early", "write fails", "write is short and then fails", "fchmod", "futimens",
             "fsync of the copy", "fsync of the folder", "close", "close of the copy alone", "unlink of the source" })
        QTest::newRow(step) << QString::fromLatin1(step);
}

void OrganiseAcross2Test::anyStepFailing()
{
    // O-ACROSS: whichever call fails, the source is as it was and nothing is left at the target.
    QFETCH(QString, step);
    Box box;
    TransferIo io = box.across();
    const TransferIo real;
    int reads = 0, writes = 0, flushes = 0, closes = 0, copyFd = -1;
    bool sourceWasThereAtEveryClose = true;
    if (step == QLatin1String("read fails"))
        io.read = [&](int fd, void *b, size_t n) -> ssize_t { if (++reads == 2) { errno = EIO; return -1; } return real.read(fd, b, n); };
    else if (step == QLatin1String("read ends early"))
        io.read = [&](int fd, void *b, size_t n) -> ssize_t { if (++reads >= 2) return 0; return real.read(fd, b, n < 4096 ? n : 4096); };
    else if (step == QLatin1String("write fails"))
        io.write = [&](int fd, const void *b, size_t n) -> ssize_t { if (++writes == 2) { errno = ENOSPC; return -1; } return real.write(fd, b, n); };
    else if (step == QLatin1String("write is short and then fails"))
        io.write = [&](int fd, const void *b, size_t n) -> ssize_t { if (++writes == 1) return real.write(fd, b, n < 100 ? n : 100); errno = ENOSPC; return -1; };
    else if (step == QLatin1String("fchmod"))
        io.fchmod = [](int, mode_t) { errno = EPERM; return -1; };
    else if (step == QLatin1String("futimens"))
        io.futimens = [](int, const struct timespec[2]) { errno = EPERM; return -1; };
    else if (step == QLatin1String("fsync of the copy"))
        io.fsync = [&](int fd) { if (++flushes == 1) { errno = EIO; return -1; } return real.fsync(fd); };
    else if (step == QLatin1String("fsync of the folder"))
        io.fsync = [&](int fd) { if (++flushes == 2) { errno = EIO; return -1; } return real.fsync(fd); };
    else if (step == QLatin1String("close"))
        // Every close really closes and then says it failed (a late write error, as on a network disk).
        io.close = [&](int fd) { ++closes; sourceWasThereAtEveryClose = sourceWasThereAtEveryClose && QFileInfo::exists(box.source); real.close(fd); errno = EIO; return -1; };
    else if (step == QLatin1String("close of the copy alone")) {
        // Only the copy's own close fails: it is the descriptor the time was set on.
        io.futimens = [&, real](int fd, const struct timespec t[2]) { copyFd = fd; return real.futimens(fd, t); };
        io.close = [&, real](int fd) { ++closes; if (fd != copyFd) return real.close(fd); real.close(fd); errno = EIO; return -1; };
    } else if (step == QLatin1String("unlink of the source"))
        io.unlink = [&](const char *p) { if (QFile::decodeName(p) == box.source) { errno = EACCES; return -1; } return real.unlink(p); };
    else
        QFAIL("no such step");

    const QString said = organise::transferFileWith(io, box.source, box.target, true);
    QVERIFY2(!said.isEmpty(), "the move said it went well");
    QVERIFY2(box.sourceIntact(), "the source is not as it was");
    QVERIFY2(box.inTo().isEmpty(), qPrintable(box.inTo().join(QLatin1Char(' '))));
    QVERIFY(sourceWasThereAtEveryClose);
    if (step.startsWith(QLatin1String("close")))
        QVERIFY(closes >= 1);
    if (step == QLatin1String("close of the copy alone"))
        QVERIFY2(copyFd >= 0, "the time was not set through the calls given");
}

void OrganiseAcross2Test::aFileThatAppearsMeanwhile()
{
    // O-ACROSS: a file that came to be at the target after the first look is somebody else's: it is not
    // written to and not removed, and the move fails.
    Box box;
    TransferIo io = box.across();
    io.renameNoReplace = [&](const char *, const char *) {
        QFile planted(box.target);
        if (planted.open(QIODevice::WriteOnly))
            planted.write("not ours");
        errno = EXDEV;
        return -1;
    };
    QVERIFY(!organise::transferFileWith(io, box.source, box.target, true).isEmpty());
    QCOMPARE(read(box.target), QByteArray("not ours"));
    QVERIFY(box.sourceIntact());
    // The same for a link planted there: it is not followed.
    Box two;
    const QString victim = two.dir.path() + QStringLiteral("/victim.txt");
    {
        QFile v(victim);
        QVERIFY(v.open(QIODevice::WriteOnly));
        v.write("victim");
    }
    TransferIo io2 = two.across();
    io2.renameNoReplace = [&](const char *, const char *) {
        QFile::link(victim, two.target);
        errno = EXDEV;
        return -1;
    };
    QVERIFY(!organise::transferFileWith(io2, two.source, two.target, true).isEmpty());
    QCOMPARE(read(victim), QByteArray("victim"));
    QVERIFY(QFileInfo(two.target).isSymLink());
    QVERIFY(two.sourceIntact());
}

void OrganiseAcross2Test::aLinkIsNotCopied()
{
    // O-ACROSS: a source that is a link is not moved to another file system: the link stays, nothing is copied.
    Box box;
    const QString link = box.dir.path() + QStringLiteral("/from/link.flac");
    QVERIFY(QFile::link(box.source, link));
    QVERIFY(!organise::transferFileWith(box.across(), link, box.target, true).isEmpty());
    QVERIFY(QFileInfo(link).isSymLink());
    QVERIFY(box.sourceIntact());
    QVERIFY(box.inTo().isEmpty());
    QVERIFY(!box.log.contains(QStringLiteral("read")));
}

void OrganiseAcross2Test::whereTheNoReplaceRenameIsNotKnown()
{
    // O-ACROSS: some file systems answer EINVAL to the rename that never replaces. Then: a second name
    // (link), which also never replaces, and the first name removed.
    const auto notKnown = [](Box &box) {
        TransferIo io = box.across();
        io.renameNoReplace = [&box](const char *, const char *) { box.log.append(QStringLiteral("renameNoReplace")); errno = EINVAL; return -1; };
        return io;
    };
    {
        Box box;
        const quint64 was = inode(box.source);
        QCOMPARE(organise::transferFileWith(notKnown(box), box.source, box.target, true), QString());
        QCOMPARE(inode(box.target), was); // the same file: nothing was copied
        QVERIFY(!QFileInfo::exists(box.source));
        QCOMPARE(box.log, QStringList({ QStringLiteral("renameNoReplace"), QStringLiteral("link"), QStringLiteral("unlink song.flac (source)") }));
    }
    {
        // A file that appears at the target meanwhile: the link refuses, nothing is replaced.
        Box box;
        TransferIo io = notKnown(box);
        io.renameNoReplace = [&box](const char *, const char *) {
            QFile planted(box.target);
            if (planted.open(QIODevice::WriteOnly))
                planted.write("not ours");
            errno = EINVAL;
            return -1;
        };
        QVERIFY(!organise::transferFileWith(io, box.source, box.target, true).isEmpty());
        QCOMPARE(read(box.target), QByteArray("not ours"));
        QVERIFY(box.sourceIntact());
    }
    {
        // The first name cannot be removed: the second is taken back, and the move fails.
        Box box;
        TransferIo io = notKnown(box);
        const TransferIo real;
        io.unlink = [&](const char *p) { if (QFile::decodeName(p) == box.source) { errno = EACCES; return -1; } return real.unlink(p); };
        QVERIFY(!organise::transferFileWith(io, box.source, box.target, true).isEmpty());
        QVERIFY(box.sourceIntact());
        QVERIFY(box.inTo().isEmpty());
    }
    {
        // No second names either (EPERM, EOPNOTSUPP, ENOSYS): the plain rename, after a look that nothing is there.
        Box box;
        TransferIo io = notKnown(box);
        io.link = [&box](const char *, const char *) { box.log.append(QStringLiteral("link")); errno = EPERM; return -1; };
        const quint64 was = inode(box.source);
        QCOMPARE(organise::transferFileWith(io, box.source, box.target, true), QString());
        QCOMPARE(inode(box.target), was);
        QCOMPARE(box.log, QStringList({ QStringLiteral("renameNoReplace"), QStringLiteral("link"), QStringLiteral("rename") }));
    }
    {
        // The second name says "another file system": the copy, as usual.
        Box box;
        TransferIo io = notKnown(box);
        io.link = [](const char *, const char *) { errno = EXDEV; return -1; };
        QCOMPARE(organise::transferFileWith(io, box.source, box.target, true), QString());
        QVERIFY(read(box.target) == box.content);
        QVERIFY(!QFileInfo::exists(box.source));
        QCOMPARE(box.log.count(QStringLiteral("fsync")), 2);
    }
    {
        // Any other answer of the rename is a failure at once: nothing else is tried.
        Box box;
        TransferIo io = box.across();
        io.renameNoReplace = [&box](const char *, const char *) { box.log.append(QStringLiteral("renameNoReplace")); errno = EACCES; return -1; };
        QVERIFY(!organise::transferFileWith(io, box.source, box.target, true).isEmpty());
        QCOMPARE(box.log, QStringList({ QStringLiteral("renameNoReplace") }));
        QVERIFY(box.sourceIntact());
    }
}

void OrganiseAcross2Test::theRealCalls()
{
    // O-IO: a default-made TransferIo is the real calls, and transferFile is transferFileWith with it.
    Box box;
    const quint64 was = inode(box.source);
    QCOMPARE(organise::transferFileWith(TransferIo(), box.source, box.target, true), QString());
    QCOMPARE(inode(box.target), was);
    QCOMPARE(organise::transferFile(box.target, box.source, true), QString());
    QCOMPARE(inode(box.source), was);
    QVERIFY(box.sourceIntact());
}

QTEST_MAIN(OrganiseAcross2Test)
#include "tst_organise_across2.moc"
