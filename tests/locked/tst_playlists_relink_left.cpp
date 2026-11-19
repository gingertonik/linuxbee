// LOCKED ACCEPTANCE TEST (the playlist store: relink says which playlists it left). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are Y-RELINK in docs/spec/playlists.md. What this holds (the review of fix-96,
// 2026-11-19): a playlist that relink could not read whole, or could not write, was passed over
// and nobody was told, so a person could believe every playlist had followed the files; and a
// pipe behind a link, which the store does list, was held by no test.
//
// A disk error is had as in tst_playlists_oneread.cpp: the store is handed a device that fails.
// The files are real, in a temporary folder.
#include <QBuffer>
#include <QThread>
#include <QtTest>

#include <functional>
#include <memory>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "playlists/playliststore.h"

using linuxbee::PlaylistInfo;
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

// A file of `data.size()` bytes of which only the first `good` can be read. After them a read
// fails (`fails`, a disk error) or says there is no more (the file ended early).
class Broken : public QIODevice {
public:
    Broken(const QByteArray &data, qsizetype good, bool fails)
        : m_data(data)
        , m_good(good)
        , m_fails(fails)
    {
    }
    bool isSequential() const override { return false; }
    qint64 size() const override { return m_data.size(); }

protected:
    qint64 readData(char *to, qint64 max) override
    {
        const qint64 at = pos();
        if (at >= m_good)
            return m_fails ? -1 : 0;
        const qint64 n = qMin(max, qint64(m_good) - at);
        memcpy(to, m_data.constData() + at, size_t(n));
        return n;
    }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QByteArray m_data;
    qsizetype m_good;
    bool m_fails;
};

std::unique_ptr<QIODevice> opened(std::unique_ptr<QIODevice> device)
{
    if (!device->open(QIODevice::ReadOnly))
        return nullptr;
    return device;
}

std::unique_ptr<QIODevice> theFile(const QString &path)
{
    return opened(std::make_unique<QFile>(path));
}

const QByteArray kThree = QByteArrayLiteral("/m/a.mp3\n/m/b.mp3\n/m/c.mp3\n");
const QByteArray kThreeMoved = QByteArrayLiteral("/n/a.mp3\n/m/b.mp3\n/m/c.mp3\n");
const QByteArray kOther = QByteArrayLiteral("/m/z.mp3\n/m/a.mp3\n");
const QByteArray kOtherMoved = QByteArrayLiteral("/m/z.mp3\n/n/a.mp3\n");
const QByteArray kNone = QByteArrayLiteral("/m/q.mp3\n");

QHash<QString, QString> theMove()
{
    return { { QStringLiteral("/m/a.mp3"), QStringLiteral("/n/a.mp3") } };
}

// Runs `call` on a thread of its own and waits two seconds for it. When it has not come back, it
// is waiting at the pipe: the pipe is opened for writing and closed again, as often as needed, so
// that the call ends, and false is answered.
bool comesBack(const QString &pipe, const std::function<void()> &call)
{
    std::unique_ptr<QThread> thread(QThread::create(call));
    thread->start();
    if (thread->wait(2000))
        return true;
    for (int i = 0; i < 200 && !thread->wait(50); ++i) {
        const int fd = ::open(QFile::encodeName(pipe).constData(), O_WRONLY | O_NONBLOCK);
        if (fd >= 0)
            ::close(fd);
    }
    thread->wait();
    return false;
}

}

class PlaylistsRelinkLeftTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void noneLeft();
    void aReadThatStopsPartWay();
    void aFileThatCannotBeOpened();
    void aPipeBehindALink();
    void aPlaylistThatCannotBeWritten();
    void twoLeftAreInOrder();
    void theNextRelinkStartsAgain();

private:
    void put(const QString &name, const QByteArray &data);
    QTemporaryDir m_dir;
    QString m_root;
    QString m_list; // a, b, c
    QString m_other; // z, a
    int m_counter = 0;
};

void PlaylistsRelinkLeftTest::put(const QString &name, const QByteArray &data)
{
    QFile file(m_root + QLatin1Char('/') + name);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        QTest::qFail("could not make a playlist", __FILE__, __LINE__);
}

void PlaylistsRelinkLeftTest::init()
{
    QVERIFY(m_dir.isValid());
    m_root = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_root));
    m_list = m_root + QStringLiteral("/List.m3u8");
    m_other = m_root + QStringLiteral("/Other.m3u8");
    put(s(u"List.m3u8"), kThree);
    put(s(u"Other.m3u8"), kOther);
    put(s(u"Quiet.m3u8"), kNone); // names no file that moves
}

void PlaylistsRelinkLeftTest::noneLeft()
{
    // Every playlist was read whole. One in which nothing changed was not written, and it was not
    // left either: there was nothing to do in it.
    PlaylistStore store(m_root);
    QCOMPARE(store.relinkPassedOver(), QStringList());
    QCOMPARE(store.relink(theMove()), 2);
    QCOMPARE(store.relinkPassedOver(), QStringList());
}

void PlaylistsRelinkLeftTest::aReadThatStopsPartWay()
{
    PlaylistStore store(m_root);
    const QString list = m_list;
    store.setFileOpener([list](const QString &path) -> std::unique_ptr<QIODevice> {
        if (path == list)
            return opened(std::make_unique<Broken>(kThree, 9, true));
        return theFile(path);
    });
    QCOMPARE(store.relink(theMove()), 1);
    QCOMPARE(store.relinkPassedOver(), QStringList({ s(u"List") }));
    QCOMPARE(bytesOf(m_list), kThree);
    QCOMPARE(bytesOf(m_other), kOtherMoved);
}

void PlaylistsRelinkLeftTest::aFileThatCannotBeOpened()
{
    // A playlist that could not be read may name the file that moved or may not: nobody knows.
    // It is said to be left. Here it is Quiet, which names none.
    PlaylistStore store(m_root);
    const QString quiet = m_root + s(u"/Quiet.m3u8");
    store.setFileOpener([quiet](const QString &path) -> std::unique_ptr<QIODevice> {
        return path == quiet ? nullptr : theFile(path);
    });
    QCOMPARE(store.relink(theMove()), 2);
    QCOMPARE(store.relinkPassedOver(), QStringList({ s(u"Quiet") }));
}

void PlaylistsRelinkLeftTest::aPipeBehindALink()
{
    // Y-EDIT, "Not a file". The store does not list a pipe, but it lists a link, and the link
    // may lead to a pipe. relink meets that one. It does not open it, and says it left it.
    const QString pipe = m_root + s(u"/the-pipe");
    QVERIFY(::mkfifo(QFile::encodeName(pipe).constData(), 0600) == 0);
    QVERIFY(QFile::link(pipe, m_root + s(u"/LinkPipe.m3u8")));
    PlaylistStore store(m_root);
    QStringList ids;
    for (const PlaylistInfo &info : store.playlists())
        ids.append(info.id);
    QCOMPARE(
        ids, QStringList({ s(u"LinkPipe"), s(u"List"), s(u"Other"), s(u"Quiet") })); // the control
    int written = -1;
    QVERIFY2(
        comesBack(pipe, [&] { written = store.relink(theMove()); }), "relink waits at the pipe");
    QCOMPARE(written, 2);
    QCOMPARE(store.relinkPassedOver(), QStringList({ s(u"LinkPipe") }));
    QCOMPARE(bytesOf(m_list), kThreeMoved);
    QCOMPARE(bytesOf(m_other), kOtherMoved);
    QString problem;
    QVERIFY2(comesBack(pipe, [&] { problem = store.editProblem(s(u"LinkPipe")); }),
        "editProblem waits at the pipe");
    QCOMPARE(problem, s(u"the file could not be read"));
}

void PlaylistsRelinkLeftTest::aPlaylistThatCannotBeWritten()
{
    // The folder takes no new file, so nothing can be saved in it. Both playlists that name the
    // moved file were read and could not be written: both are left, and are the bytes they were.
    if (::geteuid() == 0)
        QSKIP("the owner of everything can write anywhere");
    PlaylistStore store(m_root);
    QVERIFY(QFile::setPermissions(m_root, QFile::ReadOwner | QFile::ExeOwner));
    const int written = store.relink(theMove());
    const QStringList left = store.relinkPassedOver();
    QVERIFY(QFile::setPermissions(m_root, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QCOMPARE(written, 0);
    QCOMPARE(left, QStringList({ s(u"List"), s(u"Other") }));
    QCOMPARE(bytesOf(m_list), kThree);
    QCOMPARE(bytesOf(m_other), kOther);
}

void PlaylistsRelinkLeftTest::twoLeftAreInOrder()
{
    // In the order of `playlists()`.
    PlaylistStore store(m_root);
    const QString quiet = m_root + s(u"/Quiet.m3u8");
    const QString list = m_list;
    store.setFileOpener([quiet, list](const QString &path) -> std::unique_ptr<QIODevice> {
        return path == quiet || path == list ? nullptr : theFile(path);
    });
    QCOMPARE(store.relink(theMove()), 1);
    QCOMPARE(store.relinkPassedOver(), QStringList({ s(u"List"), s(u"Quiet") }));
}

void PlaylistsRelinkLeftTest::theNextRelinkStartsAgain()
{
    // What was left is what the last relink left.
    PlaylistStore store(m_root);
    const QString list = m_list;
    store.setFileOpener([list](const QString &path) -> std::unique_ptr<QIODevice> {
        return path == list ? nullptr : theFile(path);
    });
    QCOMPARE(store.relink(theMove()), 1);
    QCOMPARE(store.relinkPassedOver(), QStringList({ s(u"List") }));
    store.setFileOpener(nullptr);
    QCOMPARE(store.relink(theMove()), 1); // List now
    QCOMPARE(store.relinkPassedOver(), QStringList());
    store.setFileOpener([list](const QString &path) -> std::unique_ptr<QIODevice> {
        return path == list ? nullptr : theFile(path);
    });
    QCOMPARE(store.relink({ { s(u"/m/b.mp3"), s(u"/n/b.mp3") } }), 0);
    QCOMPARE(store.relinkPassedOver(), QStringList({ s(u"List") }));
    QCOMPARE(store.relink({ }), 0); // nothing moved: nothing was looked at, nothing left
    QCOMPARE(store.relinkPassedOver(), QStringList());
}

QTEST_GUILESS_MAIN(PlaylistsRelinkLeftTest)
#include "tst_playlists_relink_left.moc"
