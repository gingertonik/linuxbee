// LOCKED ACCEPTANCE TEST (a save that cannot be written in full is not put in the song's place).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never by
// editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-safe-save.md (WS-WRITTEN). Found by the audit of the
// tag write path: with no room for the grown file, the save said Ok and a cut-off file replaced the
// song. "No room" is real here: the process's own limit on the size of a file it writes
// (RLIMIT_FSIZE), set for the length of one call, with its signal ignored, so that a write past the
// limit fails as a write to a full disk does. Works on copies of the fixtures in a temporary
// folder.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <csignal>
#include <sys/resource.h>

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

// Everything in the folder, hidden files too.
QStringList listing(const QString &folder)
{
    return QDir(folder).entryList(
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

// A picture of this many bytes that no two calls make alike in size only.
Picture picture(qsizetype bytes)
{
    Picture p;
    p.type = QStringLiteral("Front Cover");
    p.mimeType = QStringLiteral("image/jpeg");
    p.data = QByteArray::fromHex("ffd8ffe000104a46494600");
    quint32 x = 2463534242u;
    while (p.data.size() < bytes) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        p.data.append(char(x & 0xff));
    }
    p.byteCount = p.data.size();
    return p;
}

// The change of the audit: a title, and a cover that makes the file grow by `grow` bytes or so.
TagChanges grown(qsizetype grow)
{
    TagChanges c;
    c.fields.insert(QStringLiteral("Title"), { QStringLiteral("No Room") });
    c.pictures = QList<Picture> { picture(grow) };
    return c;
}

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

WriteResult::Status saveWithRoomFor(const QString &path, const TagChanges &changes, qint64 bytes)
{
    const NoRoomPast limit(bytes);
    if (!limit.isSet())
        return WriteResult::Status::NotFound; // the test could not set its limit; no case expects
                                              // this status
    return writeTags(path, changes).status;
}

struct Audio {
    QString format;
    int sampleRate = 0, channels = 0;
    qint64 durationMs = 0;
    bool operator==(const Audio &) const = default;
};

Audio audioOf(const QString &path)
{
    const ReadResult r = readTags(path);
    if (r.status != ReadResult::Status::Ok)
        return { QStringLiteral("<unreadable>"), 0, 0, 0 };
    return { r.tags.format, r.tags.audio.sampleRateHz, r.tags.audio.channels,
        r.tags.audio.durationMs };
}

const char *const kFixtures[] = { "basic.mp3", "v23.mp3", "basic.flac", "basic.ogg", "basic.opus",
    "basic.m4a", "basic.wav", "basic.wv" };

}

class SaveNoRoomTest : public QObject {
    Q_OBJECT
private slots:
    void theLimitIsReal();
    void noRoomForWhatWasAdded_data();
    void noRoomForWhatWasAdded();
    void whereverTheRoomEnds_data();
    void whereverTheRoomEnds();
    void noRoomForTheCopyItself();
    void aSmallerFileNeedsNoRoom();
};

void SaveNoRoomTest::theLimitIsReal()
{
    // The test's own ground: under the limit a write past it fails and the process lives.
    QTemporaryDir dir;
    QFile f(dir.path() + s(u"/probe"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Unbuffered));
    {
        const NoRoomPast limit(1000);
        QVERIFY(limit.isSet());
        QCOMPARE(f.write(QByteArray(600, 'a')), 600);
        QVERIFY(f.write(QByteArray(600, 'b')) < 600);
    }
    QCOMPARE(f.write(QByteArray(600, 'c')), 600); // and the limit is gone again
    QVERIFY(f.size() >= 1600);
}

void SaveNoRoomTest::noRoomForWhatWasAdded_data()
{
    QTest::addColumn<QString>("name");
    for (const char *name : kFixtures)
        QTest::newRow(name) << QString::fromLatin1(name);
}

void SaveNoRoomTest::noRoomForWhatWasAdded()
{
    // WS-WRITTEN: there is room for the copy and not for what the change adds. The save fails, the
    // song is byte for byte what it was, and nothing is left beside it.
    QFETCH(QString, name);
    QTemporaryDir dir;
    const QString path = fixture(dir, qPrintable(name), s(u"album/") + name);
    const QString folder = dir.path() + s(u"/album");
    const QByteArray before = hashOf(path);
    const Audio audio = audioOf(path);
    const qint64 size = QFileInfo(path).size();
    QVERIFY2(audio.format != s(u"<unreadable>"), "the fixture cannot be read");
    const TagChanges changes = grown(300000);

    QCOMPARE(saveWithRoomFor(path, changes, size + 8192), WriteResult::Status::Failed);
    QCOMPARE(hashOf(path), before);
    QCOMPARE(listing(folder), QStringList { name });

    // Twice: a save that failed leaves nothing that makes the next one behave otherwise.
    QCOMPARE(saveWithRoomFor(path, changes, size + 150000), WriteResult::Status::Failed);
    QCOMPARE(hashOf(path), before);
    QCOMPARE(listing(folder), QStringList { name });

    // With room, the same change is saved, and the file is whole: the cover is there to the last
    // byte and the audio is what it was.
    QCOMPARE(writeTags(path, changes).status, WriteResult::Status::Ok);
    QCOMPARE(listing(folder), QStringList { name });
    ReadOptions withData;
    withData.pictureData = true;
    const ReadResult after = readTags(path, withData);
    QCOMPARE(after.status, ReadResult::Status::Ok);
    QCOMPARE(after.tags.first(QStringLiteral("Title")), s(u"No Room"));
    QCOMPARE(after.tags.pictures.size(), 1);
    QCOMPARE(after.tags.pictures.at(0).data, changes.pictures->at(0).data);
    QCOMPARE(audioOf(path), audio);
    QVERIFY(QFileInfo(path).size() > size + 290000); // a format may use room it had kept free
}

void SaveNoRoomTest::whereverTheRoomEnds_data()
{
    QTest::addColumn<QString>("name");
    for (const char *name : kFixtures)
        QTest::newRow(name) << QString::fromLatin1(name);
}

void SaveNoRoomTest::whereverTheRoomEnds()
{
    // WS-WRITTEN: there is no amount of room at which a save says Ok and the file is not the whole
    // new version. The whole new version is what the same save makes with all the room it wants.
    QFETCH(QString, name);
    QTemporaryDir dir;
    const TagChanges changes = grown(40000);
    const QString whole = fixture(dir, qPrintable(name), s(u"whole/") + name);
    const qint64 size = QFileInfo(whole).size();
    const QByteArray before = hashOf(whole);
    QCOMPARE(writeTags(whole, changes).status, WriteResult::Status::Ok);
    const QByteArray wanted = hashOf(whole);
    const qint64 wantedSize = QFileInfo(whole).size();
    QVERIFY(wantedSize > size + 30000);

    int failed = 0, saved = 0;
    // From one byte more than the song to past the new version: fine steps at both ends, where a
    // header or a last block is written, coarse between.
    QList<qint64> rooms;
    for (qint64 r = size + 1; r < size + 600; r += 37)
        rooms.append(r);
    for (qint64 r = size + 600; r < wantedSize - 600; r += 1777)
        rooms.append(r);
    for (qint64 r = wantedSize - 600; r <= wantedSize + 64; r += 13)
        rooms.append(r);
    int n = 0;
    for (const qint64 room : std::as_const(rooms)) {
        const QString sub = QStringLiteral("r%1").arg(++n);
        const QString path = fixture(dir, qPrintable(name), sub + QLatin1Char('/') + name);
        const WriteResult::Status status = saveWithRoomFor(path, changes, room);
        const QByteArray now = hashOf(path);
        const QByteArray where = QByteArray("with room for ") + QByteArray::number(room)
            + " bytes (the song is " + QByteArray::number(size) + ", the new version "
            + QByteArray::number(wantedSize) + ")";
        if (status == WriteResult::Status::Ok) {
            ++saved;
            QVERIFY2(now == wanted, "said Ok and the file is not the whole new version, " + where);
        } else {
            ++failed;
            QVERIFY2(status == WriteResult::Status::Failed, "neither Ok nor Failed, " + where);
            QVERIFY2(now == before, "failed and the song was changed, " + where);
        }
        QVERIFY2(listing(dir.path() + QLatin1Char('/') + sub) == QStringList { name },
            "something was left beside the song, " + where);
        QDir(dir.path() + QLatin1Char('/') + sub).removeRecursively();
    }
    // Both happened, so the steps did cross the place where the room ends.
    QVERIFY2(failed > 20,
        qPrintable(QStringLiteral("only %1 of %2 failed").arg(failed).arg(rooms.size())));
    QVERIFY2(saved > 2,
        qPrintable(QStringLiteral("only %1 of %2 were saved").arg(saved).arg(rooms.size())));
}

void SaveNoRoomTest::noRoomForTheCopyItself()
{
    // WS-WRITTEN: not even the copy fits. The same: Failed, the song as it was, nothing left.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.wav", s(u"album/song.wav"));
    const QByteArray before = hashOf(path);
    const qint64 size = QFileInfo(path).size();
    for (const qint64 room : { size / 2, size - 1, qint64(1) }) {
        QCOMPARE(saveWithRoomFor(path, grown(1000), room), WriteResult::Status::Failed);
        QCOMPARE(hashOf(path), before);
        QCOMPARE(listing(dir.path() + s(u"/album")), QStringList { s(u"song.wav") });
    }
}

void SaveNoRoomTest::aSmallerFileNeedsNoRoom()
{
    // Not every save under a limit fails: one that makes the file no larger is saved.
    QTemporaryDir dir;
    const QString path = fixture(dir, "basic.flac", s(u"song.flac"));
    QCOMPARE(writeTags(path, grown(50000)).status, WriteResult::Status::Ok);
    const qint64 size = QFileInfo(path).size();
    TagChanges smaller;
    smaller.fields.insert(QStringLiteral("Title"), { QStringLiteral("T") });
    smaller.pictures = QList<Picture> { };
    QCOMPARE(saveWithRoomFor(path, smaller, size), WriteResult::Status::Ok);
    QCOMPARE(readTags(path).tags.first(QStringLiteral("Title")), s(u"T"));
    QVERIFY(QFileInfo(path).size() <= size);
}

QTEST_GUILESS_MAIN(SaveNoRoomTest)
#include "tst_tagsafesave_noroom.moc"
