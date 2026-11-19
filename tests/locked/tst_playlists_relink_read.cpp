// LOCKED ACCEPTANCE TEST (the playlist store: relink reads whole too, and a pipe is not opened).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are Y-RELINK and Y-EDIT ("One read, and a whole one"; "Not a file") in
// docs/spec/playlists.md. The faults this holds shut (the review of fix-86b, 2026-11-18): relink
// still read with one unchecked call, so a read that stopped part-way gave it a short list, and
// when a moved file's line was in that part the short list was written over the playlist; and a
// pipe that has a playlist's name was opened, which waits for ever.
//
// A disk error is had as in tst_playlists_oneread.cpp: the store is handed a device that fails.
// The files are real, in a temporary folder, and are read back here byte by byte.
#include <QBuffer>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <functional>
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

class PlaylistsRelinkReadTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void theControl();
    void aReadThatStopsPartWay_data();
    void aReadThatStopsPartWay();
    void aFileThatCannotBeOpened();
    void eachFileIsReadOnce();
    void whatWasReadIsWhatIsWritten();
    void aPipeIsNotOpened();
    void aPipeAmongPlaylists();

private:
    QTemporaryDir m_dir;
    QString m_root;
    QString m_list; // a, b, c
    QString m_other; // z, a
    int m_counter = 0;
};

void PlaylistsRelinkReadTest::init()
{
    QVERIFY(m_dir.isValid());
    m_root = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_root));
    m_list = m_root + QStringLiteral("/List.m3u8");
    m_other = m_root + QStringLiteral("/Other.m3u8");
    QFile list(m_list);
    QVERIFY(list.open(QIODevice::WriteOnly) && list.write(kThree) == kThree.size());
    QFile other(m_other);
    QVERIFY(other.open(QIODevice::WriteOnly) && other.write(kOther) == kOther.size());
}

void PlaylistsRelinkReadTest::theControl()
{
    // Y-RELINK as it was, with no opener and with one that opens the file itself.
    {
        PlaylistStore store(m_root);
        QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
        QCOMPARE(store.relink(theMove()), 2);
        QCOMPARE(bytesOf(m_list), kThreeMoved);
        QCOMPARE(bytesOf(m_other), kOtherMoved);
        QCOMPARE(changed.count(), 2);
    }
    PlaylistStore store(m_root);
    store.setFileOpener(theFile);
    QCOMPARE(store.relink({ { s(u"/n/a.mp3"), s(u"/m/a.mp3") } }), 2);
    QCOMPARE(bytesOf(m_list), kThree);
    QCOMPARE(bytesOf(m_other), kOther);
}

void PlaylistsRelinkReadTest::aReadThatStopsPartWay_data()
{
    QTest::addColumn<int>("good");
    QTest::addColumn<bool>("fails");
    QTest::newRow("an error after the first line, the one that moved") << 9 << true;
    QTest::newRow("an error in the middle of the second line") << 13 << true;
    QTest::newRow("an error at the last byte") << int(kThree.size()) - 1 << true;
    QTest::newRow("the end comes early") << 9 << false;
}

void PlaylistsRelinkReadTest::aReadThatStopsPartWay()
{
    // List is a, b, c, and a has moved. Reading List gives its first bytes and then stops. It was
    // not read whole, so it is not written: not a byte of it changes, it is not counted and not
    // announced. (It was written back as what had been read: the first line, relinked, alone.)
    // The other playlist, which reads well, is relinked as ever.
    QFETCH(int, good);
    QFETCH(bool, fails);
    PlaylistStore store(m_root);
    const QString list = m_list;
    store.setFileOpener([list, good, fails](const QString &path) -> std::unique_ptr<QIODevice> {
        if (path == list)
            return opened(std::make_unique<Broken>(kThree, good, fails));
        return theFile(path);
    });
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QCOMPARE(store.relink(theMove()), 1);
    QCOMPARE(bytesOf(m_list), kThree);
    QCOMPARE(bytesOf(m_other), kOtherMoved);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).toString(), s(u"Other"));
    QCOMPARE(QDir(m_root).entryList(QDir::Files | QDir::Hidden, QDir::Name),
        QStringList({ s(u"List.m3u8"), s(u"Other.m3u8") }));
}

void PlaylistsRelinkReadTest::aFileThatCannotBeOpened()
{
    PlaylistStore store(m_root);
    const QString list = m_list;
    store.setFileOpener([list](const QString &path) -> std::unique_ptr<QIODevice> {
        return path == list ? nullptr : theFile(path);
    });
    QCOMPARE(store.relink(theMove()), 1);
    QCOMPARE(bytesOf(m_list), kThree);
    QCOMPARE(bytesOf(m_other), kOtherMoved);
}

void PlaylistsRelinkReadTest::eachFileIsReadOnce()
{
    PlaylistStore store(m_root);
    QStringList openedPaths;
    store.setFileOpener([&openedPaths](const QString &path) {
        openedPaths.append(path);
        return theFile(path);
    });
    QCOMPARE(store.relink(theMove()), 2);
    openedPaths.sort();
    QCOMPARE(openedPaths, QStringList({ m_list, m_other }));
}

void PlaylistsRelinkReadTest::whatWasReadIsWhatIsWritten()
{
    // The opener gives a, b, c for List; on the disk List holds something else. What relink
    // writes is made from what its one read gave.
    {
        QFile file(m_list);
        QVERIFY(
            file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write("/m/x.mp3\n") == 9);
    }
    PlaylistStore store(m_root);
    const QString list = m_list;
    store.setFileOpener([list](const QString &path) -> std::unique_ptr<QIODevice> {
        if (path != list)
            return theFile(path);
        auto buffer = std::make_unique<QBuffer>();
        buffer->setData(kThree);
        return opened(std::move(buffer));
    });
    QCOMPARE(store.relink(theMove()), 2);
    QCOMPARE(bytesOf(m_list), kThreeMoved);
}

void PlaylistsRelinkReadTest::aPipeIsNotOpened()
{
    // Y-EDIT, "Not a file": a pipe with a playlist's name. Opening it waits for somebody to write
    // to it, for ever. It is not opened: it cannot be read, it has no tracks, it is not edited.
    const QString pipe = m_root + s(u"/Pipe.m3u8");
    QVERIFY(::mkfifo(QFile::encodeName(pipe).constData(), 0600) == 0);
    PlaylistStore store(m_root);
    QString problem = s(u"<not asked>");
    QVERIFY2(comesBack(pipe, [&] { problem = store.editProblem(s(u"Pipe")); }),
        "editProblem waits at the pipe");
    QCOMPARE(problem, s(u"the file could not be read"));
    QStringList tracks { s(u"<not asked>") };
    QVERIFY2(
        comesBack(pipe, [&] { tracks = store.tracks(s(u"Pipe")); }), "tracks waits at the pipe");
    QCOMPARE(tracks, QStringList());
    int added = -1;
    QVERIFY2(comesBack(pipe, [&] { added = store.append(s(u"Pipe"), { s(u"/m/new.mp3") }); }),
        "append waits at the pipe");
    QCOMPARE(added, 0);
    bool set = true;
    QVERIFY2(comesBack(pipe, [&] { set = store.setTracks(s(u"Pipe"), { s(u"/m/new.mp3") }); }),
        "setTracks waits at the pipe");
    QVERIFY(!set);
    QFileInfo info(pipe);
    QVERIFY(info.exists() && !info.isFile()); // still the pipe
}

void PlaylistsRelinkReadTest::aPipeAmongPlaylists()
{
    // relink goes through every playlist of the store. The pipe is passed over; the others are
    // relinked.
    const QString pipe = m_root + s(u"/Pipe.m3u8");
    QVERIFY(::mkfifo(QFile::encodeName(pipe).constData(), 0600) == 0);
    PlaylistStore store(m_root);
    int written = -1;
    QVERIFY2(
        comesBack(pipe, [&] { written = store.relink(theMove()); }), "relink waits at the pipe");
    QCOMPARE(written, 2);
    QCOMPARE(bytesOf(m_list), kThreeMoved);
    QCOMPARE(bytesOf(m_other), kOtherMoved);
}

QTEST_GUILESS_MAIN(PlaylistsRelinkReadTest)
#include "tst_playlists_relink_read.moc"
