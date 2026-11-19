// LOCKED ACCEPTANCE TEST (reading tags: the length of an Ogg file is that of its last page).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are R-AUDIO ("The length of an Ogg file") in docs/spec/tag-reading.md. The
// fault this holds shut (a red gate on the build machine of the project, 2026-11-19): the tag
// library finds the last page of an Ogg file by looking back from the end for "OggS" in reads of
// 1024 bytes that do not overlap. When those four bytes lie across the border of two reads the
// page is not seen and the page before it is taken. About three files in a thousand read short
// by a page, which is about a second as ffmpeg writes them.
//
// No encoder is used, so the case does not depend on how one lays its pages out. Each file is a
// copy of a fixture (basic.ogg, basic.opus: a quarter of a second) with one more page after it,
// of a chosen size, that ends two seconds later. The page is made here, byte by byte; its
// checksum is left at zero, which a reader of lengths does not look at. MATH: 250 + 2000 ms.
#include <QtEndian>
#include <QtTest>

#include "tags/tagreader.h"

using linuxbee::ReadResult;

namespace {

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(name);
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The length the reader gives, or -1 when it cannot read the file.
int lengthOf(const QString &path)
{
    const ReadResult result = linuxbee::readTags(path);
    return result.status == ReadResult::Status::Ok ? result.tags.audio.durationMs : -1;
}

// One Ogg page of `total` bytes: the last of its stream, ending at `granule`, with the stream
// number of the page `after`, and the next sequence number. Its packet is filler.
QByteArray pageOf(int total, qint64 granule, const QByteArray &after)
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
    qToLittleEndian<qint64>(granule, number);
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

qint64 lastGranule(const QByteArray &bytes)
{
    const qsizetype last = bytes.lastIndexOf("OggS");
    return last < 0 ? -1 : qFromLittleEndian<qint64>(bytes.constData() + last + 6);
}

}

class TagReaderOggLengthTest : public QObject {
    Q_OBJECT
private slots:
    void theFixturesAsTheyAre_data();
    void theFixturesAsTheyAre();
    void aLastPageOfAnySize_data();
    void aLastPageOfAnySize();
    void bytesAfterTheLastPage_data();
    void bytesAfterTheLastPage();

private:
    QString made(const char *name, int total, const QByteArray &trailing = { });
    void both();
    QTemporaryDir m_dir;
    int m_counter = 0;
};

void TagReaderOggLengthTest::both()
{
    QTest::addColumn<QByteArray>("name");
    QTest::newRow("Vorbis") << QByteArrayLiteral("basic.ogg");
    QTest::newRow("Opus") << QByteArrayLiteral("basic.opus");
}

// A copy of the fixture with one more page of `total` bytes, two seconds on, and `trailing`
// after it. Empty when it could not be made.
QString TagReaderOggLengthTest::made(const char *name, int total, const QByteArray &trailing)
{
    const QByteArray fixtureBytes = bytesOf(fixture(name));
    const int rate = QByteArray(name).endsWith(".opus") ? 48000 : 44100;
    const QByteArray page = pageOf(total, lastGranule(fixtureBytes) + 2 * rate, fixtureBytes);
    if (fixtureBytes.isEmpty() || page.size() != total)
        return { };
    const QString path
        = m_dir.filePath(QStringLiteral("%1-%2").arg(++m_counter).arg(QLatin1String(name)));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(fixtureBytes + page + trailing)
            != fixtureBytes.size() + total + trailing.size())
        return { };
    return path;
}

void TagReaderOggLengthTest::theFixturesAsTheyAre_data()
{
    both();
}

void TagReaderOggLengthTest::theFixturesAsTheyAre()
{
    // GEN: the fixtures are a quarter of a second. Nothing is added to a file that is read
    // right already.
    QFETCH(QByteArray, name);
    QCOMPARE(lengthOf(fixture(name.constData())), 250);
}

void TagReaderOggLengthTest::aLastPageOfAnySize_data()
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<int>("total");
    // From the end of the file the page begins `total` bytes back. 1021 to 1023 put its four
    // bytes across the first border of the reads; 2045 to 2047 and 4093 across later ones.
    for (const char *name : { "basic.ogg", "basic.opus" }) {
        for (const int total : { 40, 1000, 1020, 1021, 1022, 1023, 1024, 1025, 1500, 2044, 2045,
                 2046, 2047, 2048, 4093, 4096, 30000 }) {
            QTest::addRow("%s, a last page of %d bytes", name, total) << QByteArray(name) << total;
        }
    }
}

void TagReaderOggLengthTest::aLastPageOfAnySize()
{
    // R-AUDIO: the length is that of the last page, wherever in the file it begins.
    QFETCH(QByteArray, name);
    QFETCH(int, total);
    const QString path = made(name.constData(), total);
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 2250);
}

void TagReaderOggLengthTest::bytesAfterTheLastPage_data()
{
    both();
}

void TagReaderOggLengthTest::bytesAfterTheLastPage()
{
    // As before this rule: bytes after the last page do not hide a page that the tag library
    // sees. (What is read when it does not see it is not held here: R-AUDIO, known and left.)
    QFETCH(QByteArray, name);
    const QString path = made(name.constData(), 1000, QByteArray(10, char(0x55)));
    QVERIFY2(!path.isEmpty(), "the file could not be made");
    QCOMPARE(lengthOf(path), 2250);
}

QTEST_GUILESS_MAIN(TagReaderOggLengthTest)
#include "tst_tagreader_ogg_length.moc"
