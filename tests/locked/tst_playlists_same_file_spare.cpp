// LOCKED ACCEPTANCE TEST (the playlist store: the spare name of a write).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are Y-SAME in docs/spec/playlists.md, step 3 and "A refusal at step 5". Two
// rules that no test held (the review of 2026-11-19 planted a fault for each and every test
// passed): a step that fails takes the spare name away, and a name that is taken is never
// used. The store draws its spare names by chance, so for the second the test hands them out
// (`setSpareNames`). The files are real, in a temporary folder.
#include <QtTest>

#include <csignal>
#include <sys/resource.h>

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

const QByteArray kThree = QByteArrayLiteral("/m/a.mp3\n/m/b.mp3\n/m/c.mp3\n");
const QByteArray kMine = QByteArrayLiteral("mine, and no playlist\n");
const QString kUnwritten = QStringLiteral("the file could not be written");

// For as long as it lives no file this process writes may pass `bytes`; a write past it fails.
class NoRoomPast {
public:
    explicit NoRoomPast(qint64 bytes)
    {
        m_signal = ::signal(SIGXFSZ, SIG_IGN);
        ::getrlimit(RLIMIT_FSIZE, &m_before);
        struct rlimit now = m_before;
        now.rlim_cur = rlim_t(bytes);
        m_set = ::setrlimit(RLIMIT_FSIZE, &now) == 0;
    }
    ~NoRoomPast()
    {
        ::setrlimit(RLIMIT_FSIZE, &m_before);
        ::signal(SIGXFSZ, m_signal);
    }
    bool isSet() const { return m_set; }

private:
    struct rlimit m_before { };
    void (*m_signal)(int) = SIG_DFL;
    bool m_set = false;
};

}

class PlaylistsSameFileSpareTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void aWriteThatFailsTakesTheSpareNameAway();
    void aNameThatIsTakenIsPassedOver();
    void whenEveryNameIsTakenNothingIsWritten();
    void withNoNamesHandedOutTheStoreDrawsItsOwn();

private:
    QTemporaryDir m_dir;
    QString m_root;
    QString m_file;
    int m_counter = 0;
};

void PlaylistsSameFileSpareTest::init()
{
    QVERIFY(m_dir.isValid());
    m_root = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_root));
    m_file = m_root + QStringLiteral("/List.m3u8");
    QVERIFY(put(m_file, kThree));
}

void PlaylistsSameFileSpareTest::aWriteThatFailsTakesTheSpareNameAway()
{
    // Y-SAME: the new bytes do not fit (no file may pass 16 bytes here). The call fails and
    // says so, the playlist is as it was, and nothing is left beside it.
    PlaylistStore store(m_root);
    {
        const NoRoomPast limit(16);
        QVERIFY(limit.isSet());
        QVERIFY(store.append(s(u"List"), { s(u"/m/new.mp3") }) != 1);
    }
    QCOMPARE(store.lastEditProblem(), kUnwritten);
    QCOMPARE(namesIn(m_root), QStringList({ s(u"List.m3u8") }));
    QCOMPARE(bytesOf(m_file), kThree);
    // With room again the same call writes.
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 1);
    QCOMPARE(bytesOf(m_file), kThree + QByteArrayLiteral("/m/new.mp3\n"));
    QCOMPARE(namesIn(m_root), QStringList({ s(u"List.m3u8") }));
}

void PlaylistsSameFileSpareTest::aNameThatIsTakenIsPassedOver()
{
    // Y-SAME, step 3: "made new (never a name that is there)". The first name handed out is
    // another file's. It is not written to, and the next name is asked for and used.
    const QString taken = m_root + s(u"/.lbsave-taken1");
    QVERIFY(put(taken, kMine));
    PlaylistStore store(m_root);
    int asked = 0;
    store.setSpareNames(
        [&asked] { return ++asked == 1 ? QStringLiteral("taken1") : QStringLiteral("free22"); });
    QStringList atTheHook;
    QByteArray takenAtTheHook;
    store.setBeforeReplace([&](const QString &) {
        atTheHook = namesIn(m_root);
        takenAtTheHook = bytesOf(taken);
    });
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 1);
    QCOMPARE(asked, 2);
    QCOMPARE(
        atTheHook, QStringList({ s(u".lbsave-free22"), s(u".lbsave-taken1"), s(u"List.m3u8") }));
    QCOMPARE(takenAtTheHook, kMine);
    QCOMPARE(bytesOf(taken), kMine);
    QCOMPARE(bytesOf(m_file), kThree + QByteArrayLiteral("/m/new.mp3\n"));
    QCOMPARE(namesIn(m_root), QStringList({ s(u".lbsave-taken1"), s(u"List.m3u8") }));
}

void PlaylistsSameFileSpareTest::whenEveryNameIsTakenNothingIsWritten()
{
    // Y-SAME, step 3: after a hundred names that are taken the call fails as a write that
    // fails. The file whose name it is, is as it was, and so is the playlist.
    const QString taken = m_root + s(u"/.lbsave-taken1");
    QVERIFY(put(taken, kMine));
    PlaylistStore store(m_root);
    int asked = 0;
    store.setSpareNames([&asked] {
        ++asked;
        return QStringLiteral("taken1");
    });
    bool hookCalled = false;
    store.setBeforeReplace([&hookCalled](const QString &) { hookCalled = true; });
    QVERIFY(store.append(s(u"List"), { s(u"/m/new.mp3") }) != 1);
    QCOMPARE(store.lastEditProblem(), kUnwritten);
    QCOMPARE(asked, 100);
    QVERIFY(!hookCalled);
    QCOMPARE(bytesOf(taken), kMine);
    QCOMPARE(bytesOf(m_file), kThree);
    QCOMPARE(namesIn(m_root), QStringList({ s(u".lbsave-taken1"), s(u"List.m3u8") }));
}

void PlaylistsSameFileSpareTest::withNoNamesHandedOutTheStoreDrawsItsOwn()
{
    // Handing out nothing (an empty function) is as never having set one.
    PlaylistStore store(m_root);
    store.setSpareNames({ });
    QStringList atTheHook;
    store.setBeforeReplace([&](const QString &) { atTheHook = namesIn(m_root); });
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 1);
    QCOMPARE(atTheHook.size(), 2);
    QVERIFY(atTheHook.value(0).startsWith(s(u".lbsave-")));
    QCOMPARE(atTheHook.value(0).size(), 14);
    QCOMPARE(namesIn(m_root), QStringList({ s(u"List.m3u8") }));
}

QTEST_GUILESS_MAIN(PlaylistsSameFileSpareTest)
#include "tst_playlists_same_file_spare.moc"
