// LOCKED ACCEPTANCE TEST (a file with other names, or one that changed while it was being saved, is
// not replaced). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason, and
// left alone.
//
// Expected values are rules in docs/spec/tag-safe-save.md (WS-LINK, WS-SAME) and
// docs/decisions/hard-linked-files.md. Found by the audit of the tag write path. Works on copies of
// the fixtures in a temporary folder. "Another program" is this test, acting at the hook that runs
// just before the swap, or holding the file's lock.
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "convert/tagcopy.h"
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

bool hardLink(const QString &from, const QString &to)
{
    return ::link(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
}

// The one line a refusal gives, or a text that matches nothing.
QString line(const WriteResult &r)
{
    return r.problems.size() == 1 ? r.problems.at(0)
                                  : QStringLiteral("<%1 lines>").arg(r.problems.size());
}

}

class SaveSharedTest : public QObject {
    Q_OBJECT
private slots:
    void aFileWithAnotherName_data();
    void aFileWithAnotherName();
    void throughASymbolicLink();
    void nothingToWriteIsNotRefused();
    void convertSaysWhy();
    void changedWhileItWasSaved_data();
    void changedWhileItWasSaved();
    void notChangedIsSaved();
    void anotherSaveUnderWay();
    void aSaveInsideASave();
};

void SaveSharedTest::aFileWithAnotherName_data()
{
    QTest::addColumn<QString>("name");
    for (const char *name : { "basic.flac", "basic.mp3", "basic.m4a", "basic.ogg" })
        QTest::newRow(name) << QString::fromLatin1(name);
}

void SaveSharedTest::aFileWithAnotherName()
{
    // WS-LINK: refused before anything is written, and the refusal says how many names and what to
    // do.
    QFETCH(QString, name);
    QTemporaryDir dir;
    const QString path = fixture(dir, qPrintable(name), s(u"library/") + name);
    const QString other = dir.path() + s(u"/seeding/") + name;
    QDir().mkpath(dir.path() + s(u"/seeding"));
    QVERIFY(hardLink(path, other));
    const QByteArray before = hashOf(path);
    const QString was = titleOf(path);
    bool hookCalled = false;
    WriteOptions options;
    options.beforeReplace = [&hookCalled](const QString &) {
        hookCalled = true;
        return true;
    };

    WriteResult r = writeTags(path, title(s(u"New")), options);
    QCOMPARE(r.status, WriteResult::Status::HardLinked);
    QVERIFY(!r.ok());
    QVERIFY2(!hookCalled, "a copy was made and brought to the swap before the refusal");
    QVERIFY2(line(r).contains(s(u"2 names")), qPrintable(line(r)));
    QVERIFY2(line(r).contains(s(u"hard link")), qPrintable(line(r)));
    QVERIFY2(line(r).contains(s(u"cp -p FILE FILE.new && mv FILE.new FILE")), qPrintable(line(r)));
    QCOMPARE(hashOf(path), before);
    QCOMPARE(hashOf(other), before);
    QCOMPARE(listing(dir.path() + s(u"/library")), QStringList { name });
    QCOMPARE(listing(dir.path() + s(u"/seeding")), QStringList { name });
    struct stat a { }, b { };
    QVERIFY(::stat(QFile::encodeName(path).constData(), &a) == 0
        && ::stat(QFile::encodeName(other).constData(), &b) == 0);
    QVERIFY2(a.st_ino == b.st_ino && a.st_nlink == 2, "the two names are no longer one file");

    // Whichever of its names is given.
    QCOMPARE(writeTags(other, title(s(u"New"))).status, WriteResult::Status::HardLinked);

    // A third name: the count is the file's.
    QVERIFY(hardLink(path, dir.path() + s(u"/third")));
    r = writeTags(path, title(s(u"New")));
    QCOMPARE(r.status, WriteResult::Status::HardLinked);
    QVERIFY2(line(r).contains(s(u"3 names")), qPrintable(line(r)));
    QVERIFY(QFile::remove(dir.path() + s(u"/third")));

    // What the line says to do makes it a file of its own, and then it is saved; the other name
    // keeps the old tags.
    QVERIFY(QFile::copy(path, path + s(u".new")));
    QVERIFY(::rename(QFile::encodeName(path + s(u".new")).constData(),
                QFile::encodeName(path).constData())
        == 0);
    r = writeTags(path, title(s(u"New")));
    QCOMPARE(r.status, WriteResult::Status::Ok);
    QVERIFY(r.problems.isEmpty());
    QCOMPARE(titleOf(path), s(u"New"));
    QCOMPARE(hashOf(other), before);
    QCOMPARE(titleOf(other), was);
}

void SaveSharedTest::throughASymbolicLink()
{
    // WS-LINK: the file the path leads to is the one that counts.
    QTemporaryDir dir;
    const QString real = fixture(dir, "basic.flac", s(u"real/song.flac"));
    QVERIFY(hardLink(real, dir.path() + s(u"/real/twin.flac")));
    const QString via = dir.path() + s(u"/via.flac");
    QVERIFY(QFile::link(real, via));
    const QByteArray before = hashOf(real);
    QCOMPARE(writeTags(via, title(s(u"New"))).status, WriteResult::Status::HardLinked);
    QCOMPARE(hashOf(real), before);
    QVERIFY(QFileInfo(via).isSymLink());
    // A symbolic link is not another name: a file with one name and a link to it is saved, as
    // before.
    QVERIFY(QFile::remove(dir.path() + s(u"/real/twin.flac")));
    QCOMPARE(writeTags(via, title(s(u"New"))).status, WriteResult::Status::Ok);
    QCOMPARE(titleOf(real), s(u"New"));
    QVERIFY(QFileInfo(via).isSymLink());
}

void SaveSharedTest::nothingToWriteIsNotRefused()
{
    // W-UNCHANGED comes first: a save that would write nothing is refused nothing.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"song.flac"));
    QVERIFY(hardLink(path, dir.path() + s(u"/twin.flac")));
    QCOMPARE(writeTags(path, title(titleOf(path))).status, WriteResult::Status::Unchanged);
    // And a change that is refused for itself is refused for that.
    TagChanges bad;
    bad.fields.insert(s(u"No Such Field"), { s(u"x") });
    QCOMPARE(writeTags(path, bad).status, WriteResult::Status::InvalidChange);
}

void SaveSharedTest::convertSaysWhy()
{
    // C-TAGS, "update tags": an output file that has another name is not rewritten, and the reason
    // is given.
    QTemporaryDir dir;
    const QString target = fixture(dir, "basic.mp3", s(u"out/song.mp3"));
    QVERIFY(hardLink(target, dir.path() + s(u"/out/twin.mp3")));
    const QByteArray before = hashOf(target);
    const ReadResult source = readTags(fixture(dir, "basic.flac", s(u"in/song.flac")));
    QCOMPARE(source.status, ReadResult::Status::Ok);
    QStringList warnings;
    const QString said = convert::copyTags(source.tags, target, true, WriteOptions { }, &warnings);
    QVERIFY2(said.startsWith(s(u"the tags could not be written: ")), qPrintable(said));
    QVERIFY2(said.contains(s(u"2 names")) && said.contains(s(u"hard link")), qPrintable(said));
    QCOMPARE(hashOf(target), before);
}

void SaveSharedTest::changedWhileItWasSaved_data()
{
    QTest::addColumn<QString>("how");
    for (const char *how : { "rewritten", "touched", "replaced", "grown", "gone" })
        QTest::newRow(how) << QString::fromLatin1(how);
}

void SaveSharedTest::changedWhileItWasSaved()
{
    // WS-SAME: another program changed the file after it was read and before the swap. Its change
    // is not thrown away: the save is refused, and the file is as that program left it.
    QFETCH(QString, how);
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"album/song.flac"));
    const QString folder = dir.path() + s(u"/album");
    const QString elsewhere = fixture(dir, "basic.flac", s(u"elsewhere/song.flac"));
    QCOMPARE(writeTags(elsewhere, title(s(u"Theirs"))).status, WriteResult::Status::Ok);
    const QByteArray encoded = QFile::encodeName(path);
    struct stat first { };
    QVERIFY(::stat(encoded.constData(), &first) == 0);

    QByteArray theirs;
    int calls = 0;
    WriteOptions options;
    options.beforeReplace = [&](const QString &) {
        ++calls;
        // The other program does not act in the very instant the file was made: a system that
        // stamps changes by a coarse clock gives two changes within one tick the same stamp.
        QTest::qSleep(30);
        if (how == s(u"rewritten")) {
            // Another tagger saved it in place: other bytes; whatever its size and time come to.
            QFile from(elsewhere), to(path);
            if (from.open(QIODevice::ReadOnly)
                && to.open(QIODevice::WriteOnly | QIODevice::Truncate))
                to.write(from.readAll());
        } else if (how == s(u"touched")) {
            // The same bytes and size; only the time moved, by a second.
            const struct timespec times[2]
                = { first.st_atim, { first.st_mtim.tv_sec + 1, first.st_mtim.tv_nsec } };
            ::utimensat(AT_FDCWD, encoded.constData(), times, 0);
        } else if (how == s(u"replaced")) {
            // Another safe-saver put a new file in its place, and gave it the old size's time back:
            // only the file itself is another.
            QFile::remove(path);
            QFile::copy(elsewhere, path);
            const struct timespec times[2] = { first.st_atim, first.st_mtim };
            ::utimensat(AT_FDCWD, encoded.constData(), times, 0);
        } else if (how == s(u"grown")) {
            QFile to(path);
            if (to.open(QIODevice::Append))
                to.write("more");
            to.close();
            const struct timespec times[2] = { first.st_atim, first.st_mtim };
            ::utimensat(AT_FDCWD, encoded.constData(), times, 0); // and the time put back
        } else {
            QFile::remove(path);
        }
        theirs = hashOf(path);
        return true;
    };
    const WriteResult r = writeTags(path, title(s(u"Ours")), options);
    QCOMPARE(calls, 1);
    QCOMPARE(r.status, WriteResult::Status::ChangedOnDisk);
    QVERIFY(!r.ok());
    QVERIFY2(
        line(r).contains(s(u"changed")) && line(r).contains(s(u"try again")), qPrintable(line(r)));
    QCOMPARE(hashOf(path), theirs); // as the other program left it
    if (how == s(u"gone")) {
        QVERIFY2(!QFile::exists(path), "a file the other program removed was put back");
        QCOMPARE(listing(folder), QStringList { });
    } else {
        QCOMPARE(listing(folder), QStringList { s(u"song.flac") });
        QVERIFY(titleOf(path) != s(u"Ours"));
        // Tried again, as the line says, it is saved on top of what the other program did.
        const WriteResult again = writeTags(path, title(s(u"Ours")));
        QCOMPARE(again.status, WriteResult::Status::Ok);
        QCOMPARE(titleOf(path), s(u"Ours"));
    }
}

void SaveSharedTest::notChangedIsSaved()
{
    // The hook looks and touches nothing (reading does not count as a change): saved.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.mp3", s(u"song.mp3"));
    WriteOptions options;
    QByteArray seen;
    options.beforeReplace = [&](const QString &) {
        seen = hashOf(path);
        (void)readTags(path);
        return true;
    };
    QCOMPARE(writeTags(path, title(s(u"Ours")), options).status, WriteResult::Status::Ok);
    QVERIFY(!seen.isEmpty());
    QCOMPARE(titleOf(path), s(u"Ours"));
    // Keeping the time (W-MTIME) is not mistaken for a change either, twice in a row.
    options.beforeReplace = nullptr;
    options.keepModificationTime = true;
    QCOMPARE(writeTags(path, title(s(u"Two")), options).status, WriteResult::Status::Ok);
    QCOMPARE(writeTags(path, title(s(u"Three")), options).status, WriteResult::Status::Ok);
    QCOMPARE(titleOf(path), s(u"Three"));
}

void SaveSharedTest::anotherSaveUnderWay()
{
    // WS-SAME: linuxBee against itself (the window and linuxbeectl). While one save has the file,
    // another is refused at once, having written nothing. The file's lock is flock(2), exclusive,
    // on the file itself.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"album/song.flac"));
    const QByteArray before = hashOf(path);
    const int held = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    QVERIFY(held >= 0);
    QVERIFY(::flock(held, LOCK_EX | LOCK_NB) == 0);
    bool hookCalled = false;
    WriteOptions options;
    options.beforeReplace = [&hookCalled](const QString &) {
        hookCalled = true;
        return true;
    };
    QElapsedTimer timer;
    timer.start();
    const WriteResult r = writeTags(path, title(s(u"Ours")), options);
    QCOMPARE(r.status, WriteResult::Status::ChangedOnDisk);
    QVERIFY2(line(r).contains(s(u"another save")) && line(r).contains(s(u"try again")),
        qPrintable(line(r)));
    QVERIFY2(
        !hookCalled, "a copy was made and brought to the swap while another save had the file");
    QVERIFY2(timer.elapsed() < 2000, "the refusal waited"); // a limit, not a measure
    QCOMPARE(hashOf(path), before);
    QCOMPARE(listing(dir.path() + s(u"/album")), QStringList { s(u"song.flac") });
    ::close(held);
    // Let go, and it is saved; and a save leaves no lock behind it.
    QCOMPARE(writeTags(path, title(s(u"Ours"))).status, WriteResult::Status::Ok);
    QCOMPARE(writeTags(path, title(s(u"Again"))).status, WriteResult::Status::Ok);
    const int after = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    QVERIFY(after >= 0);
    QVERIFY2(::flock(after, LOCK_EX | LOCK_NB) == 0, "a finished save still holds the file");
    ::close(after);
    // A failed one neither.
    options.beforeReplace = [](const QString &) { return false; };
    QCOMPARE(writeTags(path, title(s(u"No")), options).status, WriteResult::Status::Failed);
    const int afterFailed = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    QVERIFY2(::flock(afterFailed, LOCK_EX | LOCK_NB) == 0, "a failed save still holds the file");
    ::close(afterFailed);
}

void SaveSharedTest::aSaveInsideASave()
{
    // The same, with the first save real: a second save of the file, begun while the first is at
    // its hook, is refused, and the first is saved.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.ogg", s(u"song.ogg"));
    WriteResult inner;
    WriteOptions options;
    options.beforeReplace = [&](const QString &) {
        inner = writeTags(path, title(s(u"Second")));
        return true;
    };
    const WriteResult outer = writeTags(path, title(s(u"First")), options);
    QCOMPARE(inner.status, WriteResult::Status::ChangedOnDisk);
    QVERIFY2(line(inner).contains(s(u"another save")), qPrintable(line(inner)));
    QCOMPARE(outer.status, WriteResult::Status::Ok);
    QCOMPARE(titleOf(path), s(u"First"));
    QCOMPARE(listing(dir.path()), QStringList { s(u"song.ogg") });
}

QTEST_GUILESS_MAIN(SaveSharedTest)
#include "tst_tagsafesave_shared.moc"
