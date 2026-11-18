// LOCKED ACCEPTANCE TEST (the playlist store: one read, and a whole one). Written by the reviewer,
// not the builder. The builder makes these pass by changing src/, never by editing this file. A
// test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are Y-EDIT in docs/spec/playlists.md ("One read, and a whole one"). The faults
// this holds shut (the review of fix-86, 2026-11-18, B1): a read that stopped part-way with a disk
// error left no error behind, so the list was taken to be its first few bytes, and one added track
// wrote that over the file; and the check and the edit read the file apart, so a file that could
// not be read the second time was emptied.
//
// A disk error cannot be had from a real file in a temporary folder, so the store is told how to
// open a playlist file (PlaylistStore::setFileOpener) and is handed a device that fails. The files
// themselves are real, in a temporary folder, and are read back here byte by byte.
#include <QBuffer>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

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
const QString kNotRead = QStringLiteral("the file could not be read");

}

class PlaylistsOneReadTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void theSeamChangesNothingByItself();
    void aReadThatStopsPartWay_data();
    void aReadThatStopsPartWay();
    void theFileIsReadOnce_data();
    void theFileIsReadOnce();
    void whatWasReadIsWhatIsEdited_data();
    void whatWasReadIsWhatIsEdited();
    void aSecondOpenThatFails();
    void nothingToWriteIsFoundFromTheOneRead();
    void theProblemIsToldFromTheSameRead();

private:
    // The four calls that change entries, by number: each on a list of a, b, c.
    bool call(int which, PlaylistStore &store) const;
    QByteArray after(int which) const;
    void four();
    QTemporaryDir m_dir;
    QString m_root;
    QString m_file;
    int m_counter = 0;
};

void PlaylistsOneReadTest::init()
{
    QVERIFY(m_dir.isValid());
    m_root = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_root));
    m_file = m_root + QStringLiteral("/List.m3u8");
    QFile file(m_file);
    QVERIFY(file.open(QIODevice::WriteOnly) && file.write(kThree) == kThree.size());
}

void PlaylistsOneReadTest::four()
{
    QTest::addColumn<int>("which");
    QTest::newRow("append") << 0;
    QTest::newRow("removeAt") << 1;
    QTest::newRow("moveTrack") << 2;
    QTest::newRow("setTracks") << 3;
}

bool PlaylistsOneReadTest::call(int which, PlaylistStore &store) const
{
    switch (which) {
    case 0:
        return store.append(s(u"List"), { s(u"/m/new.mp3") }) == 1;
    case 1:
        return store.removeAt(s(u"List"), { 1 }) == 1;
    case 2:
        return store.moveTrack(s(u"List"), 0, 2);
    default:
        return store.setTracks(s(u"List"), { s(u"/m/only.mp3") });
    }
}

// What the file holds after that call on a, b, c.
QByteArray PlaylistsOneReadTest::after(int which) const
{
    switch (which) {
    case 0:
        return kThree + QByteArrayLiteral("/m/new.mp3\n");
    case 1:
        return QByteArrayLiteral("/m/a.mp3\n/m/c.mp3\n");
    case 2:
        return QByteArrayLiteral("/m/b.mp3\n/m/c.mp3\n/m/a.mp3\n");
    default:
        return QByteArrayLiteral("/m/only.mp3\n");
    }
}

void PlaylistsOneReadTest::theSeamChangesNothingByItself()
{
    // The control: an opener that opens the file itself is the store as it was.
    PlaylistStore store(m_root);
    store.setFileOpener(theFile);
    QCOMPARE(store.editProblem(s(u"List")), QString());
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 1);
    QCOMPARE(bytesOf(m_file), after(0));
    // And with none given, the same.
    PlaylistStore plain(m_root);
    QCOMPARE(plain.removeAt(s(u"List"), { 3 }), 1);
    QCOMPARE(bytesOf(m_file), kThree);
}

void PlaylistsOneReadTest::aReadThatStopsPartWay_data()
{
    QTest::addColumn<int>("which");
    QTest::addColumn<int>("good");
    QTest::addColumn<bool>("fails");
    const char *names[] = { "append", "removeAt", "moveTrack", "setTracks" };
    for (int which = 0; which < 4; ++which) {
        QTest::addRow("%s, an error after 4 bytes", names[which]) << which << 4 << true;
        QTest::addRow("%s, an error after the first line", names[which]) << which << 9 << true;
        QTest::addRow("%s, an error at the last byte", names[which])
            << which << int(kThree.size()) - 1 << true;
        QTest::addRow("%s, the end comes early", names[which]) << which << 9 << false;
    }
}

void PlaylistsOneReadTest::aReadThatStopsPartWay()
{
    // B1. The file is a, b, c. Reading it gives its first bytes and then stops. It was not read
    // whole, so it is not edited: the call fails, not a byte changes, nothing is announced.
    QFETCH(int, which);
    QFETCH(int, good);
    QFETCH(bool, fails);
    PlaylistStore store(m_root);
    store.setFileOpener([good, fails](const QString &) {
        return opened(std::make_unique<Broken>(kThree, good, fails));
    });
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QCOMPARE(store.editProblem(s(u"List")), kNotRead);
    QVERIFY(!call(which, store));
    QCOMPARE(bytesOf(m_file), kThree);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(QDir(m_root).entryList(QDir::Files | QDir::Hidden), QStringList({ s(u"List.m3u8") }));
}

void PlaylistsOneReadTest::theFileIsReadOnce_data()
{
    four();
}

void PlaylistsOneReadTest::theFileIsReadOnce()
{
    // One read for one call: what is judged, what is edited and what the result is compared with
    // are the same bytes.
    QFETCH(int, which);
    PlaylistStore store(m_root);
    int opens = 0;
    store.setFileOpener([&opens](const QString &path) {
        ++opens;
        return theFile(path);
    });
    QVERIFY(call(which, store));
    QCOMPARE(opens, 1);
    QCOMPARE(bytesOf(m_file), after(which));
}

void PlaylistsOneReadTest::whatWasReadIsWhatIsEdited_data()
{
    four();
}

void PlaylistsOneReadTest::whatWasReadIsWhatIsEdited()
{
    // The first open gives a, b, c. Any later open gives a file that has lost its entries (it
    // was cut off meanwhile), and a read that does not go through the opener finds another list
    // altogether. The call works on what its one read gave, and on nothing else.
    QFETCH(int, which);
    {
        QFile other(m_file);
        QVERIFY(other.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && other.write("/m/x.mp3\n") == 9);
    }
    PlaylistStore store(m_root);
    int opens = 0;
    store.setFileOpener([&opens](const QString &) -> std::unique_ptr<QIODevice> {
        auto buffer = std::make_unique<QBuffer>();
        buffer->setData(++opens == 1 ? kThree : QByteArray());
        return opened(std::move(buffer));
    });
    QVERIFY(call(which, store));
    QCOMPARE(bytesOf(m_file), after(which));
}

void PlaylistsOneReadTest::aSecondOpenThatFails()
{
    // The review's second shape: the file can be opened once and not again. One track is added;
    // the list is the three and the new one, not the new one alone.
    PlaylistStore store(m_root);
    int opens = 0;
    store.setFileOpener([&opens](const QString &path) -> std::unique_ptr<QIODevice> {
        return ++opens == 1 ? theFile(path) : nullptr;
    });
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 1);
    QCOMPARE(bytesOf(m_file), after(0));
}

void PlaylistsOneReadTest::nothingToWriteIsFoundFromTheOneRead()
{
    // Y-STORE: a call that changes nothing writes nothing and announces nothing. That too is
    // found from the one read.
    PlaylistStore store(m_root);
    int opens = 0;
    store.setFileOpener([&opens](const QString &path) {
        ++opens;
        return theFile(path);
    });
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QVERIFY(store.setTracks(s(u"List"), { s(u"/m/a.mp3"), s(u"/m/b.mp3"), s(u"/m/c.mp3") }));
    QCOMPARE(opens, 1);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(bytesOf(m_file), kThree);

    // The same when a read of its own would find other bytes: what was read holds a, b, c, so
    // there is nothing to write, and nothing is written.
    {
        QFile other(m_file);
        QVERIFY(other.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && other.write("/m/x.mp3\n") == 9);
    }
    store.setFileOpener([](const QString &) -> std::unique_ptr<QIODevice> {
        auto buffer = std::make_unique<QBuffer>();
        buffer->setData(kThree);
        return opened(std::move(buffer));
    });
    QVERIFY(store.setTracks(s(u"List"), { s(u"/m/a.mp3"), s(u"/m/b.mp3"), s(u"/m/c.mp3") }));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(bytesOf(m_file), QByteArray("/m/x.mp3\n"));
}

void PlaylistsOneReadTest::theProblemIsToldFromTheSameRead()
{
    // Y-EDIT, as it was, through the opener: what is not UTF-8 is not edited, and a file that
    // cannot be opened is not either.
    PlaylistStore store(m_root);
    store.setFileOpener([](const QString &) -> std::unique_ptr<QIODevice> {
        auto buffer = std::make_unique<QBuffer>();
        buffer->setData(QByteArrayLiteral("/m/caf\xE9.mp3\n"));
        return opened(std::move(buffer));
    });
    QCOMPARE(store.editProblem(s(u"List")), s(u"the file is not UTF-8"));
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 0);
    QCOMPARE(bytesOf(m_file), kThree);
    store.setFileOpener([](const QString &) -> std::unique_ptr<QIODevice> { return nullptr; });
    QCOMPARE(store.editProblem(s(u"List")), kNotRead);
    QCOMPARE(store.append(s(u"List"), { s(u"/m/new.mp3") }), 0);
    QCOMPARE(bytesOf(m_file), kThree);
}

QTEST_GUILESS_MAIN(PlaylistsOneReadTest)
#include "tst_playlists_oneread.moc"
