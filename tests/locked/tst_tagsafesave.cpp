// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-safe-save.md (WS-x). Works on
// copies of the plan-03 fixtures in a temporary folder. One test starts a
// second process (fork) and lets it die in the middle of a save.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
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
    if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from), target))
        QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup);
    return target;
}

// Everything in the folder, hidden files too.
QStringList listing(const QString &folder)
{
    return QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
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
    return r.status == ReadResult::Status::Ok ? r.tags.first(QStringLiteral("Title")) : QStringLiteral("<unreadable>");
}

}

class SafeSaveTest : public QObject {
    Q_OBJECT
private slots:
    void theOriginalWaits();
    void abandoned();
    void killedInTheMiddle();
    void leftoversOfOthersStay();
    void keepsWhatTheFileHad();
    void modificationTime();
    void throughALink();
    void notWritable();
    void nothingToWrite();
    void oddNames();
    void everyFormat_data();
    void everyFormat();
};

void SafeSaveTest::theOriginalWaits()
{
    // WS-ORDER, WS-NAME, WS-HOOK: at the moment before the swap both versions exist, whole.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"album/song.flac"));
    const QString folder = dir.path() + s(u"/album");
    const QByteArray before = hashOf(path);
    int calls = 0;
    QString seen;
    WriteOptions options;
    options.beforeReplace = [&](const QString &newVersion) {
        ++calls;
        seen = newVersion;
        [&] {
            QCOMPARE(hashOf(path), before); // the original has not been touched
            QCOMPARE(titleOf(path), s(u"Flac Title"));
            QCOMPARE(QFileInfo(newVersion).absolutePath(), folder); // beside it: the same file system
            QVERIFY2(QRegularExpression(s(u"^\\.lbsave-[a-z0-9]{6}-song\\.flac$")).match(QFileInfo(newVersion).fileName()).hasMatch(),
                qPrintable(newVersion));
            QCOMPARE(titleOf(newVersion), s(u"New Title")); // complete and readable
            QCOMPARE(listing(folder).size(), 2);
        }();
        return true;
    };
    const WriteResult r = writeTags(path, title(s(u"New Title")), options);
    QVERIFY(r.status == WriteResult::Status::Ok);
    QCOMPARE(calls, 1);
    QCOMPARE(titleOf(path), s(u"New Title"));
    QCOMPARE(listing(folder), QStringList({ s(u"song.flac") })); // nothing left beside it
    QVERIFY(!QFile::exists(seen));
    QVERIFY(hashOf(path) != before);
}

void SafeSaveTest::abandoned()
{
    // WS-HOOK: false abandons the save.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.mp3", s(u"song.mp3"));
    const QByteArray before = hashOf(path);
    const QDateTime modified = QFileInfo(path).lastModified();
    WriteOptions options;
    options.beforeReplace = [](const QString &) { return false; };
    const WriteResult r = writeTags(path, title(s(u"New Title")), options);
    QVERIFY(r.status == WriteResult::Status::Failed);
    QCOMPARE(hashOf(path), before);
    QCOMPARE(QFileInfo(path).lastModified(), modified);
    QCOMPARE(listing(dir.path()), QStringList({ s(u"song.mp3") }));
}

void SafeSaveTest::killedInTheMiddle()
{
    // WS-KILLED: a process that dies during a save leaves the original whole,
    // and the next save of that file clears up after it.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"song.flac"));
    const QByteArray before = hashOf(path);
    const pid_t child = fork();
    QVERIFY(child >= 0);
    if (child == 0) {
        WriteOptions options;
        options.beforeReplace = [](const QString &) -> bool { _exit(42); }; // dies at the worst moment
        writeTags(path, title(s(u"Never Arrives")), options);
        _exit(1); // the hook was not called
    }
    int status = 0;
    QCOMPARE(waitpid(child, &status, 0), child);
    QVERIFY(WIFEXITED(status));
    QCOMPARE(WEXITSTATUS(status), 42);
    QCOMPARE(hashOf(path), before);
    QCOMPARE(titleOf(path), s(u"Flac Title"));
    const QStringList left = listing(dir.path());
    QCOMPARE(left.size(), 2); // the file and what the dead process left
    QVERIFY(left.at(0).startsWith(s(u".lbsave-")));

    QVERIFY(writeTags(path, title(s(u"Second Try"))).status == WriteResult::Status::Ok);
    QCOMPARE(titleOf(path), s(u"Second Try"));
    QCOMPARE(listing(dir.path()), QStringList({ s(u"song.flac") }));
}

void SafeSaveTest::leftoversOfOthersStay()
{
    // WS-KILLED: only this file's leftovers are removed, and only what has exactly their shape.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"b.flac"));
    const QStringList others {
        s(u".lbsave-abc123-a-b.flac"), // belongs to "a-b.flac"
        s(u".lbsave-abc123-other.flac"),
        s(u".lbsave-abc12-b.flac"), // five characters: not ours
        s(u".lbsave-abc1234-b.flac"), // seven
        s(u"lbsave-abc123-b.flac"), // no dot
        s(u".lbsave-abc123-b.flac.bak"),
    };
    for (const QString &name : others) {
        QFile f(dir.path() + QLatin1Char('/') + name);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("keep me");
    }
    QFile mine(dir.path() + s(u"/.lbsave-zzz999-b.flac"));
    QVERIFY(mine.open(QIODevice::WriteOnly));
    mine.write("half a file");
    mine.close();

    QVERIFY(writeTags(path, title(s(u"New Title"))).status == WriteResult::Status::Ok);
    QStringList expected = others;
    expected.append(s(u"b.flac"));
    expected.sort();
    QStringList got = listing(dir.path());
    got.sort();
    QCOMPARE(got, expected);
}

void SafeSaveTest::keepsWhatTheFileHad()
{
    // WS-KEEP: permissions and extended attributes.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"song.flac"));
    const QByteArray name = QFile::encodeName(path);
    QCOMPARE(::chmod(name.constData(), 0640), 0);
    const bool attributes = ::setxattr(name.constData(), "user.linuxbee.test", "kept", 4, 0) == 0;
    QVERIFY(writeTags(path, title(s(u"New Title"))).status == WriteResult::Status::Ok);
    struct stat st { };
    QCOMPARE(::stat(name.constData(), &st), 0);
    QCOMPARE(int(st.st_mode & 07777), 0640);
    QCOMPARE(st.st_uid, ::getuid());
    if (attributes) {
        char value[16] { };
        QCOMPARE(int(::getxattr(name.constData(), "user.linuxbee.test", value, sizeof value)), 4);
        QCOMPARE(QByteArray(value, 4), QByteArray("kept"));
    } else {
        qWarning("this file system takes no extended attributes: that part was not checked");
    }
    QCOMPARE(::chmod(name.constData(), 0604), 0);
    QVERIFY(writeTags(path, title(s(u"Once More"))).status == WriteResult::Status::Ok);
    QCOMPARE(::stat(name.constData(), &st), 0);
    QCOMPARE(int(st.st_mode & 07777), 0604);
}

void SafeSaveTest::modificationTime()
{
    // W-MTIME still holds, though the file on disk is a new one.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.ogg", s(u"song.ogg"));
    const QDateTime old = QDateTime(QDate(2020, 5, 17), QTime(8, 30, 15), QTimeZone::UTC);
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(old, QFileDevice::FileModificationTime));
    }
    WriteOptions keep;
    keep.keepModificationTime = true;
    QVERIFY(writeTags(path, title(s(u"Kept")), keep).status == WriteResult::Status::Ok);
    QCOMPARE(QFileInfo(path).lastModified().toUTC(), old);
    QVERIFY(writeTags(path, title(s(u"Not Kept"))).status == WriteResult::Status::Ok);
    QVERIFY(QFileInfo(path).lastModified() > QDateTime::currentDateTimeUtc().addSecs(-120));
}

void SafeSaveTest::throughALink()
{
    // WS-LINK: a symbolic link stays a link; the file it points to is saved, in its own folder.
    QTemporaryDir dir;
    const QString real = fixture(dir, "basic.flac", s(u"store/song.flac"));
    QDir(dir.path()).mkpath(s(u"shelf"));
    const QString link = dir.path() + s(u"/shelf/linked.flac");
    QVERIFY(QFile::link(real, link));
    QString seen;
    WriteOptions options;
    options.beforeReplace = [&](const QString &newVersion) {
        seen = newVersion;
        return true;
    };
    QVERIFY(writeTags(link, title(s(u"Through The Link")), options).status == WriteResult::Status::Ok);
    QVERIFY(QFileInfo(link).isSymLink());
    QCOMPARE(QFileInfo(link).symLinkTarget(), real);
    QCOMPARE(titleOf(real), s(u"Through The Link"));
    QCOMPARE(QFileInfo(seen).absolutePath(), dir.path() + s(u"/store"));
    QVERIFY(QFileInfo(seen).fileName().endsWith(s(u"-song.flac")));
    QCOMPARE(listing(dir.path() + s(u"/shelf")), QStringList({ s(u"linked.flac") }));
    QCOMPARE(listing(dir.path() + s(u"/store")), QStringList({ s(u"song.flac") }));
}

void SafeSaveTest::notWritable()
{
    // WS-REFUSED
    if (::geteuid() == 0)
        QSKIP("permissions do not stop the superuser");
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"locked/song.flac"));
    const QString folder = dir.path() + s(u"/locked");
    const QByteArray before = hashOf(path);
    int calls = 0;
    WriteOptions options;
    options.beforeReplace = [&](const QString &) {
        ++calls;
        return true;
    };
    // The file itself may not be written: refused, though replacing it would be possible.
    QCOMPARE(::chmod(QFile::encodeName(path).constData(), 0444), 0);
    QVERIFY(writeTags(path, title(s(u"No")), options).status == WriteResult::Status::NotWritable);
    QCOMPARE(listing(folder), QStringList({ s(u"song.flac") }));
    // The folder takes no new file.
    QCOMPARE(::chmod(QFile::encodeName(path).constData(), 0644), 0);
    QCOMPARE(::chmod(QFile::encodeName(folder).constData(), 0555), 0);
    const WriteResult r = writeTags(path, title(s(u"No")), options);
    QCOMPARE(::chmod(QFile::encodeName(folder).constData(), 0755), 0);
    QVERIFY(r.status == WriteResult::Status::NotWritable);
    QCOMPARE(calls, 0);
    QCOMPARE(hashOf(path), before);
    QCOMPARE(listing(folder), QStringList({ s(u"song.flac") }));
}

void SafeSaveTest::nothingToWrite()
{
    // WS-HOOK: no copy is made and the hook is not called when nothing is written.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"song.flac"));
    QFile stale(dir.path() + s(u"/.lbsave-old000-song.flac"));
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.close();
    int calls = 0;
    WriteOptions options;
    options.beforeReplace = [&](const QString &) {
        ++calls;
        return true;
    };
    QVERIFY(writeTags(path, title(s(u"Flac Title")), options).status == WriteResult::Status::Unchanged);
    TagChanges bad;
    bad.fields.insert(s(u"No Such Field"), { s(u"x") });
    QVERIFY(writeTags(path, bad, options).status == WriteResult::Status::InvalidChange);
    QVERIFY(writeTags(dir.path() + s(u"/missing.flac"), title(s(u"x")), options).status == WriteResult::Status::NotFound);
    QCOMPARE(calls, 0);
    QCOMPARE(listing(dir.path()).size(), 2); // and nothing was cleared up either: no save began
}

void SafeSaveTest::oddNames()
{
    // WS-NAME: characters that mean something to a shell or a pattern are just characters.
    QTemporaryDir dir;
    const QStringList names { s(u"01 - a [live] (take *2*)?.flac"), s(u".hidden.flac"), s(u"Bj\u00f6rk \u2013 J\u00f3ga.flac"), s(u"-dash.flac") };
    for (const QString &name : names)
        fixture(dir, "basic.flac", name);
    QStringList all = names;
    all.sort();
    for (const QString &name : names) {
        const QString path = dir.path() + QLatin1Char('/') + name;
        QString seen;
        WriteOptions options;
        options.beforeReplace = [&](const QString &newVersion) {
            seen = QFileInfo(newVersion).fileName();
            return true;
        };
        QVERIFY2(writeTags(path, title(s(u"Odd")), options).status == WriteResult::Status::Ok, qPrintable(name));
        QCOMPARE(seen.mid(15), name); // ".lbsave-" + six + "-" is fifteen characters
        QCOMPARE(titleOf(path), s(u"Odd"));
        QStringList got = listing(dir.path());
        got.sort();
        QCOMPARE(got, all); // no other file was taken for a leftover
    }
}

void SafeSaveTest::everyFormat_data()
{
    QTest::addColumn<QString>("file");
    for (const char *name : { "basic.mp3", "v23.mp3", "v1only.mp3", "basic.flac", "basic.ogg", "basic.opus", "basic.m4a", "basic.wav", "basic.wv" })
        QTest::newRow(name) << QString::fromLatin1(name);
}

void SafeSaveTest::everyFormat()
{
    // The copy keeps the file's extension, which is how the tag library knows the format.
    QFETCH(QString, file);
    QTemporaryDir dir;
    const QString path = fixture(dir, qPrintable(file), file);
    const ReadResult before = readTags(path);
    QVERIFY(before.status == ReadResult::Status::Ok);
    QVERIFY(writeTags(path, title(s(u"Safely Saved"))).status == WriteResult::Status::Ok);
    const ReadResult after = readTags(path);
    QVERIFY(after.status == ReadResult::Status::Ok);
    QCOMPARE(after.tags.first(s(u"Title")), s(u"Safely Saved"));
    QCOMPARE(after.tags.audio.durationMs, before.tags.audio.durationMs);
    QCOMPARE(after.tags.pictures.size(), before.tags.pictures.size());
    QCOMPARE(listing(dir.path()), QStringList({ file }));
}

QTEST_GUILESS_MAIN(SafeSaveTest)
#include "tst_tagsafesave.moc"
