// LOCKED ACCEPTANCE TEST (the playlist store: a playlist is written on a disk that has no second
// names for a file). Written by the reviewer, not the builder. The builder makes these pass by
// changing src/, never by editing this file. A test believed to be wrong is reported, with the
// reason, and left alone.
//
// Expected values are Y-SAME in docs/spec/playlists.md, step 3 ("No step needs a second name").
// The fault this holds shut (the second review of that rule, 2026-11-19): the file beside got
// its name as a second name of an open file (`linkat`). A disk without second names (FAT, exFAT,
// some FUSE) refuses that, and every edit of a playlist there failed, where it had worked
// before.
//
// No such disk can be made in a test without rights the gate does not have. So the test runs
// itself a second time as a child, and the child forbids itself the calls first: `link` and
// `linkat` answer "not permitted", and a file without a name cannot be opened (`O_TMPFILE`
// answers "not supported"), which is what such a disk says. Then it makes one call of the store.
// Where the system does not let a program forbid itself a call, the cases are skipped and say
// so. The files are real, in a temporary folder.
#include <QCoreApplication>
#include <QProcess>
#include <QtTest>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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

QStringList namesIn(const QString &folder)
{
    return QDir(folder).entryList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

int modeOf(const QString &path)
{
    struct stat st = { };
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? int(st.st_mode & 07777) : -1;
}

const QByteArray kThree = QByteArrayLiteral("/m/a.mp3\n/m/b.mp3\n/m/c.mp3\n");

bool call(int which, PlaylistStore &store)
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

QByteArray after(int which)
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

// What the child answers with.
enum { Wrote = 0, DidNotWrite = 1, CannotForbid = 77, ForbiddingDidNotHold = 78 };

// From here on this program cannot give a file a second name, and cannot open a file that has
// no name. False where the system does not allow a program to forbid itself a call.
bool forbidSecondNames()
{
    const unsigned noName = unsigned(O_TMPFILE);
#define LB_REFUSE(number, error)                                                                   \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, unsigned(number), 0, 1),                                   \
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | unsigned(error))
#define LB_REFUSE_NO_NAME(number, argument)                                                        \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, unsigned(number), 0, 5),                                   \
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,                                                         \
            unsigned(offsetof(struct seccomp_data, args) + (argument) * sizeof(__u64))),           \
        BPF_STMT(BPF_ALU | BPF_AND | BPF_K, noName),                                               \
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, noName, 0, 1),                                         \
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | unsigned(EOPNOTSUPP)),                       \
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, unsigned(offsetof(struct seccomp_data, nr)))
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, unsigned(offsetof(struct seccomp_data, nr))),
        LB_REFUSE(SYS_linkat, EPERM),
#ifdef SYS_link
        LB_REFUSE(SYS_link, EPERM),
#endif
        LB_REFUSE_NO_NAME(SYS_openat, 2),
#ifdef SYS_open
        LB_REFUSE_NO_NAME(SYS_open, 1),
#endif
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
#undef LB_REFUSE
#undef LB_REFUSE_NO_NAME
    struct sock_fprog program = { };
    program.len = static_cast<unsigned short>(sizeof filter / sizeof filter[0]);
    program.filter = filter;
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        return false;
    return ::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
}

// The second run of this program: one call of the store, with second names forbidden.
int child(const QString &root, int which)
{
    if (!forbidSecondNames())
        return CannotForbid;
    // That it holds, shown on a file of the child's own.
    const QByteArray probe = QFile::encodeName(root + s(u"/probe"));
    const QByteArray second = QFile::encodeName(root + s(u"/probe2"));
    const int fd = ::open(probe.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return ForbiddingDidNotHold;
    ::close(fd);
    const bool linked = ::link(probe.constData(), second.constData()) == 0;
    const int nameless
        = ::open(QFile::encodeName(root).constData(), O_TMPFILE | O_WRONLY | O_CLOEXEC, 0600);
    if (nameless >= 0)
        ::close(nameless);
    ::unlink(probe.constData());
    ::unlink(second.constData());
    if (linked || nameless >= 0)
        return ForbiddingDidNotHold;

    PlaylistStore store(root);
    const bool wrote = call(which, store);
    if (!wrote)
        std::fprintf(stderr, "the store said: %s\n", qPrintable(store.lastEditProblem()));
    return wrote ? Wrote : DidNotWrite;
}

}

class PlaylistsSameFileNoLinkTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void withoutSecondNamesEachCallWrites_data();
    void withoutSecondNamesEachCallWrites();
    void atTheHookTheFileBesideHasThePlaylistsMode_data();
    void atTheHookTheFileBesideHasThePlaylistsMode();

private:
    QTemporaryDir m_dir;
    QString m_root;
    QString m_file;
    int m_counter = 0;
};

void PlaylistsSameFileNoLinkTest::init()
{
    QVERIFY(m_dir.isValid());
    m_root = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_root));
    m_file = m_root + QStringLiteral("/List.m3u8");
    QVERIFY(put(m_file, kThree));
}

void PlaylistsSameFileNoLinkTest::withoutSecondNamesEachCallWrites_data()
{
    QTest::addColumn<int>("which");
    QTest::newRow("append") << 0;
    QTest::newRow("removeAt") << 1;
    QTest::newRow("moveTrack") << 2;
    QTest::newRow("setTracks") << 3;
    QTest::newRow("relink") << 4;
}

void PlaylistsSameFileNoLinkTest::withoutSecondNamesEachCallWrites()
{
    // Y-SAME 3: no step needs a second name. The call writes, the playlist holds the new list,
    // and nothing else is left in the folder.
    QFETCH(int, which);
    QProcess second;
    second.setProcessChannelMode(QProcess::ForwardedChannels);
    second.start(QCoreApplication::applicationFilePath(),
        { QStringLiteral("--without-second-names"), m_root, QString::number(which) });
    QVERIFY(second.waitForFinished(60000));
    QCOMPARE(second.exitStatus(), QProcess::NormalExit);
    if (second.exitCode() == CannotForbid)
        QSKIP("SKIPPED: this system does not let a program forbid itself a call");
    QVERIFY2(second.exitCode() != ForbiddingDidNotHold,
        "second names were forbidden and could still be made: the rig is wrong");
    QCOMPARE(second.exitCode(), int(Wrote));
    QCOMPARE(bytesOf(m_file), after(which));
    QCOMPARE(namesIn(m_root), QStringList({ s(u"List.m3u8") }));
}

void PlaylistsSameFileNoLinkTest::atTheHookTheFileBesideHasThePlaylistsMode_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("only the owner") << 0600;
    QTest::newRow("the owner and the group") << 0640;
    QTest::newRow("all may read") << 0644;
}

void PlaylistsSameFileNoLinkTest::atTheHookTheFileBesideHasThePlaylistsMode()
{
    // Y-SAME 3: at the hook the file beside has the mode of the playlist, so that what replaces
    // a playlist only its owner may read was not open to others while it waited.
    QFETCH(int, mode);
    QCOMPARE(::chmod(QFile::encodeName(m_file).constData(), mode_t(mode)), 0);
    PlaylistStore store(m_root);
    QStringList names;
    int atTheHook = -1;
    store.setBeforeReplace([&](const QString &) {
        names = namesIn(m_root);
        names.removeAll(s(u"List.m3u8"));
        atTheHook = modeOf(m_root + QLatin1Char('/') + names.value(0));
    });
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 1);
    QCOMPARE(names.size(), 1);
    QCOMPARE(atTheHook, mode);
    QCOMPARE(modeOf(m_file), mode);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 4 && std::strcmp(argv[1], "--without-second-names") == 0)
        return child(QString::fromLocal8Bit(argv[2]), QByteArray(argv[3]).toInt());
    PlaylistsSameFileNoLinkTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_playlists_same_file_nolink.moc"
