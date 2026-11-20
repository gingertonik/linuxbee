// LOCKED ACCEPTANCE TEST (reading tags: the length of an Ogg file, three points no test held).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are R-AUDIO ("The length of an Ogg file", "What the last page may add") in
// docs/spec/tag-reading.md. The review of that rule planted three faults that every test let
// pass: Speex left out, the limit moved, and the sum let past what `durationMs` can hold. The
// program is right in all three already: these cases pass before any change.
//
// No encoder is used. A page of 1022 bytes at the end of a file is one the tag library does
// not see. The Speex file is given here as the bytes of its two header pages (168 of them,
// made once with the reference encoder, 16000 a second, one channel); its sound page is made
// here. The Vorbis file "of one unit a second" is basic.ogg with the rate in its header
// written over: nonsense that the tag library accepts, and the only way to the last guard.
// MATH: the limit is 255 * 8192 = 2,088,960 units. At 44100 that is 47368.7 ms, so 250 + 47369;
// at 48000 it is 43520 ms, so 250 + 43520. At one unit a second a unit is 1000 ms:
// 1000 units and the limit are 2,089,960,000 ms, which fits; 100,000 units and the limit
// are 2,188,960,000 ms, which does not, and the length is then the tag library's 100,000,000.
#include <QtEndian>
#include <QtTest>

#include "tags/tagreader.h"

using linuxbee::ReadResult;

namespace {

const int kNotSeen = 1022; // a last page of this size is not seen by the tag library
const qint64 kLimit = 255 * 8192;

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray fixtureBytes(const char *name)
{
    return bytesOf(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(name));
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

// A Speex file of a quarter of a second: the two header pages as the encoder wrote them, and
// one page of sound that ends at 4000.
QByteArray speexBytes()
{
    const QByteArray head = QByteArray::fromHex(
        "4f676753000200000000000000005cbeb25500000000b0f57b6701505370656578202020312e322e"
        "310000000000000000000000000000000100000050000000803e0000010000000400000001000000"
        "ffffffff4001000000000000010000000000000000000000000000004f6767530000000000000000"
        "00005cbeb25501000000a5f7e8af012018000000456e636f64656420776974682053706565782031"
        "2e322e3100000000");
    return head + pageOf(60, 4000, head);
}

// basic.ogg with a rate of one unit a second in its header, and its last page at `position`.
QByteArray oneUnitASecond(qint64 position)
{
    QByteArray bytes = fixtureBytes("basic.ogg");
    const qsizetype header = bytes.indexOf(QByteArrayLiteral("\x01vorbis"));
    if (header < 0)
        return { };
    qToLittleEndian<quint32>(1, bytes.data() + header + 12);
    return withLastPosition(bytes, position);
}

}

class TagReaderOggLengthLimitsTest : public QObject {
    Q_OBJECT
private slots:
    void aSpeexFileAsItIs();
    void aSpeexFileIsCorrectedToo();
    void theLimitIsAddedAndOneMoreIsNot_data();
    void theLimitIsAddedAndOneMoreIsNot();
    void aSumThatFitsAndOneThatDoesNot_data();
    void aSumThatFitsAndOneThatDoesNot();

private:
    QString written(const char *name, const QByteArray &bytes);
    QTemporaryDir m_dir;
    int m_counter = 0;
};

QString TagReaderOggLengthLimitsTest::written(const char *name, const QByteArray &bytes)
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

void TagReaderOggLengthLimitsTest::aSpeexFileAsItIs()
{
    const QByteArray speex = speexBytes();
    QCOMPARE(speex.size(), 228);
    const QString path = written("basic.spx", speex);
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    const ReadResult result = linuxbee::readTags(path);
    QCOMPARE(result.status, ReadResult::Status::Ok);
    QCOMPARE(result.tags.audio.sampleRateHz, 16000);
    QCOMPARE(result.tags.audio.durationMs, 250);
}

void TagReaderOggLengthLimitsTest::aSpeexFileIsCorrectedToo()
{
    // R-AUDIO: Speex has its length from the last page, as Vorbis and Opus have.
    const QByteArray speex = speexBytes();
    const QString path = written("basic.spx", speex + pageOf(kNotSeen, 4000 + 2 * 16000, speex));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 2250);
}

void TagReaderOggLengthLimitsTest::theLimitIsAddedAndOneMoreIsNot_data()
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<qint64>("more");
    QTest::addColumn<int>("expected");
    QTest::newRow("Vorbis, the limit") << QByteArrayLiteral("basic.ogg") << kLimit << 47619;
    QTest::newRow("Vorbis, one more") << QByteArrayLiteral("basic.ogg") << kLimit + 1 << 250;
    QTest::newRow("Vorbis, one less") << QByteArrayLiteral("basic.ogg") << kLimit - 1 << 47619;
    QTest::newRow("Opus, the limit") << QByteArrayLiteral("basic.opus") << kLimit << 43770;
    QTest::newRow("Opus, one more") << QByteArrayLiteral("basic.opus") << kLimit + 1 << 250;
}

void TagReaderOggLengthLimitsTest::theLimitIsAddedAndOneMoreIsNot()
{
    // R-AUDIO: what one page can hold, 2,088,960 units, is added; more than that is not.
    QFETCH(QByteArray, name);
    QFETCH(qint64, more);
    QFETCH(int, expected);
    const QByteArray fixture = fixtureBytes(name.constData());
    const QString path = written(
        name.constData(), fixture + pageOf(kNotSeen, lastPosition(fixture) + more, fixture));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), expected);
}

void TagReaderOggLengthLimitsTest::aSumThatFitsAndOneThatDoesNot_data()
{
    QTest::addColumn<qint64>("taken");
    QTest::addColumn<int>("expected");
    QTest::newRow("it fits") << qint64(1000) << 2089960000;
    QTest::newRow("it does not fit") << qint64(100000) << 100000000;
}

void TagReaderOggLengthLimitsTest::aSumThatFitsAndOneThatDoesNot()
{
    // R-AUDIO: a sum that would not fit `durationMs` is not made. The length is the tag
    // library's, and never a number below zero.
    QFETCH(qint64, taken);
    QFETCH(int, expected);
    const QByteArray before = oneUnitASecond(taken);
    const QString path = written("rate1.ogg", before + pageOf(kNotSeen, taken + kLimit, before));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    const ReadResult result = linuxbee::readTags(path);
    QCOMPARE(result.status, ReadResult::Status::Ok);
    QCOMPARE(result.tags.audio.sampleRateHz, 1);
    QCOMPARE(result.tags.audio.durationMs, expected);
}

QTEST_GUILESS_MAIN(TagReaderOggLengthLimitsTest)
#include "tst_tagreader_ogg_length_limits.moc"
