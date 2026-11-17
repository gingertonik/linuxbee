// LOCKED ACCEPTANCE TEST (the first look at a file is taken before the file is read). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are the rules WS-SAME and WS-HOOK in docs/spec/tag-safe-save.md. Why this test: a
// save that takes its first look after it has read the file loses another program's edit made
// meanwhile (measured by the review: 39 of 60 on a large file), and no other test can tell, because
// they all act at the hook before the swap. "Another program" here is this test, acting at the hook
// between the first look and the read.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QByteArray hashOf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return "cannot open";
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex();
}

QString fixture(const QTemporaryDir &dir, const char *from, const QString &to)
{
    const QString target = dir.path() + QLatin1Char('/') + to;
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QFile::copy(
            QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from),
            target))
        QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    QFile::setPermissions(
        target, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup);
    return target;
}

QStringList listing(const QString &folder)
{
    return QDir(folder).entryList(
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

TagChanges title(const QString &text)
{
    TagChanges c;
    c.fields.insert(QStringLiteral("Title"), { text });
    return c;
}

QString titleOf(const QString &path)
{
    const ReadResult r = readTags(path);
    return r.status == ReadResult::Status::Ok ? r.tags.first(QStringLiteral("Title"))
                                              : QStringLiteral("<unreadable>");
}

}

class SaveFirstLookTest : public QObject {
    Q_OBJECT
private slots:
    void calledOnceEverySave();
    void changedAfterTheFirstLook_data();
    void changedAfterTheFirstLook();
    void theReadComesAfterIt();
};

void SaveFirstLookTest::calledOnceEverySave()
{
    // WS-HOOK: `afterFirstLook` is called once in every call, whatever comes of it, and before the
    // hook at the swap.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"song.flac"));
    QStringList order;
    WriteOptions options;
    options.afterFirstLook = [&order] { order.append(s(u"look")); };
    options.beforeReplace = [&order](const QString &) {
        order.append(s(u"swap"));
        return true;
    };
    QCOMPARE(writeTags(path, title(s(u"One")), options).status, WriteResult::Status::Ok);
    QCOMPARE(order, (QStringList { s(u"look"), s(u"swap") }));
    order.clear();
    QCOMPARE(writeTags(path, title(s(u"One")), options).status, WriteResult::Status::Unchanged);
    QCOMPARE(order, QStringList { s(u"look") });
    order.clear();
    QCOMPARE(writeTags(dir.path() + s(u"/none.flac"), title(s(u"x")), options).status,
        WriteResult::Status::NotFound);
    QCOMPARE(order, QStringList { s(u"look") });
    // Not set: a save as ever.
    QCOMPARE(writeTags(path, title(s(u"Two"))).status, WriteResult::Status::Ok);
    QCOMPARE(titleOf(path), s(u"Two"));
}

void SaveFirstLookTest::changedAfterTheFirstLook_data()
{
    QTest::addColumn<QString>("how");
    QTest::newRow("rewritten in place") << s(u"rewritten");
    QTest::newRow("only its time moved") << s(u"touched");
    QTest::newRow("replaced by another file") << s(u"replaced");
}

void SaveFirstLookTest::changedAfterTheFirstLook()
{
    // WS-SAME: what another program does to the file after the first look, though before the file
    // is read, is a change during the save: refused, their version kept, nothing left beside it.
    QFETCH(QString, how);
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"album/song.flac"));
    const QString elsewhere = fixture(dir, "love.flac", s(u"elsewhere/theirs.flac"));
    QCOMPARE(writeTags(elsewhere, title(s(u"Theirs"))).status, WriteResult::Status::Ok);
    struct stat first { };
    QCOMPARE(::stat(QFile::encodeName(path).constData(), &first), 0);
    int calls = 0;
    WriteOptions options;
    options.afterFirstLook = [&] {
        ++calls;
        QTest::qSleep(30); // a coarse clock gives two changes within one tick the same stamp
        if (how == s(u"rewritten")) {
            QFile from(elsewhere), to(path);
            if (from.open(QIODevice::ReadOnly)
                && to.open(QIODevice::WriteOnly | QIODevice::Truncate))
                to.write(from.readAll());
        } else if (how == s(u"touched")) {
            const struct timespec times[2]
                = { first.st_atim, { first.st_mtim.tv_sec + 1, first.st_mtim.tv_nsec } };
            ::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, 0);
        } else {
            QFile::rename(elsewhere, path + s(u".new"));
            ::rename(QFile::encodeName(path + s(u".new")).constData(),
                QFile::encodeName(path).constData());
        }
    };
    const WriteResult r = writeTags(path, title(s(u"Ours")), options);
    QCOMPARE(calls, 1);
    const QByteArray theirs
        = hashOf(path); // nothing of ours may be in it: taken now, compared below
    QCOMPARE(r.status, WriteResult::Status::ChangedOnDisk);
    QVERIFY(!r.ok());
    QVERIFY2(titleOf(path) != s(u"Ours"), "our save went over what the other program did");
    if (how != s(u"touched"))
        QCOMPARE(titleOf(path), s(u"Theirs"));
    QCOMPARE(listing(dir.path() + s(u"/album")), QStringList { s(u"song.flac") });
    // And then, with nobody acting, the same save goes through, on top of their version.
    QCOMPARE(writeTags(path, title(s(u"Ours"))).status, WriteResult::Status::Ok);
    QCOMPARE(titleOf(path), s(u"Ours"));
    QVERIFY(hashOf(path) != theirs);
}

void SaveFirstLookTest::theReadComesAfterIt()
{
    // WS-HOOK: the hook is between the first look and the read. So a file that goes away at the
    // hook is not found by the read. (With the read first, the save would have gone on with what it
    // had read.)
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.mp3", s(u"album/song.mp3"));
    WriteOptions options;
    options.afterFirstLook = [&path] { QFile::remove(path); };
    QCOMPARE(writeTags(path, title(s(u"Ours")), options).status, WriteResult::Status::NotFound);
    QVERIFY(listing(dir.path() + s(u"/album")).isEmpty());
}

QTEST_MAIN(SaveFirstLookTest)
#include "tst_tagsafesave_firstlook.moc"
