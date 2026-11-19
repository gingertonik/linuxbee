// LOCKED ACCEPTANCE TEST (the playlist store: what replaces a playlist is whole, flushed and has a
// name of its own before the second look). Written by the reviewer, not the builder. The builder
// makes these pass by changing src/, never by editing this file. A test believed to be wrong is
// reported, with the reason, and left alone.
//
// Expected values are Y-SAME in docs/spec/playlists.md, steps 3 and 6. The fault this holds shut
// (the review of that rule's first build, 2026-11-19): the second look was taken, and then the
// file beside was flushed, named and closed, and only then put in place. That took about four
// tenths of a millisecond on the owner's disk, half the time of the whole call, and about half of
// what another program wrote meanwhile was still lost.
//
// No test can stand between the look and the replacing. What a test can see is that everything
// else is done before the hook: there, the new bytes are in a file with a name of its own, all of
// them; and the file the playlist is afterwards is that very file, so nothing was written,
// copied or named after it. The files are real, in a temporary folder.
#include <QRegularExpression>
#include <QtTest>

#include <sys/stat.h>

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

// Every name in a folder, hidden ones too, sorted.
QStringList namesIn(const QString &folder)
{
    return QDir(folder).entryList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

// The number the system gives the file at a name, and its mode. 0 when nothing has the name.
quint64 numberOf(const QString &path)
{
    struct stat st = { };
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? quint64(st.st_ino) : 0;
}

int modeOf(const QString &path)
{
    struct stat st = { };
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? int(st.st_mode & 07777) : -1;
}

const QByteArray kThree = QByteArrayLiteral("/m/a.mp3\n/m/b.mp3\n/m/c.mp3\n");
// What a run that was killed between the naming and the replacing leaves.
const QByteArray kLeftover = QByteArrayLiteral("/old/x.mp3\n");

bool isASpareName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^\\.lbsave-[a-z0-9]{6}$"));
    return pattern.match(name).hasMatch();
}

}

class PlaylistsSameFileNamedTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void atTheHookTheNewBytesHaveANameAndAreWhole_data();
    void atTheHookTheNewBytesHaveANameAndAreWhole();
    void aRefusalTakesTheNameAway_data();
    void aRefusalTakesTheNameAway();
    void aLeftoverIsNoPlaylistAndIsLeftAlone();
    void theModeOfThePlaylistStays();

private:
    bool call(int which, PlaylistStore &store) const;
    QByteArray after(int which) const;
    void five();
    QTemporaryDir m_dir;
    QString m_root;
    QString m_file;
    int m_counter = 0;
};

void PlaylistsSameFileNamedTest::init()
{
    QVERIFY(m_dir.isValid());
    m_root = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_root));
    m_file = m_root + QStringLiteral("/List.m3u8");
    QVERIFY(put(m_file, kThree));
}

void PlaylistsSameFileNamedTest::five()
{
    QTest::addColumn<int>("which");
    QTest::newRow("append") << 0;
    QTest::newRow("removeAt") << 1;
    QTest::newRow("moveTrack") << 2;
    QTest::newRow("setTracks") << 3;
    QTest::newRow("relink") << 4;
}

bool PlaylistsSameFileNamedTest::call(int which, PlaylistStore &store) const
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

QByteArray PlaylistsSameFileNamedTest::after(int which) const
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

void PlaylistsSameFileNamedTest::atTheHookTheNewBytesHaveANameAndAreWhole_data()
{
    five();
}

void PlaylistsSameFileNamedTest::atTheHookTheNewBytesHaveANameAndAreWhole()
{
    // Y-SAME 3: when the hook is called, one more name is in the folder. It is a spare name, and
    // the file under it holds the whole of what the playlist is to be. Y-SAME 6: the playlist is
    // afterwards that very file, by its number: it was put in place by a rename, and nothing was
    // written, copied or given a name after the hook.
    QFETCH(int, which);
    PlaylistStore store(m_root);
    QStringList names;
    QByteArray held;
    quint64 number = 0;
    const quint64 was = numberOf(m_file);
    store.setBeforeReplace([&](const QString &) {
        names = namesIn(m_root);
        names.removeAll(s(u"List.m3u8"));
        held = bytesOf(m_root + QLatin1Char('/') + names.value(0));
        number = numberOf(m_root + QLatin1Char('/') + names.value(0));
    });
    QVERIFY(call(which, store));
    QCOMPARE(names.size(), 1);
    QVERIFY2(isASpareName(names.value(0)), qPrintable(names.value(0)));
    QCOMPARE(held, after(which));
    QVERIFY(number != 0 && number != was);
    QCOMPARE(numberOf(m_file), number);
    QCOMPARE(bytesOf(m_file), after(which));
    QCOMPARE(namesIn(m_root), QStringList({ s(u"List.m3u8") }));
}

void PlaylistsSameFileNamedTest::aRefusalTakesTheNameAway_data()
{
    five();
}

void PlaylistsSameFileNamedTest::aRefusalTakesTheNameAway()
{
    // The other program adds an entry at the hook. Nothing is replaced, and the spare name that
    // was there at the hook is gone: only the playlist is in the folder, as the other left it.
    QFETCH(int, which);
    PlaylistStore store(m_root);
    QStringList names;
    bool did = false;
    store.setBeforeReplace([&](const QString &path) {
        names = namesIn(m_root);
        names.removeAll(s(u"List.m3u8"));
        QThread::msleep(30); // a coarse clock gives two changes in one tick the same stamp
        QFile file(path);
        did = file.open(QIODevice::WriteOnly | QIODevice::Append)
            && file.write("/o/other.mp3\n") == 13;
    });
    const bool answer = call(which, store);
    QVERIFY2(did, "the rig could not change the file");
    QVERIFY2(!answer, "the call answered that it wrote");
    QCOMPARE(names.size(), 1);
    QVERIFY2(isASpareName(names.value(0)), qPrintable(names.value(0)));
    QCOMPARE(bytesOf(m_file), kThree + QByteArrayLiteral("/o/other.mp3\n"));
    QCOMPARE(namesIn(m_root), QStringList({ s(u"List.m3u8") }));
}

void PlaylistsSameFileNamedTest::aLeftoverIsNoPlaylistAndIsLeftAlone()
{
    // A run killed between the naming and the replacing leaves a spare name. It is not a
    // playlist, it is not in the way of the next call, and nothing removes or reuses it.
    const QString leftover = m_root + s(u"/.lbsave-k3x9qa");
    QVERIFY(put(leftover, kLeftover));
    PlaylistStore store(m_root);
    QStringList ids;
    for (const linuxbee::PlaylistInfo &info : store.playlists())
        ids.append(info.id);
    QCOMPARE(ids, QStringList({ s(u"List") }));
    QStringList atTheHook;
    store.setBeforeReplace([&](const QString &) { atTheHook = namesIn(m_root); });
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 1);
    QCOMPARE(atTheHook.size(), 3); // the leftover, the call's own spare name, the playlist
    QVERIFY(atTheHook.contains(s(u".lbsave-k3x9qa")));
    QCOMPARE(bytesOf(m_file), kThree + QByteArrayLiteral("/m/new.mp3\n"));
    QCOMPARE(bytesOf(leftover), kLeftover);
    QCOMPARE(namesIn(m_root), QStringList({ s(u".lbsave-k3x9qa"), s(u"List.m3u8") }));
}

void PlaylistsSameFileNamedTest::theModeOfThePlaylistStays()
{
    // As before this rule: a playlist that only its owner may read is not opened to others by
    // being written.
    QVERIFY(QFile::setPermissions(m_file, QFile::ReadOwner | QFile::WriteOwner));
    QCOMPARE(modeOf(m_file), 0600);
    PlaylistStore store(m_root);
    store.setBeforeReplace([](const QString &) { });
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 1);
    QCOMPARE(bytesOf(m_file), kThree + QByteArrayLiteral("/m/new.mp3\n"));
    QCOMPARE(modeOf(m_file), 0600);
}

QTEST_GUILESS_MAIN(PlaylistsSameFileNamedTest)
#include "tst_playlists_same_file_named.moc"
