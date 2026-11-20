// LOCKED ACCEPTANCE TEST (reading tags: what the last page of an Ogg file may add to its length).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are R-AUDIO ("The length of an Ogg file") in docs/spec/tag-reading.md, and
// L-SCAN ("the reader's number") in docs/spec/library.md. The first test of this rule
// (tst_tagreader_ogg_length.cpp) holds that a last page the tag library did not see is added.
// This one holds what is not added: a file is not to be trusted, and a page whose numbers make
// no sense adds nothing. (Found by the review of the first building, 2026-11-19: lengths below
// zero and of weeks, and three Ogg FLAC files in a thousand made wrong that were right.)
//
// No encoder is used. Each file is a copy of a fixture (a quarter of a second) with one more
// page after it, made here byte by byte. A page of 1022 bytes at the end of a file is one the
// tag library does not see: it takes the page before. The Ogg FLAC file is given here as its
// bytes (236 of them, made once with the reference encoder from basic.flac).
// MATH: 250 + 2000 ms; 250 + 40000 ms; 2 s and 30 units more is 2000.68 ms (Vorbis, 44100 a
// second) and 2000.63 ms (Opus, 48000), so 2251 to the nearest and 2250 cut off.
// MEASURED (the program before this rule, which is the tag library alone, 2026-11-19): where
// the page it takes has a position below zero it says 0 for Vorbis and for Opus.
#include <QtEndian>
#include <QtTest>

#include <limits>

#include "library/library.h"
#include "tags/tagreader.h"

using linuxbee::ReadResult;

namespace {

const int kNotSeen = 1022; // a last page of this size is not seen by the tag library
const int kSeen = 1000;

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The fixture of that name; the Ogg FLAC one is here as its bytes.
QByteArray fixtureBytes(const QByteArray &name)
{
    if (name == "basic.oga") {
        return QByteArray::fromHex(
            "4f67675300020000000000000000016797460000000052c2deaf01337f464c414301000001664c61"
            "43000000221000100000000e0000100ac442f000002b11bd881f582a1efb5094ca6681ec0b48e94f"
            "6767530000000000000000000001679746010000003e009757013784000033200000007265666572"
            "656e6365206c6962464c414320312e352e3020323032353032313101000000070000005449544c45"
            "3d784f6767530004112b00000000000001679746020000001c7106ec030e0e10fff8c91800c20000"
            "00000000b8eefff8c91801c50000000000002f9bfff87918020b10370000000000004cf7");
    }
    return bytesOf(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(name));
}

// The units of the stream's position in a second.
qint64 rateOf(const QByteArray &name)
{
    return name.endsWith(".opus") ? 48000 : 44100;
}

// The length the reader gives, or -1 when it cannot read the file.
int lengthOf(const QString &path)
{
    const ReadResult result = linuxbee::readTags(path);
    return result.status == ReadResult::Status::Ok ? result.tags.audio.durationMs : -1;
}

// One Ogg page of `total` bytes: the last of its stream, at `position`, with the stream number
// of the last page of `after`, and the next sequence number. Its packet is filler.
QByteArray pageOf(int total, qint64 position, const QByteArray &after)
{
    const qsizetype last = after.lastIndexOf("OggS");
    if (last < 0 || last + 27 > after.size())
        return { };
    // 27 bytes of header, n lacing values of which all but the last are 255, and the packet.
    int n = 1;
    while (total - 27 - n - 255 * (n - 1) > 254)
        ++n;
    const int rest = total - 27 - n - 255 * (n - 1);
    if (rest < 0 || n > 255)
        return { };
    QByteArray page("OggS");
    page.append(char(0)); // the version
    page.append(char(4)); // the last page of the stream
    char number[8];
    qToLittleEndian<qint64>(position, number);
    page.append(number, 8);
    page.append(after.mid(last + 14, 4)); // the stream
    qToLittleEndian<quint32>(qFromLittleEndian<quint32>(after.constData() + last + 18) + 1, number);
    page.append(number, 4);
    page.append(4, char(0)); // the checksum
    page.append(char(n));
    page.append(n - 1, char(255));
    page.append(char(rest));
    page.append(255 * (n - 1) + rest, char(0x55));
    return page;
}

qint64 lastPosition(const QByteArray &bytes)
{
    const qsizetype last = bytes.lastIndexOf("OggS");
    return last < 0 ? -1 : qFromLittleEndian<qint64>(bytes.constData() + last + 6);
}

// `bytes` with the position of its last page written over.
QByteArray withLastPosition(QByteArray bytes, qint64 position)
{
    const qsizetype last = bytes.lastIndexOf("OggS");
    if (last < 0 || last + 14 > bytes.size())
        return { };
    qToLittleEndian<qint64>(position, bytes.data() + last + 6);
    return bytes;
}

qint64 withTheTopBit(qint64 position)
{
    return static_cast<qint64>(static_cast<quint64>(position) | (quint64(1) << 63));
}

}

class TagReaderOggLengthGuardedTest : public QObject {
    Q_OBJECT
private slots:
    void aPageThatHoldsMuch_data();
    void aPageThatHoldsMuch();
    void aPositionThatIsNone_data();
    void aPositionThatIsNone();
    void aPositionNotLater_data();
    void aPositionNotLater();
    void aPositionTooFarOn_data();
    void aPositionTooFarOn();
    void thePageTakenHasNoPosition_data();
    void thePageTakenHasNoPosition();
    void bytesAfterAPageNotSeen_data();
    void bytesAfterAPageNotSeen();
    void toTheNearestMillisecond_data();
    void toTheNearestMillisecond();
    void anOggFlacFileIsReadAsBefore_data();
    void anOggFlacFileIsReadAsBefore();
    void theReadersNumberWasRaised();

private:
    QString written(const QByteArray &name, const QByteArray &bytes);
    void both();
    void positions(const QList<QPair<const char *, qint64>> &ahead, bool fromTheLast);
    QTemporaryDir m_dir;
    int m_counter = 0;
};

void TagReaderOggLengthGuardedTest::both()
{
    QTest::addColumn<QByteArray>("name");
    QTest::newRow("Vorbis") << QByteArrayLiteral("basic.ogg");
    QTest::newRow("Opus") << QByteArrayLiteral("basic.opus");
}

// Rows of a kind and a position for the page added. `fromTheLast`: the number is a count of
// seconds on from the fixture's last page; else it is the position itself.
void TagReaderOggLengthGuardedTest::positions(
    const QList<QPair<const char *, qint64>> &ahead, bool fromTheLast)
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<qint64>("position");
    for (const char *name : { "basic.ogg", "basic.opus" }) {
        for (const auto &[what, number] : ahead) {
            const qint64 position
                = fromTheLast ? lastPosition(fixtureBytes(name)) + number * rateOf(name) : number;
            QTest::addRow("%s, %s", name, what) << QByteArray(name) << position;
        }
    }
}

// The bytes as a file of the fixture's kind. Empty when it could not be written.
QString TagReaderOggLengthGuardedTest::written(const QByteArray &name, const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return { };
    const QString path
        = m_dir.filePath(QStringLiteral("%1-%2").arg(++m_counter).arg(QLatin1String(name)));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return { };
    return path;
}

void TagReaderOggLengthGuardedTest::aPageThatHoldsMuch_data()
{
    positions({ { "forty seconds on", 40 } }, true);
}

void TagReaderOggLengthGuardedTest::aPageThatHoldsMuch()
{
    // R-AUDIO: what one page can hold is added, though it is far more than a second.
    QFETCH(QByteArray, name);
    QFETCH(qint64, position);
    const QByteArray fixture = fixtureBytes(name);
    const QString path = written(name, fixture + pageOf(kNotSeen, position, fixture));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 40250);
}

void TagReaderOggLengthGuardedTest::aPositionThatIsNone_data()
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<qint64>("position");
    for (const char *name : { "basic.ogg", "basic.opus" }) {
        const qint64 good = lastPosition(fixtureBytes(name)) + 2 * rateOf(name);
        QTest::addRow("%s, -1", name) << QByteArray(name) << qint64(-1);
        QTest::addRow("%s, the lowest number", name)
            << QByteArray(name) << std::numeric_limits<qint64>::min();
        QTest::addRow("%s, two seconds on with the top bit set", name)
            << QByteArray(name) << withTheTopBit(good);
    }
}

void TagReaderOggLengthGuardedTest::aPositionThatIsNone()
{
    // R-AUDIO: a position below zero is no position (-1 is how a page says that no packet
    // ends on it). Nothing is added.
    QFETCH(QByteArray, name);
    QFETCH(qint64, position);
    const QByteArray fixture = fixtureBytes(name);
    const QString path = written(name, fixture + pageOf(kNotSeen, position, fixture));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 250);
}

void TagReaderOggLengthGuardedTest::aPositionNotLater_data()
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<qint64>("position");
    for (const char *name : { "basic.ogg", "basic.opus" }) {
        const qint64 taken = lastPosition(fixtureBytes(name));
        QTest::addRow("%s, the same", name) << QByteArray(name) << taken;
        QTest::addRow("%s, a hundred earlier", name) << QByteArray(name) << taken - 100;
        QTest::addRow("%s, nought", name) << QByteArray(name) << qint64(0);
    }
}

void TagReaderOggLengthGuardedTest::aPositionNotLater()
{
    // R-AUDIO: nothing is taken away. A last page that is not later adds nothing.
    QFETCH(QByteArray, name);
    QFETCH(qint64, position);
    const QByteArray fixture = fixtureBytes(name);
    const QString path = written(name, fixture + pageOf(kNotSeen, position, fixture));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 250);
}

void TagReaderOggLengthGuardedTest::aPositionTooFarOn_data()
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<qint64>("position");
    for (const char *name : { "basic.ogg", "basic.opus" }) {
        const qint64 taken = lastPosition(fixtureBytes(name));
        QTest::addRow("%s, an hour on", name) << QByteArray(name) << taken + 3600 * rateOf(name);
        QTest::addRow("%s, two to the fortieth", name) << QByteArray(name) << (qint64(1) << 40);
        QTest::addRow("%s, two to the sixty-second", name) << QByteArray(name) << (qint64(1) << 62);
        QTest::addRow("%s, the highest number", name)
            << QByteArray(name) << std::numeric_limits<qint64>::max();
    }
}

void TagReaderOggLengthGuardedTest::aPositionTooFarOn()
{
    // R-AUDIO: more than one page can hold is not added. The length is the tag library's.
    QFETCH(QByteArray, name);
    QFETCH(qint64, position);
    const QByteArray fixture = fixtureBytes(name);
    const QString path = written(name, fixture + pageOf(kNotSeen, position, fixture));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 250);
}

void TagReaderOggLengthGuardedTest::thePageTakenHasNoPosition_data()
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<qint64>("taken");
    for (const char *name : { "basic.ogg", "basic.opus" }) {
        QTest::addRow("%s, -1", name) << QByteArray(name) << qint64(-1);
        QTest::addRow("%s, the lowest number", name)
            << QByteArray(name) << std::numeric_limits<qint64>::min();
    }
}

void TagReaderOggLengthGuardedTest::thePageTakenHasNoPosition()
{
    // R-AUDIO: where the page the tag library took has no position there is nothing to add
    // to. The length is the tag library's (MEASURED: 0), not one made up from nought.
    QFETCH(QByteArray, name);
    QFETCH(qint64, taken);
    const QByteArray fixture = fixtureBytes(name);
    const qint64 good = lastPosition(fixture) + 2 * rateOf(name);
    const QByteArray before = withLastPosition(fixture, taken);
    const QString path = written(name, before + pageOf(kNotSeen, good, fixture));
    QVERIFY2(!before.isEmpty() && !path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 0);
}

void TagReaderOggLengthGuardedTest::bytesAfterAPageNotSeen_data()
{
    both();
}

void TagReaderOggLengthGuardedTest::bytesAfterAPageNotSeen()
{
    // R-AUDIO, known and left: a last page the tag library does not see, with bytes after it,
    // is no page that ends where the file ends. The length is the tag library's.
    QFETCH(QByteArray, name);
    const QByteArray fixture = fixtureBytes(name);
    const qint64 good = lastPosition(fixture) + 2 * rateOf(name);
    const QString path = written(
        name, fixture + pageOf(kNotSeen - 10, good, fixture) + QByteArray(10, char(0x55)));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 250);
}

void TagReaderOggLengthGuardedTest::toTheNearestMillisecond_data()
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<int>("total");
    for (const char *name : { "basic.ogg", "basic.opus" }) {
        QTest::addRow("%s, a page that is seen", name) << QByteArray(name) << kSeen;
        QTest::addRow("%s, a page that is not", name) << QByteArray(name) << kNotSeen;
    }
}

void TagReaderOggLengthGuardedTest::toTheNearestMillisecond()
{
    // R-AUDIO: the length is to the nearest millisecond, as the tag library's own is, whether
    // or not the tag library saw the page.
    QFETCH(QByteArray, name);
    QFETCH(int, total);
    const QByteArray fixture = fixtureBytes(name);
    const qint64 position = lastPosition(fixture) + 2 * rateOf(name) + 30;
    const QString path = written(name, fixture + pageOf(total, position, fixture));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 2251);
}

void TagReaderOggLengthGuardedTest::anOggFlacFileIsReadAsBefore_data()
{
    QTest::addColumn<int>("total");
    QTest::newRow("as it is") << 0;
    QTest::newRow("a last page that is seen") << kSeen;
    QTest::newRow("a last page that is not") << kNotSeen;
}

void TagReaderOggLengthGuardedTest::anOggFlacFileIsReadAsBefore()
{
    // R-AUDIO: the length of an Ogg FLAC file is written at its head, and the tag library
    // reads it there. No page changes it.
    QFETCH(int, total);
    const QByteArray name("basic.oga");
    const QByteArray fixture = fixtureBytes(name);
    QCOMPARE(fixture.size(), 236);
    const qint64 position = lastPosition(fixture) + 2 * rateOf(name);
    const QString path
        = written(name, total == 0 ? fixture : fixture + pageOf(total, position, fixture));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 250);
}

void TagReaderOggLengthGuardedTest::theReadersNumberWasRaised()
{
    // L-SCAN: the reader makes something else of some files now, so its number is 3 or more,
    // and a library read before is read again.
    QVERIFY(linuxbee::kReaderVersion >= 3);
}

QTEST_GUILESS_MAIN(TagReaderOggLengthGuardedTest)
#include "tst_tagreader_ogg_length_guarded.moc"
