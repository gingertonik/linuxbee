// LOCKED ACCEPTANCE TEST (the organiser: a move to another file system). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/organise.md (O-ACROSS). The other file system is /dev/shm, a memory file
// system on every Linux. The files are made here; nothing of the user's is touched.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <sys/stat.h>
#include <unistd.h>

#include "organise/transfer.h"

using namespace linuxbee;

namespace {

const QDateTime kOld = QDateTime(QDate(2001, 2, 3), QTime(4, 5, 6), QTimeZone::UTC);

struct Stat {
    quint64 device = 0, inode = 0;
    qint64 size = -1;
    unsigned mode = 0;
};

Stat statOf(const QString &path)
{
    struct stat st { };
    Stat out;
    if (::stat(QFile::encodeName(path).constData(), &st) == 0) {
        out.device = st.st_dev;
        out.inode = st.st_ino;
        out.size = st.st_size;
        out.mode = st.st_mode & 07777;
    }
    return out;
}

QByteArray bytes(int n)
{
    QByteArray out(n, Qt::Uninitialized);
    quint32 x = 12345;
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

// A file of about three megabytes, with an old time and a mode that is not the default.
QString make(const QString &path, const QByteArray &content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content) != content.size())
        QTest::qFail("could not write a file", __FILE__, __LINE__);
    f.close();
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
    QFile again(path);
    if (!again.open(QIODevice::ReadWrite) || !again.setFileTime(kOld, QFileDevice::FileModificationTime))
        QTest::qFail("could not set the time", __FILE__, __LINE__);
    return path;
}

struct Two {
    QTemporaryDir here; // where tests keep their files
    QTemporaryDir there { QStringLiteral("/dev/shm/linuxbee-test-XXXXXX") };
    bool ok() const
    {
        return here.isValid() && there.isValid() && statOf(here.path()).device != statOf(there.path()).device;
    }
};

#define NEED_TWO(t) \
    do { \
        if (!(t).ok()) \
            QFAIL("this test needs /dev/shm to be writable and another file system than the temporary folder"); \
    } while (false)

}

class OrganiseAcrossTest : public QObject {
    Q_OBJECT
private slots:
    void aMoveAcrossKeepsEverything();
    void aMoveAcrossThatCannotBeDone();
    void aMoveWithinIsARename();
    void aCopyAcross();
};

void OrganiseAcrossTest::aMoveAcrossKeepsEverything()
{
    // O-ACROSS: the file arrives whole, with its time and its mode, and only then does the source go.
    Two t;
    NEED_TWO(t);
    const QByteArray content = bytes(3 * 1024 * 1024 + 17);
    const QString source = make(t.here.path() + QStringLiteral("/song.flac"), content);
    const QString target = t.there.path() + QStringLiteral("/song.flac");
    QCOMPARE(organise::transferFile(source, target, true), QString());
    QVERIFY(!QFileInfo::exists(source));
    QCOMPARE(statOf(target).size, qint64(content.size()));
    QVERIFY(read(target) == content);
    QCOMPARE(QFileInfo(target).lastModified().toUTC(), kOld); // the scan must not take it for a changed file
    QCOMPARE(statOf(target).mode, 0640u);
    // Nothing else was left in either folder.
    QCOMPARE(QDir(t.there.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList({ QStringLiteral("song.flac") }));
    QCOMPARE(QDir(t.here.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList());
    // And back again, the other way across.
    QCOMPARE(organise::transferFile(target, source, true), QString());
    QVERIFY(read(source) == content);
    QCOMPARE(QFileInfo(source).lastModified().toUTC(), kOld);
    QVERIFY(!QFileInfo::exists(target));
    // A source that cannot be written to (a read-only file) moves all the same and stays read-only.
    QFile::setPermissions(source, QFile::ReadOwner);
    QCOMPARE(organise::transferFile(source, target, true), QString());
    QCOMPARE(statOf(target).mode, 0400u);
    QCOMPARE(QFileInfo(target).lastModified().toUTC(), kOld);
    QVERIFY(!QFileInfo::exists(source));
}

void OrganiseAcrossTest::aMoveAcrossThatCannotBeDone()
{
    // O-ACROSS: on any failure the source is as it was and no part of a target is left.
    if (::geteuid() == 0)
        QSKIP("run as root, a folder cannot be made unwritable");
    Two t;
    NEED_TWO(t);
    const QByteArray content = bytes(200000);
    const QString source = make(t.here.path() + QStringLiteral("/song.flac"), content);
    // The target is there already.
    const QString taken = make(t.there.path() + QStringLiteral("/taken.flac"), QByteArray("other"));
    QVERIFY(!organise::transferFile(source, taken, true).isEmpty());
    QVERIFY(read(source) == content);
    QVERIFY(read(taken) == QByteArray("other"));
    // The target's folder cannot be written.
    const QString closed = t.there.path() + QStringLiteral("/closed");
    QVERIFY(QDir().mkpath(closed));
    QVERIFY(QFile::setPermissions(closed, QFile::ReadOwner | QFile::ExeOwner));
    QVERIFY(!organise::transferFile(source, closed + QStringLiteral("/song.flac"), true).isEmpty());
    QVERIFY(read(source) == content);
    QCOMPARE(QFileInfo(source).lastModified().toUTC(), kOld);
    QCOMPARE(QDir(closed).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList());
    QVERIFY(QFile::setPermissions(closed, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    // The source's folder cannot be written, so the source cannot be removed: the copy is taken back.
    QVERIFY(QFile::setPermissions(t.here.path(), QFile::ReadOwner | QFile::ExeOwner));
    const QString target = t.there.path() + QStringLiteral("/song.flac");
    const QString said = organise::transferFile(source, target, true);
    QVERIFY(QFile::setPermissions(t.here.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QVERIFY(!said.isEmpty());
    QVERIFY(read(source) == content);
    QVERIFY(!QFileInfo::exists(target));
    // A source that is not there.
    QVERIFY(!organise::transferFile(t.here.path() + QStringLiteral("/none.flac"), target, true).isEmpty());
    QVERIFY(!QFileInfo::exists(target));
}

void OrganiseAcrossTest::aMoveWithinIsARename()
{
    // O-ACROSS: on one file system the file is renamed: it is the same file afterwards, not a copy.
    (void)&bytes;
    QTemporaryDir here;
    const QString source = make(here.path() + QStringLiteral("/a.flac"), QByteArray("abc"));
    const Stat before = statOf(source);
    QVERIFY(QDir().mkpath(here.path() + QStringLiteral("/sub")));
    const QString target = here.path() + QStringLiteral("/sub/b.flac");
    QCOMPARE(organise::transferFile(source, target, true), QString());
    QCOMPARE(statOf(target).inode, before.inode);
    QCOMPARE(QFileInfo(target).lastModified().toUTC(), kOld);
    QVERIFY(!QFileInfo::exists(source));
    // And it never replaces: a rename onto a file that is there is refused.
    const QString other = make(here.path() + QStringLiteral("/c.flac"), QByteArray("c"));
    QVERIFY(!organise::transferFile(other, target, true).isEmpty());
    QVERIFY(read(target) == QByteArray("abc"));
    QVERIFY(read(other) == QByteArray("c"));
}

void OrganiseAcrossTest::aCopyAcross()
{
    // O-EXECUTE, unchanged: a copy leaves the source and keeps its time.
    Two t;
    NEED_TWO(t);
    const QByteArray content = bytes(100000);
    const QString source = make(t.here.path() + QStringLiteral("/song.flac"), content);
    const QString target = t.there.path() + QStringLiteral("/song.flac");
    QCOMPARE(organise::transferFile(source, target, false), QString());
    QVERIFY(read(source) == content);
    QVERIFY(read(target) == content);
    QCOMPARE(QFileInfo(target).lastModified().toUTC(), kOld);
}

QTEST_MAIN(OrganiseAcrossTest)
#include "tst_organise_across.moc"
