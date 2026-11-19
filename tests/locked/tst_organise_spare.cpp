// LOCKED ACCEPTANCE TEST (the organiser: a copy is made under a spare name, and Copy is as careful
// as a move to another disk). Written by the reviewer, not the builder. The builder makes these
// pass by changing src/, never by editing this file. A test believed to be wrong is reported, with
// the reason, and left alone.
//
// Expected values are O-SPARE and O-ACROSS in docs/spec/organise.md. The faults this holds shut
// (the write-path audit, part 2): F4, a run killed while it copied left a cut-off file under the
// target's real name; F3, a Copy that failed removed whatever was at the target, its own or not.
//
// A test cannot kill itself half way. It looks instead, from inside a call the copy makes, at what
// a kill at that moment would leave. The rig is the one of tst_organise_across2.cpp.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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
        if (!again.open(QIODevice::ReadWrite)
            || !again.setFileTime(kOld, QFileDevice::FileModificationTime))
            QTest::qFail("could not set the time", __FILE__, __LINE__);
    }
    // The real calls, each written down; and the rename of the source answering "another file
    // system", so that the copy is made.
    TransferIo across()
    {
        TransferIo real;
        TransferIo io;
        // O-SPARE, the bytes are shared where the disk can: this rig's disk cannot.
        io.clone = [](int, int) {
            errno = EOPNOTSUPP;
            return -1;
        };
        io.renameNoReplace = [this, real](const char *a, const char *b) {
            log.append(QStringLiteral("renameNoReplace"));
            if (!QFile::decodeName(a).startsWith(dir.path() + QStringLiteral("/to/"))) {
                errno = EXDEV; // whatever is moved from outside the target's folder
                return -1;
            }
            return real.renameNoReplace(a, b); // O-SPARE: the copy is given its name
        };
        io.link = [this, real](const char *a, const char *b) {
            log.append(QStringLiteral("link"));
            return real.link(a, b);
        };
        io.rename = [this, real](const char *a, const char *b) {
            log.append(QStringLiteral("rename"));
            return real.rename(a, b);
        };
        io.read = [this, real](int fd, void *b, size_t n) {
            log.append(QStringLiteral("read"));
            return real.read(fd, b, n);
        };
        io.write = [this, real](int fd, const void *b, size_t n) {
            log.append(QStringLiteral("write"));
            return real.write(fd, b, n);
        };
        io.fchmod = [this, real](int fd, mode_t m) {
            log.append(QStringLiteral("fchmod"));
            return real.fchmod(fd, m);
        };
        io.futimens = [this, real](int fd, const struct timespec t[2]) {
            log.append(QStringLiteral("futimens"));
            return real.futimens(fd, t);
        };
        io.fsync = [this, real](int fd) {
            log.append(QStringLiteral("fsync"));
            return real.fsync(fd);
        };
        io.close = [this, real](int fd) {
            log.append(QStringLiteral("close"));
            return real.close(fd);
        };
        io.unlink = [this, real](const char *p) {
            log.append(QStringLiteral("unlink ") + QFileInfo(QFile::decodeName(p)).fileName()
                + (QFile::decodeName(p) == source ? QStringLiteral(" (source)")
                                                  : QStringLiteral(" (target)")));
            return real.unlink(p);
        };
        return io;
    }
    bool sourceIntact() const
    {
        return read(source) == content && QFileInfo(source).lastModified().toUTC() == kOld;
    }
    QStringList inTo() const
    {
        return QDir(dir.path() + QStringLiteral("/to"))
            .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    }
};

const QRegularExpression kSpare(QStringLiteral("^\\.lbmove-[a-z0-9]{6}$"));

unsigned modeOf(const QString &path)
{
    struct stat st { };
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? unsigned(st.st_mode & 07777)
                                                                  : 0u;
}

}

class OrganiseSpareTest : public QObject {
    Q_OBJECT
private slots:
    void whileItIsCopied_data();
    void whileItIsCopied();
    void theNameIsGivenToAWholeFile_data();
    void theNameIsGivenToAWholeFile();
    void aFileThatAppearsWhileItIsCopied_data();
    void aFileThatAppearsWhileItIsCopied();
    void nothingIsCopiedWhenSomethingIsThere();
    void aCopyKeepsEverything();
    void aCopyThatFails_data();
    void aCopyThatFails();
    void namedByASecondName();
    void namedByAPlainRename();
    void aLeftoverIsNotInTheWay();
};

static void moveAndCopy()
{
    QTest::addColumn<bool>("move");
    QTest::newRow("move") << true;
    QTest::newRow("copy") << false;
}

void OrganiseSpareTest::whileItIsCopied_data()
{
    moveAndCopy();
}

void OrganiseSpareTest::whileItIsCopied()
{
    // O-SPARE: while the bytes go, nothing is at the target's name. What a kill would leave is one
    // hidden file with the spare name, which no scan takes for music.
    QFETCH(bool, move);
    Box box;
    TransferIo io = box.across();
    const TransferIo real;
    int writes = 0;
    QStringList seen;
    bool targetSeen = false;
    io.write = [&](int fd, const void *b, size_t n) -> ssize_t {
        if (++writes == 2) {
            seen = box.inTo();
            targetSeen = QFileInfo::exists(box.target);
        }
        return real.write(fd, b, n);
    };
    QCOMPARE(organise::transferFileWith(io, box.source, box.target, move), QString());
    QVERIFY2(writes >= 2, "the bytes did not go through the calls given");
    QVERIFY2(!targetSeen, "half a file was at the target's name");
    QCOMPARE(int(seen.size()), 1);
    QVERIFY2(kSpare.match(seen.value(0)).hasMatch(), qPrintable(seen.value(0)));
    QCOMPARE(box.inTo(),
        QStringList({ QStringLiteral("song.flac") })); // and afterwards no spare name is left
    QVERIFY(read(box.target) == box.content);
}

void OrganiseSpareTest::theNameIsGivenToAWholeFile_data()
{
    moveAndCopy();
}

void OrganiseSpareTest::theNameIsGivenToAWholeFile()
{
    // O-SPARE: at the moment the copy gets its name it is whole: every byte, the time, the mode,
    // flushed and closed for writing. Nothing is done to it afterwards.
    QFETCH(bool, move);
    Box box;
    TransferIo io = box.across();
    const TransferIo real;
    const unsigned mode = modeOf(box.source);
    int flushes = 0, namings = 0;
    bool whole = false, targetThereBefore = true;
    io.fsync = [&](int fd) {
        ++flushes;
        box.log.append(QStringLiteral("fsync"));
        return real.fsync(fd);
    };
    io.renameNoReplace = [&](const char *a, const char *b) {
        const QString from = QFile::decodeName(a);
        if (from == box.source) {
            errno = EXDEV;
            return -1;
        }
        ++namings;
        box.log.append(QStringLiteral("named"));
        targetThereBefore = QFileInfo::exists(box.target);
        whole = QFile::decodeName(b) == box.target && read(from) == box.content
            && QFileInfo(from).lastModified().toUTC() == kOld && modeOf(from) == mode
            && flushes == 1;
        return real.renameNoReplace(a, b);
    };
    QCOMPARE(organise::transferFileWith(io, box.source, box.target, move), QString());
    QCOMPARE(namings, 1);
    QVERIFY(!targetThereBefore);
    QVERIFY2(whole, "the name was given to a file that was not yet whole");
    const qsizetype named = box.log.indexOf(QStringLiteral("named"));
    for (const char *call : { "write", "fchmod", "futimens" })
        QVERIFY2(box.log.lastIndexOf(QLatin1String(call)) < named, call);
    QCOMPARE(QFileInfo::exists(box.source), !move);
}

void OrganiseSpareTest::aFileThatAppearsWhileItIsCopied_data()
{
    QTest::addColumn<bool>("move");
    QTest::addColumn<bool>("link");
    QTest::newRow("move, a file") << true << false;
    QTest::newRow("move, a link") << true << true;
    QTest::newRow("copy, a file") << false << false;
    QTest::newRow("copy, a link") << false << true;
}

void OrganiseSpareTest::aFileThatAppearsWhileItIsCopied()
{
    // O-SPARE, and F3 for a copy: somebody else puts a file (or a link) at the target while the
    // bytes go. It is theirs: not written to, not followed, not removed. The transfer fails and
    // takes back its own copy.
    QFETCH(bool, move);
    QFETCH(bool, link);
    Box box;
    const QString victim = box.dir.path() + QStringLiteral("/victim.txt");
    {
        QFile v(victim);
        QVERIFY(v.open(QIODevice::WriteOnly));
        v.write("victim");
    }
    TransferIo io = box.across();
    const TransferIo real;
    int writes = 0;
    io.write = [&](int fd, const void *b, size_t n) -> ssize_t {
        if (++writes == 2) {
            if (link) {
                QFile::link(victim, box.target);
            } else {
                QFile planted(box.target);
                if (planted.open(QIODevice::WriteOnly))
                    planted.write("not ours");
            }
        }
        return real.write(fd, b, n);
    };
    QCOMPARE(organise::transferFileWith(io, box.source, box.target, move),
        QStringLiteral("the target already exists"));
    QVERIFY(box.sourceIntact());
    QCOMPARE(box.inTo(), QStringList({ QStringLiteral("song.flac") }));
    QCOMPARE(read(victim), QByteArray("victim"));
    if (link)
        QVERIFY(QFileInfo(box.target).isSymLink());
    else
        QCOMPARE(read(box.target), QByteArray("not ours"));
}

void OrganiseSpareTest::nothingIsCopiedWhenSomethingIsThere()
{
    // O-SPARE: one look before the bytes go, so that a whole file is not copied only to be refused
    // its name.
    Box box;
    TransferIo io = box.across();
    io.renameNoReplace = [&](const char *, const char *) {
        QFile planted(box.target);
        if (planted.open(QIODevice::WriteOnly))
            planted.write("not ours");
        errno = EXDEV;
        return -1;
    };
    QCOMPARE(organise::transferFileWith(io, box.source, box.target, true),
        QStringLiteral("the target already exists"));
    QVERIFY2(!box.log.contains(QStringLiteral("read")), "the file was copied first");
    QCOMPARE(read(box.target), QByteArray("not ours"));
    QVERIFY(box.sourceIntact());
}

void OrganiseSpareTest::aCopyKeepsEverything()
{
    // O-ACROSS for Copy: the bytes, the time and the mode; flushed, the file and its folder; the
    // source is not touched. No rename of the source is tried, and nothing of the source is
    // removed.
    Box box;
    const quint64 was = inode(box.source);
    QCOMPARE(organise::transferFileWith(box.across(), box.source, box.target, false), QString());
    QVERIFY(read(box.target) == box.content);
    QCOMPARE(QFileInfo(box.target).lastModified().toUTC(), kOld);
    QCOMPARE(modeOf(box.target), modeOf(box.source));
    QVERIFY(box.sourceIntact());
    QCOMPARE(inode(box.source), was);
    QVERIFY(inode(box.target) != was);
    QVERIFY2(!box.log.isEmpty(), "the calls given were not used");
    QCOMPARE(box.log.count(QStringLiteral("fsync")), 2);
    QCOMPARE(box.log.count(QStringLiteral("renameNoReplace")), 1); // the naming only
    QCOMPARE(box.log.count(QStringLiteral("unlink song.flac (source)")), 0);
    QCOMPARE(box.inTo(), QStringList({ QStringLiteral("song.flac") }));
}

void OrganiseSpareTest::aCopyThatFails_data()
{
    QTest::addColumn<QString>("step");
    for (const char *step : { "read fails", "write fails", "fchmod", "futimens",
             "fsync of the copy", "fsync of the folder", "close", "the naming" })
        QTest::newRow(step) << QString::fromLatin1(step);
}

void OrganiseSpareTest::aCopyThatFails()
{
    // O-ACROSS for Copy: whichever call fails, the source is as it was and nothing is left in the
    // target's folder, under any name.
    QFETCH(QString, step);
    Box box;
    TransferIo io = box.across();
    const TransferIo real;
    int reads = 0, writes = 0, flushes = 0;
    if (step == QLatin1String("read fails"))
        io.read = [&](int fd, void *b, size_t n) -> ssize_t {
            if (++reads == 2) {
                errno = EIO;
                return -1;
            }
            return real.read(fd, b, n);
        };
    else if (step == QLatin1String("write fails"))
        io.write = [&](int fd, const void *b, size_t n) -> ssize_t {
            if (++writes == 2) {
                errno = EIO;
                return -1;
            }
            return real.write(fd, b, n);
        };
    else if (step == QLatin1String("fchmod"))
        io.fchmod = [](int, mode_t) {
            errno = EPERM;
            return -1;
        };
    else if (step == QLatin1String("futimens"))
        io.futimens = [](int, const struct timespec[2]) {
            errno = EPERM;
            return -1;
        };
    else if (step == QLatin1String("fsync of the copy"))
        io.fsync = [&](int fd) {
            if (++flushes == 1) {
                errno = EIO;
                return -1;
            }
            return real.fsync(fd);
        };
    else if (step == QLatin1String("fsync of the folder"))
        io.fsync = [&](int fd) {
            if (++flushes == 2) {
                errno = EIO;
                return -1;
            }
            return real.fsync(fd);
        };
    else if (step == QLatin1String("close"))
        io.close = [&](int fd) {
            real.close(fd);
            errno = EIO;
            return -1;
        };
    else if (step == QLatin1String("the naming"))
        io.renameNoReplace = [](const char *, const char *) {
            errno = EACCES;
            return -1;
        };
    else
        QFAIL("no such step");
    const QString said = organise::transferFileWith(io, box.source, box.target, false);
    QVERIFY2(!said.isEmpty(), "the copy said it went well");
    QVERIFY2(box.sourceIntact(), "the source is not as it was");
    QVERIFY2(box.inTo().isEmpty(), qPrintable(box.inTo().join(QLatin1Char(' '))));
}

void OrganiseSpareTest::namedByASecondName()
{
    // O-SPARE: where the never-replacing rename is not known (EINVAL), the copy is given its name
    // as a second name (link, which never replaces), and the spare name is removed.
    for (const bool planted : { false, true }) {
        Box box;
        TransferIo io = box.across();
        const TransferIo real;
        io.renameNoReplace = [&](const char *a, const char *) {
            if (QFile::decodeName(a) == box.source) {
                errno = EXDEV;
                return -1;
            }
            if (planted) {
                QFile other(box.target);
                if (other.open(QIODevice::WriteOnly))
                    other.write("not ours");
            }
            errno = EINVAL;
            return -1;
        };
        const QString said = organise::transferFileWith(io, box.source, box.target, true);
        QCOMPARE(box.log.count(QStringLiteral("link")), 1);
        QCOMPARE(box.inTo(), QStringList({ QStringLiteral("song.flac") }));
        if (planted) {
            QVERIFY(!said.isEmpty());
            QCOMPARE(read(box.target), QByteArray("not ours"));
            QVERIFY(box.sourceIntact());
        } else {
            QCOMPARE(said, QString());
            QVERIFY(read(box.target) == box.content);
            QVERIFY(!QFileInfo::exists(box.source));
        }
    }
}

void OrganiseSpareTest::namedByAPlainRename()
{
    // O-SPARE: only where second names are not possible either is the plain rename used, after one
    // more look that nothing is at the target.
    for (const bool planted : { false, true }) {
        Box box;
        TransferIo io = box.across();
        io.renameNoReplace = [&](const char *a, const char *) {
            errno = QFile::decodeName(a) == box.source ? EXDEV : EINVAL;
            return -1;
        };
        io.link = [&](const char *, const char *) {
            box.log.append(QStringLiteral("link"));
            if (planted) {
                QFile other(box.target);
                if (other.open(QIODevice::WriteOnly))
                    other.write("not ours");
            }
            errno = EOPNOTSUPP;
            return -1;
        };
        const QString said = organise::transferFileWith(io, box.source, box.target, true);
        QCOMPARE(box.inTo(), QStringList({ QStringLiteral("song.flac") }));
        if (planted) {
            QVERIFY(!said.isEmpty());
            QCOMPARE(box.log.count(QStringLiteral("rename")), 0);
            QCOMPARE(read(box.target), QByteArray("not ours"));
            QVERIFY(box.sourceIntact());
        } else {
            QCOMPARE(said, QString());
            QCOMPARE(box.log.count(QStringLiteral("rename")), 1);
            QVERIFY(read(box.target) == box.content);
            QVERIFY(!QFileInfo::exists(box.source));
        }
    }
}

void OrganiseSpareTest::aLeftoverIsNotInTheWay()
{
    // O-SPARE: what a killed run left behind does not stop the next one.
    Box box;
    const QString leftover = box.dir.path() + QStringLiteral("/to/.lbmove-abc123");
    {
        QFile f(leftover);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("half a song");
    }
    QCOMPARE(organise::transferFileWith(box.across(), box.source, box.target, true), QString());
    QVERIFY(read(box.target) == box.content);
    QVERIFY(!QFileInfo::exists(box.source));
}

QTEST_MAIN(OrganiseSpareTest)
#include "tst_organise_spare.moc"
