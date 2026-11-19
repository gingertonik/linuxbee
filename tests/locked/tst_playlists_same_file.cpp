// LOCKED ACCEPTANCE TEST (the playlist store: a playlist is written only over the file that was
// read). Written by the reviewer, not the builder. The builder makes these pass by changing src/,
// never by editing this file. A test believed to be wrong is reported, with the reason, and left
// alone.
//
// Expected values are Y-SAME in docs/spec/playlists.md. The fault this holds shut (the write-path
// audit, part 4, F3): a call read the file, worked out the new list and put it in the file's
// place, and asked nothing of the file in between; what another program wrote there meanwhile was
// replaced, and the call answered success.
//
// The other program is this test. It acts inside PlaylistStore::setBeforeReplace's hook, which the
// store calls when the new bytes are written beside the file and the file is not yet replaced, or
// inside the opener (setFileOpener), which the store calls after its first look and before its
// read. The files are real, in a temporary folder, and are read back here byte by byte.
#include <QSignalSpy>
#include <QtTest>

#include <memory>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "playlists/playliststore.h"

using linuxbee::PlaylistStore;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArrayLiteral("<cannot open>");
    return file.readAll();
}

bool put(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

// When the contents were last changed, as the system keeps it.
struct timespec contentsTime(const QString &path)
{
    struct stat st = { };
    (void)::stat(QFile::encodeName(path).constData(), &st);
    return st.st_mtim;
}

bool setContentsTime(const QString &path, const struct timespec &when)
{
    struct timespec times[2];
    times[0].tv_sec = 0;
    times[0].tv_nsec = UTIME_OMIT;
    times[1] = when;
    return ::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, 0) == 0;
}

// Every name in a folder, hidden ones too, sorted.
QStringList namesIn(const QString &folder)
{
    return QDir(folder).entryList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

// What there is in the folder, at this moment, of bytes that are not the playlist's own: every
// other name that holds some, and every file that this program holds open there and that holds
// some. (A spare file need not have a name while it is written: the system can keep a file that
// gets its name only when it is put in place, and the library's own way of saving does that.)
QStringList newBytesBeside(const QString &folder, const QString &file)
{
    QStringList found;
    const QString under = folder + QLatin1Char('/');
    const QDir::Filters all = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
    for (const QFileInfo &info : QDir(folder).entryInfoList(all, QDir::Name))
        if (info.absoluteFilePath() != file && info.isFile() && info.size() > 0)
            found.append(info.fileName());
    for (const QString &fd : QDir(QStringLiteral("/proc/self/fd")).entryList(all)) {
        const QByteArray link = QFile::encodeName(QStringLiteral("/proc/self/fd/") + fd);
        char target[4096];
        const ssize_t length = ::readlink(link.constData(), target, sizeof target - 1);
        struct stat is;
        if (length <= 0 || ::stat(link.constData(), &is) != 0 || !S_ISREG(is.st_mode)
            || is.st_size == 0)
            continue;
        const QString to = QFile::decodeName(QByteArray(target, int(length)));
        if (to.startsWith(under) && to != file && !found.contains(to.mid(under.size())))
            found.append(QStringLiteral("(open) ") + to.mid(under.size()));
    }
    return found;
}

const QByteArray kThree = QByteArrayLiteral("/m/a.mp3\n/m/b.mp3\n/m/c.mp3\n");
// The same size as kThree, other entries.
const QByteArray kOther = QByteArrayLiteral("/o/x.mp3\n/o/y.mp3\n/o/z.mp3\n");
const QString kChanged = QStringLiteral("the file was changed by another program");

// What the other program does to the file. Each answers whether it could.
enum Way { Grown, SameSizeTimePutBack, AnotherFileTimePutBack, TimeOnly, Removed, Ways };

const char *nameOf(int way)
{
    switch (way) {
    case Grown:
        return "an entry added in place";
    case SameSizeTimePutBack:
        return "rewritten in place, same size, the time put back";
    case AnotherFileTimePutBack:
        return "another file put in its place, same size, the time put back";
    case TimeOnly:
        return "the time alone moved";
    default:
        return "removed";
    }
}

bool otherProgram(int way, const QString &path)
{
    QThread::msleep(30); // a coarse clock gives two changes in one tick the same stamp
    const struct timespec was = contentsTime(path);
    switch (way) {
    case Grown: {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Append)
            && file.write("/o/other.mp3\n") == 13;
    }
    case SameSizeTimePutBack: {
        QFile file(path);
        if (!file.open(QIODevice::ReadWrite) || file.write(kOther) != kOther.size())
            return false;
        file.close();
        return setContentsTime(path, was);
    }
    case AnotherFileTimePutBack: {
        const QString beside = path + QStringLiteral(".theirs");
        if (!put(beside, kOther) || !setContentsTime(beside, was))
            return false;
        return ::rename(QFile::encodeName(beside).constData(), QFile::encodeName(path).constData())
            == 0;
    }
    case TimeOnly: {
        struct timespec later = was;
        later.tv_sec += 2;
        return setContentsTime(path, later);
    }
    default:
        return QFile::remove(path);
    }
}

// What the file holds after the other program, when it began as kThree.
QByteArray leftBy(int way)
{
    switch (way) {
    case Grown:
        return kThree + QByteArrayLiteral("/o/other.mp3\n");
    case SameSizeTimePutBack:
    case AnotherFileTimePutBack:
        return kOther;
    case TimeOnly:
        return kThree;
    default:
        return QByteArrayLiteral("<cannot open>");
    }
}

}

class PlaylistsSameFileTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void theRigIsWhatItSays();
    void withNoOtherWriterEachCallWrites_data();
    void withNoOtherWriterEachCallWrites();
    void changedBeforeTheReplacing_data();
    void changedBeforeTheReplacing();
    void changedAfterTheFirstLook_data();
    void changedAfterTheFirstLook();
    void aHookThatOnlyReads_data();
    void aHookThatOnlyReads();
    void twoCallsInARow();
    void afterARefusalTheNextCallWrites_data();
    void afterARefusalTheNextCallWrites();
    void nothingToDoAfterARefusalSaysNothing();
    void theHookIsCalledOnceWhenSomethingIsWritten_data();
    void theHookIsCalledOnceWhenSomethingIsWritten();
    void theHookIsNotCalledWhenNothingIsWritten();
    void theOlderRefusalsHaveTheirWords();
    void aFolderThatCannotBeWrittenTo();
    void relinkGoesOnPastTheOneThatChanged();

private:
    // The calls that write entries, by number: each on a list of a, b, c.
    bool call(int which, PlaylistStore &store) const;
    QByteArray after(int which) const;
    void five();
    QTemporaryDir m_dir;
    QString m_root;
    QString m_file;
    int m_counter = 0;
};

void PlaylistsSameFileTest::init()
{
    QVERIFY(m_dir.isValid());
    m_root = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_root));
    m_file = m_root + QStringLiteral("/List.m3u8");
    QVERIFY(put(m_file, kThree));
}

void PlaylistsSameFileTest::five()
{
    QTest::addColumn<int>("which");
    QTest::newRow("append") << 0;
    QTest::newRow("removeAt") << 1;
    QTest::newRow("moveTrack") << 2;
    QTest::newRow("setTracks") << 3;
    QTest::newRow("relink") << 4;
}

bool PlaylistsSameFileTest::call(int which, PlaylistStore &store) const
{
    switch (which) {
    case 0:
        return store.append(s(u"List"), { s(u"/m/new.mp3") }) == 1;
    case 1:
        return store.removeAt(s(u"List"), { 1 }) == 1;
    case 2:
        return store.moveTrack(s(u"List"), 0, 2);
    case 3:
        return store.setTracks(s(u"List"), { s(u"/m/x.mp3"), s(u"/m/y.mp3") });
    default:
        return store.relink({ { s(u"/m/b.mp3"), s(u"/n/b.mp3") } }) == 1;
    }
}

QByteArray PlaylistsSameFileTest::after(int which) const
{
    switch (which) {
    case 0:
        return kThree + QByteArrayLiteral("/m/new.mp3\n");
    case 1:
        return QByteArrayLiteral("/m/a.mp3\n/m/c.mp3\n");
    case 2:
        return QByteArrayLiteral("/m/b.mp3\n/m/c.mp3\n/m/a.mp3\n");
    case 3:
        return QByteArrayLiteral("/m/x.mp3\n/m/y.mp3\n");
    default:
        return QByteArrayLiteral("/m/a.mp3\n/n/b.mp3\n/m/c.mp3\n");
    }
}

// The other program's five ways do what their names say: otherwise a case below would pass or
// fail for a reason of the rig's own.
void PlaylistsSameFileTest::theRigIsWhatItSays()
{
    QCOMPARE(kOther.size(), kThree.size());
    for (int way = 0; way < Ways; ++way) {
        QVERIFY(put(m_file, kThree));
        struct stat before = { };
        QCOMPARE(::stat(QFile::encodeName(m_file).constData(), &before), 0);
        QVERIFY2(otherProgram(way, m_file), nameOf(way));
        QCOMPARE(bytesOf(m_file), leftBy(way));
        struct stat now = { };
        const bool there = ::stat(QFile::encodeName(m_file).constData(), &now) == 0;
        QCOMPARE(there, way != Removed);
        if (!there)
            continue;
        const bool sameTime = now.st_mtim.tv_sec == before.st_mtim.tv_sec
            && now.st_mtim.tv_nsec == before.st_mtim.tv_nsec;
        QVERIFY2(
            sameTime == (way == SameSizeTimePutBack || way == AnotherFileTimePutBack), nameOf(way));
        QVERIFY2((now.st_size == before.st_size) == (way != Grown), nameOf(way));
        QVERIFY2((now.st_ino == before.st_ino) == (way != AnotherFileTimePutBack), nameOf(way));
    }
}

// The feature lives: nobody else writes, a hook is set and does nothing, and each call writes.
void PlaylistsSameFileTest::withNoOtherWriterEachCallWrites_data()
{
    five();
}

void PlaylistsSameFileTest::withNoOtherWriterEachCallWrites()
{
    QFETCH(int, which);
    PlaylistStore store(m_root);
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QVERIFY(call(which, store));
    QCOMPARE(bytesOf(m_file), after(which));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(store.lastEditProblem(), QString());
    QCOMPARE(store.relinkPassedOver(), QStringList());
    QCOMPARE(namesIn(m_root), QStringList({ s(u"List.m3u8") }));
}

// Y-SAME: the file changes when the new bytes are written beside it and it is not yet replaced.
// Nothing is replaced; the file is as the other program left it; nothing is left beside it.
void PlaylistsSameFileTest::changedBeforeTheReplacing_data()
{
    QTest::addColumn<int>("which");
    QTest::addColumn<int>("way");
    const char *calls[] = { "append", "removeAt", "moveTrack", "setTracks", "relink" };
    for (int which = 0; which < 5; ++which) {
        for (int way = 0; way < Ways; ++way) {
            QTest::addRow("%s, %s", calls[which], nameOf(way)) << which << way;
        }
    }
}

void PlaylistsSameFileTest::changedBeforeTheReplacing()
{
    QFETCH(int, which);
    QFETCH(int, way);
    PlaylistStore store(m_root);
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    int called = 0;
    bool did = false;
    QStringList besideAtTheHook;
    store.setBeforeReplace([&](const QString &path) {
        ++called;
        besideAtTheHook = newBytesBeside(m_root, m_file);
        if (path == m_file)
            did = otherProgram(way, path);
    });
    const bool answer = call(which, store);
    QCOMPARE(called, 1);
    QVERIFY2(did, "the rig could not change the file");
    // The new bytes were there at the hook, under a name or in a file held open: the look that
    // refuses comes after them.
    QVERIFY2(!besideAtTheHook.isEmpty(), "nothing of the new bytes was in the folder at the hook");
    QVERIFY2(!answer, "the call answered that it wrote");
    QCOMPARE(bytesOf(m_file), leftBy(way));
    const QStringList left = way == Removed ? QStringList() : QStringList({ s(u"List.m3u8") });
    QCOMPARE(namesIn(m_root), left);
    QCOMPARE(changed.count(), 0);
    if (which == 4)
        QCOMPARE(store.relinkPassedOver(), QStringList({ s(u"List") }));
    else
        QCOMPARE(store.lastEditProblem(), kChanged);
}

// Y-SAME: the first look is taken before the file is opened for reading. The opener is called
// between the two, so a file changed inside the opener is a file changed after the first look,
// and the call is refused.
void PlaylistsSameFileTest::changedAfterTheFirstLook_data()
{
    five();
}

void PlaylistsSameFileTest::changedAfterTheFirstLook()
{
    QFETCH(int, which);
    PlaylistStore store(m_root);
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    int opened = 0;
    bool did = false;
    store.setFileOpener([&](const QString &path) -> std::unique_ptr<QIODevice> {
        ++opened;
        did = otherProgram(TimeOnly, path);
        auto file = std::make_unique<QFile>(path);
        if (!file->open(QIODevice::ReadOnly))
            return nullptr;
        return file;
    });
    const bool answer = call(which, store);
    QCOMPARE(opened, 1);
    QVERIFY2(did, "the rig could not change the file");
    QVERIFY2(!answer, "the call answered that it wrote");
    QCOMPARE(bytesOf(m_file), kThree);
    QCOMPARE(namesIn(m_root), QStringList({ s(u"List.m3u8") }));
    QCOMPARE(changed.count(), 0);
    if (which == 4)
        QCOMPARE(store.relinkPassedOver(), QStringList({ s(u"List") }));
    else
        QCOMPARE(store.lastEditProblem(), kChanged);
}

// No false alarm: a program that only reads the file changes nothing the looks compare.
void PlaylistsSameFileTest::aHookThatOnlyReads_data()
{
    five();
}

void PlaylistsSameFileTest::aHookThatOnlyReads()
{
    QFETCH(int, which);
    PlaylistStore store(m_root);
    QByteArray seen;
    store.setBeforeReplace([&](const QString &path) {
        QThread::msleep(30);
        seen = bytesOf(path);
    });
    QVERIFY(call(which, store));
    QCOMPARE(seen, kThree); // at the hook the file is still the one that was read
    QCOMPARE(bytesOf(m_file), after(which));
    QCOMPARE(store.lastEditProblem(), QString());
    QCOMPARE(store.relinkPassedOver(), QStringList());
}

// No false alarm: the store's own write just before is not "another program".
void PlaylistsSameFileTest::twoCallsInARow()
{
    PlaylistStore store(m_root);
    for (int round = 0; round < 20; ++round) {
        QCOMPARE(store.append(s(u"List"), { QStringLiteral("/m/n%1.mp3").arg(round) }), 1);
        QCOMPARE(store.lastEditProblem(), QString());
    }
    QCOMPARE(store.tracks(s(u"List")).size(), 23);
    QCOMPARE(store.removeAt(s(u"List"), { 0, 1, 2 }), 3);
    QVERIFY(store.moveTrack(s(u"List"), 0, 19));
    QCOMPARE(store.relink({ { s(u"/m/n5.mp3"), s(u"/n/n5.mp3") } }), 1);
    QCOMPARE(store.relinkPassedOver(), QStringList());
    QVERIFY(store.setTracks(s(u"List"), { s(u"/m/x.mp3") }));
    QCOMPARE(bytesOf(m_file), QByteArrayLiteral("/m/x.mp3\n"));
    QCOMPARE(store.lastEditProblem(), QString());
}

// A refusal is of that call alone: the same call made again, with nobody in the way, reads the
// file as the other program left it and writes, and nothing is said any more.
void PlaylistsSameFileTest::afterARefusalTheNextCallWrites_data()
{
    five();
}

void PlaylistsSameFileTest::afterARefusalTheNextCallWrites()
{
    QFETCH(int, which);
    PlaylistStore store(m_root);
    store.setBeforeReplace([&](const QString &path) { (void)otherProgram(TimeOnly, path); });
    QVERIFY(!call(which, store));
    QCOMPARE(bytesOf(m_file), kThree);
    store.setBeforeReplace({ });
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QVERIFY(call(which, store));
    QCOMPARE(bytesOf(m_file), after(which));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(store.lastEditProblem(), QString());
    QCOMPARE(store.relinkPassedOver(), QStringList());
}

// lastEditProblem is of the last call: one with nothing to do, made after a refused one, is not
// a refused one.
void PlaylistsSameFileTest::nothingToDoAfterARefusalSaysNothing()
{
    for (int which = 0; which < 4; ++which) {
        QVERIFY(put(m_file, kThree));
        PlaylistStore store(m_root);
        store.setBeforeReplace([&](const QString &path) { (void)otherProgram(TimeOnly, path); });
        QVERIFY(!store.append(s(u"List"), { s(u"/m/new.mp3") }));
        QCOMPARE(store.lastEditProblem(), kChanged);
        store.setBeforeReplace({ });
        switch (which) {
        case 0:
            QCOMPARE(store.append(s(u"List"), { s(u"/m/a.mp3") }), 0); // it has it already
            break;
        case 1:
            QCOMPARE(store.removeAt(s(u"List"), { 99 }), 0);
            break;
        case 2:
            QVERIFY(store.moveTrack(s(u"List"), 1, 1));
            break;
        default:
            QVERIFY(
                store.setTracks(s(u"List"), { s(u"/m/a.mp3"), s(u"/m/b.mp3"), s(u"/m/c.mp3") }));
            break;
        }
        QVERIFY2(store.lastEditProblem().isEmpty(),
            qPrintable(QStringLiteral("call %1: %2").arg(which).arg(store.lastEditProblem())));
        QCOMPARE(bytesOf(m_file), kThree);
    }
}

void PlaylistsSameFileTest::theHookIsCalledOnceWhenSomethingIsWritten_data()
{
    five();
}

void PlaylistsSameFileTest::theHookIsCalledOnceWhenSomethingIsWritten()
{
    QFETCH(int, which);
    PlaylistStore store(m_root);
    int called = 0;
    QString with;
    store.setBeforeReplace([&](const QString &path) {
        ++called;
        with = path;
    });
    QVERIFY(call(which, store));
    QCOMPARE(called, 1);
    QCOMPARE(with, m_file);
}

void PlaylistsSameFileTest::theHookIsNotCalledWhenNothingIsWritten()
{
    PlaylistStore store(m_root);
    int called = 0;
    store.setBeforeReplace([&](const QString &) { ++called; });
    // nothing to do
    QCOMPARE(store.append(s(u"List"), { s(u"/m/a.mp3") }), 0);
    QCOMPARE(store.removeAt(s(u"List"), { 99 }), 0);
    QVERIFY(store.moveTrack(s(u"List"), 1, 1));
    QVERIFY(!store.moveTrack(s(u"List"), 0, 7));
    QVERIFY(store.setTracks(s(u"List"), { s(u"/m/a.mp3"), s(u"/m/b.mp3"), s(u"/m/c.mp3") }));
    QCOMPARE(store.relink({ { s(u"/m/zzz.mp3"), s(u"/n/zzz.mp3") } }), 0);
    QCOMPARE(store.relink({ }), 0);
    // no such playlist
    QCOMPARE(store.append(s(u"Nobody"), { s(u"/m/new.mp3") }), 0);
    // not to be edited (Y-EDIT)
    QVERIFY(put(m_file, QByteArrayLiteral("/m/Bj\xF6rk.mp3\n")));
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 0);
    QCOMPARE(called, 0);
}

// lastEditProblem also says why a call was refused for an older reason, in editProblem's words.
void PlaylistsSameFileTest::theOlderRefusalsHaveTheirWords()
{
    PlaylistStore store(m_root);
    QCOMPARE(store.append(s(u"Nobody"), { s(u"/m/new.mp3") }), 0);
    QCOMPARE(store.lastEditProblem(), s(u"there is no such playlist"));
    QCOMPARE(store.removeAt(s(u"Nobody"), { 0 }), 0);
    QCOMPARE(store.lastEditProblem(), s(u"there is no such playlist"));

    QVERIFY(put(m_file, QByteArrayLiteral("/m/Bj\xF6rk.mp3\n")));
    QVERIFY(!store.setTracks(s(u"List"), { s(u"/m/x.mp3") }));
    QCOMPARE(store.lastEditProblem(), s(u"the file is not UTF-8"));
    QVERIFY(!store.moveTrack(s(u"List"), 0, 0));
    QCOMPARE(store.lastEditProblem(), s(u"the file is not UTF-8"));

    QVERIFY(put(m_file, kThree));
    store.setFileOpener([](const QString &) -> std::unique_ptr<QIODevice> { return nullptr; });
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 0);
    QCOMPARE(store.lastEditProblem(), s(u"the file could not be read"));
    store.setFileOpener({ });

    // A position outside the list is a refusal of the caller's, not a problem with the file.
    QVERIFY(!store.moveTrack(s(u"List"), 0, 7));
    QCOMPARE(store.lastEditProblem(), QString());
    QCOMPARE(bytesOf(m_file), kThree);
}

// A write that fails for a reason of its own keeps its own words.
void PlaylistsSameFileTest::aFolderThatCannotBeWrittenTo()
{
    PlaylistStore store(m_root);
    QVERIFY(QFile::setPermissions(m_root, QFile::ReadOwner | QFile::ExeOwner));
    struct Restore {
        QString path;
        ~Restore()
        {
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
    } restore { m_root };
    {
        QFile probe(m_root + QStringLiteral("/probe"));
        if (probe.open(QIODevice::WriteOnly)) {
            probe.remove();
            QSKIP("this user writes to a folder it has no right to write to");
        }
    }
    int called = 0;
    store.setBeforeReplace([&](const QString &) { ++called; });
    for (int which = 0; which < 4; ++which) {
        QVERIFY2(!call(which, store), qPrintable(QString::number(which)));
        QCOMPARE(store.lastEditProblem(), s(u"the file could not be written"));
    }
    QCOMPARE(called, 0);
    QCOMPARE(bytesOf(m_file), kThree);
}

// Y-SAME and Y-RELINK: relink takes each playlist by itself. The one that changed is left as the
// other program left it and is named; the others are written.
void PlaylistsSameFileTest::relinkGoesOnPastTheOneThatChanged()
{
    const QString a = m_root + QStringLiteral("/A.m3u8");
    const QString z = m_root + QStringLiteral("/Z.m3u8");
    QVERIFY(put(a, kThree));
    QVERIFY(put(z, kThree));
    PlaylistStore store(m_root);
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QStringList asked;
    store.setBeforeReplace([&](const QString &path) {
        asked.append(QFileInfo(path).fileName());
        if (path == m_file)
            (void)otherProgram(Grown, path);
    });
    QCOMPARE(store.relink({ { s(u"/m/b.mp3"), s(u"/n/b.mp3") } }), 2);
    QCOMPARE(asked, QStringList({ s(u"A.m3u8"), s(u"List.m3u8"), s(u"Z.m3u8") }));
    QCOMPARE(bytesOf(a), after(4));
    QCOMPARE(bytesOf(z), after(4));
    QCOMPARE(bytesOf(m_file), leftBy(Grown));
    QCOMPARE(store.relinkPassedOver(), QStringList({ s(u"List") }));
    QCOMPARE(changed.count(), 2);
    QStringList said;
    for (const QList<QVariant> &one : changed)
        said.append(one.value(0).toString());
    QCOMPARE(said, QStringList({ s(u"A"), s(u"Z") }));
    QCOMPARE(namesIn(m_root), QStringList({ s(u"A.m3u8"), s(u"List.m3u8"), s(u"Z.m3u8") }));
}

QTEST_GUILESS_MAIN(PlaylistsSameFileTest)
#include "tst_playlists_same_file.moc"
