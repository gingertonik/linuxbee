// LOCKED ACCEPTANCE TEST (Speex carries Vorbis comments). Written by the reviewer, not the
// builder. The builder makes these pass by changing src/, never by editing this file. A test
// believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-writing.md (W-TABLE). A Speex file in Ogg carries
// Vorbis comments, as FLAC, Ogg Vorbis and Opus do, so its keys are theirs: TRACKTOTAL and
// TOTALDISCS beside the numbers, never `n/total` (the owner's ruling of 2026-11-20, N8, names
// Speex). The file is made here, in a folder of its own; no fixture is touched.
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

using linuxbee::ReadResult;
using linuxbee::TagChanges;
using linuxbee::WriteResult;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArrayLiteral("<cannot open>");
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

}

class TagsSpeexTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void numbersAndTotals();
    void otherVorbisKeys();
    void aClearedCount();

private:
    QString speexFile();
    QTemporaryDir m_dir;
    int m_counter = 0;
};

void TagsSpeexTest::init()
{
    QVERIFY(m_dir.isValid());
}

QString TagsSpeexTest::speexFile()
{
    const QString path = m_dir.filePath(QStringLiteral("%1-basic.spx").arg(++m_counter));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(speexBytes()) != 228)
        return { };
    return path;
}

void TagsSpeexTest::numbersAndTotals()
{
    // W-TABLE: Disc# and Disc Count are DISCNUMBER and TOTALDISCS; Track# and Track Count are
    // TRACKNUMBER and TRACKTOTAL. No `n/total`, and no DISCTOTAL is made (W-DISCTOTAL).
    const QString path = speexFile();
    QVERIFY(!path.isEmpty());
    TagChanges c;
    c.fields = { { s(u"Disc#"), { s(u"1") } }, { s(u"Disc Count"), { s(u"3") } },
        { s(u"Track#"), { s(u"4") } }, { s(u"Track Count"), { s(u"9") } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    const QByteArray b = bytesOf(path);
    QVERIFY(b.contains("DISCNUMBER=1"));
    QVERIFY(!b.contains("DISCNUMBER=1/3"));
    QVERIFY(b.contains("TOTALDISCS=3"));
    QVERIFY(!b.contains("DISCTOTAL="));
    QVERIFY(b.contains("TRACKNUMBER=4"));
    QVERIFY(!b.contains("TRACKNUMBER=4/9"));
    QVERIFY(b.contains("TRACKTOTAL=9"));
    const ReadResult read = linuxbee::readTags(path);
    QCOMPARE(read.status, ReadResult::Status::Ok);
    QCOMPARE(read.tags.fields.value(s(u"Disc#")), QStringList({ s(u"1") }));
    QCOMPARE(read.tags.fields.value(s(u"Disc Count")), QStringList({ s(u"3") }));
    QCOMPARE(read.tags.fields.value(s(u"Track Count")), QStringList({ s(u"9") }));
    QVERIFY(!read.tags.other.contains(s(u"TOTALDISCS")));
}

void TagsSpeexTest::otherVorbisKeys()
{
    // W-TABLE, the Vorbis column: Publisher is ORGANIZATION and Grouping CONTENTGROUP.
    const QString path = speexFile();
    TagChanges c;
    c.fields = { { s(u"Publisher"), { s(u"A Label") } }, { s(u"Grouping"), { s(u"A Group") } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    const QByteArray b = bytesOf(path);
    QVERIFY(b.contains("ORGANIZATION=A Label"));
    QVERIFY(!b.contains("LABEL=A Label"));
    QVERIFY(b.contains("CONTENTGROUP=A Group"));
    QVERIFY(!b.contains("GROUPING=A Group"));
}

void TagsSpeexTest::aClearedCount()
{
    // A cleared Disc Count leaves no total behind.
    const QString path = speexFile();
    TagChanges c;
    c.fields = { { s(u"Disc#"), { s(u"2") } }, { s(u"Disc Count"), { s(u"5") } } };
    QCOMPARE(linuxbee::writeTags(path, c).status, WriteResult::Status::Ok);
    TagChanges clear;
    clear.fields = { { s(u"Disc Count"), { } } };
    QCOMPARE(linuxbee::writeTags(path, clear).status, WriteResult::Status::Ok);
    const QByteArray b = bytesOf(path);
    QVERIFY(!b.contains("TOTALDISCS="));
    QVERIFY(b.contains("DISCNUMBER=2"));
    QVERIFY(!b.contains("DISCNUMBER=2/"));
}

QTEST_MAIN(TagsSpeexTest)
#include "tst_tags_speex.moc"
