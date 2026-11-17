// LOCKED ACCEPTANCE TEST (a save that makes the tag larger or smaller moves the rest of the file,
// and the rest is the same bytes afterwards). Written by the reviewer, not the builder. The builder
// makes these pass by changing src/, never by editing this file. A test believed to be wrong is
// reported, with the reason, and left alone.
//
// Expected values are the rule WS-REST in docs/spec/tag-safe-save.md. Why this test: the writer
// moves the rest of a file itself, a mebibyte at a time, and the other tag tests use files smaller
// than one such step and compare a save with another save of the same build. Here the sound is
// several mebibytes that no two places in it are alike, made by this test, and what it is compared
// with is those bytes as this test made them: nothing the writer did.
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

using namespace linuxbee;

namespace {

constexpr qsizetype kMiB = 1024 * 1024;

QByteArray bytesOf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        QTest::qFail("could not read a file", __FILE__, __LINE__);
    return f.readAll();
}

QByteArray fixtureBytes(const char *name)
{
    return bytesOf(
        QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(name));
}

void put(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        QTest::qFail("could not write a file", __FILE__, __LINE__);
}

// Bytes in which no stretch is like another: a byte moved to the wrong place, or left out, or
// written twice, shows. Never 0xff, so that nothing in them looks like the start of an MPEG frame.
QByteArray noise(qsizetype bytes, quint32 seed)
{
    QByteArray out(bytes, '\0');
    quint32 x = seed;
    for (qsizetype i = 0; i < bytes; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const uchar c = uchar(x >> 11);
        out[i] = char(c == 0xff ? 0x7f : c);
    }
    return out;
}

Picture picture(qsizetype bytes, quint32 seed)
{
    Picture p;
    p.type = QStringLiteral("Front Cover");
    p.mimeType = QStringLiteral("image/jpeg");
    p.data = QByteArray::fromHex("ffd8ffe000104a46494600") + noise(bytes, seed);
    p.byteCount = p.data.size();
    return p;
}

qsizetype atomAt(const QByteArray &d, const char *name, qsizetype from = 0)
{
    qsizetype i = from;
    while (i + 8 <= d.size()) {
        const quint32 n = qFromBigEndian<quint32>(d.constData() + i);
        if (d.mid(i + 4, 4) == name)
            return i;
        if (n < 8)
            break;
        i += n;
    }
    return -1;
}

// What a file is made of for this test: `rest` is the sound, and it is the end of the file.
struct Made {
    QByteArray file;
    QByteArray rest;
};

// basic.mp3, with noise between its frames and its last frame: the tag, then some megabytes.
Made bigMp3(qsizetype sound)
{
    const QByteArray d = fixtureBytes("basic.mp3");
    const uchar *h = reinterpret_cast<const uchar *>(d.constData());
    const qsizetype tag
        = 10 + ((h[6] & 0x7f) << 21 | (h[7] & 0x7f) << 14 | (h[8] & 0x7f) << 7 | (h[9] & 0x7f));
    if (!d.startsWith("ID3") || tag >= d.size())
        QTest::qFail("basic.mp3 is not as this test expects", __FILE__, __LINE__);
    const QByteArray audio = d.mid(tag);
    Made m;
    m.rest = audio + noise(sound, 0x1234567u) + audio;
    m.file = d.left(tag) + m.rest;
    return m;
}

// basic.flac with noise after its frames. (The tag library reads the blocks before the frames
// only.)
Made bigFlac(qsizetype sound)
{
    const QByteArray d = fixtureBytes("basic.flac");
    qsizetype i = 4;
    bool last = false;
    while (!last && i + 4 <= d.size()) {
        last = uchar(d.at(i)) & 0x80;
        i += 4 + (uchar(d.at(i + 1)) << 16 | uchar(d.at(i + 2)) << 8 | uchar(d.at(i + 3)));
    }
    if (!d.startsWith("fLaC") || !last || i >= d.size())
        QTest::qFail("basic.flac is not as this test expects", __FILE__, __LINE__);
    Made m;
    m.rest = d.mid(i) + noise(sound, 0x7654321u);
    m.file = d.left(i) + m.rest;
    return m;
}

// basic.m4a turned round, the tags before the sound (as a file made for streaming has them), and
// the sound made long. The table of where the sound is (stco) is moved with it.
Made bigM4a(qsizetype sound)
{
    const QByteArray d = fixtureBytes("basic.m4a");
    const qsizetype ftyp = atomAt(d, "ftyp"), mdat = atomAt(d, "mdat"), moov = atomAt(d, "moov");
    if (ftyp != 0 || mdat < 0 || moov < mdat)
        QTest::qFail("basic.m4a is not as this test expects", __FILE__, __LINE__);
    const qsizetype ftypSize = qFromBigEndian<quint32>(d.constData());
    const qsizetype mdatSize = qFromBigEndian<quint32>(d.constData() + mdat);
    QByteArray moovBytes = d.mid(moov, qFromBigEndian<quint32>(d.constData() + moov));
    const QByteArray payload = d.mid(mdat + 8, mdatSize - 8) + noise(sound, 0x2468aceu);
    const qsizetype newPayloadAt = ftypSize + moovBytes.size() + 8;
    const qsizetype delta = newPayloadAt - (mdat + 8);
    qsizetype at = 0;
    int tables = 0;
    while ((at = moovBytes.indexOf("stco", at)) >= 0) {
        const quint32 count = qFromBigEndian<quint32>(moovBytes.constData() + at + 8);
        for (quint32 k = 0; k < count; ++k) {
            char *entry = moovBytes.data() + at + 12 + 4 * k;
            qToBigEndian<quint32>(quint32(qFromBigEndian<quint32>(entry) + delta), entry);
        }
        ++tables;
        at += 4;
    }
    if (tables == 0)
        QTest::qFail("basic.m4a has no stco table", __FILE__, __LINE__);
    QByteArray head(8, '\0');
    qToBigEndian<quint32>(quint32(payload.size() + 8), head.data());
    head.replace(4, 4, "mdat");
    Made m;
    m.rest = payload;
    m.file = d.left(ftypSize) + moovBytes + head + payload;
    return m;
}

// Where the sound is in a file as it is now, found by this test's own reading of the format: after
// the ID3v2 tag, after the last metadata block, inside mdat. -1 when the file is not that format
// any more.
qsizetype soundAt(const QString &format, const QByteArray &d)
{
    if (format == QLatin1String("mp3")) {
        if (!d.startsWith("ID3") || d.size() < 10)
            return -1;
        const uchar *h = reinterpret_cast<const uchar *>(d.constData());
        return 10
            + ((h[6] & 0x7f) << 21 | (h[7] & 0x7f) << 14 | (h[8] & 0x7f) << 7 | (h[9] & 0x7f));
    }
    if (format == QLatin1String("flac")) {
        if (!d.startsWith("fLaC"))
            return -1;
        qsizetype i = 4;
        bool last = false;
        while (!last && i + 4 <= d.size()) {
            last = uchar(d.at(i)) & 0x80;
            i += 4 + (uchar(d.at(i + 1)) << 16 | uchar(d.at(i + 2)) << 8 | uchar(d.at(i + 3)));
        }
        return last ? i : -1;
    }
    const qsizetype mdat = atomAt(d, "mdat");
    return mdat < 0 ? -1 : mdat + 8;
}

Made make(const QString &format, qsizetype sound)
{
    if (format == QLatin1String("mp3"))
        return bigMp3(sound);
    if (format == QLatin1String("flac"))
        return bigFlac(sound);
    return bigM4a(sound);
}

}

class SaveRestTest : public QObject {
    Q_OBJECT
private slots:
    void theRestIsTheSameBytes_data();
    void theRestIsTheSameBytes();
};

void SaveRestTest::theRestIsTheSameBytes_data()
{
    QTest::addColumn<QString>("format");
    QTest::addColumn<qsizetype>("sound");
    // Sizes that are not a whole number of the writer's steps, and one that is.
    for (const char *format : { "mp3", "flac", "m4a" }) {
        QTest::addRow("%s, 3.4 MiB", format)
            << QString::fromLatin1(format) << qsizetype(3 * kMiB + 444444);
        QTest::addRow("%s, 2 MiB exactly", format)
            << QString::fromLatin1(format) << qsizetype(2 * kMiB);
    }
}

void SaveRestTest::theRestIsTheSameBytes()
{
    QFETCH(QString, format);
    QFETCH(qsizetype, sound);
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/song.") + format;
    const Made made = make(format, sound);
    put(path, made.file);
    QVERIFY(made.rest.size() > 2 * kMiB);
    const QByteArray restHash = QCryptographicHash::hash(made.rest, QCryptographicHash::Sha256);

    // WS-REST: after every save, the file ends with the sound as it was made, byte for byte, and
    // what comes before it is a tag that reads as what was saved.
    qint64 before = made.file.size();
    int step = 0;
    const auto save = [&](const char *what, const QString &title, const QList<Picture> &pictures,
                          qint64 atLeast, qint64 atMost) {
        ++step;
        TagChanges c;
        c.fields.insert(QStringLiteral("Title"), { title });
        c.pictures = pictures;
        const WriteResult r = writeTags(path, c);
        const QByteArray label = QByteArray::number(step) + ", " + what;
        if (r.status != WriteResult::Status::Ok) {
            QTest::qFail(qPrintable(QLatin1String(label)
                             + QStringLiteral(": not saved, status %1").arg(int(r.status))),
                __FILE__, __LINE__);
            return false;
        }
        const QByteArray now = bytesOf(path);
        const qint64 change = now.size() - before;
        before = now.size();
        if (change < atLeast || change > atMost) {
            // Not a fault of the writer: the save did not move what this step is here to have
            // moved.
            QTest::qFail(qPrintable(QStringLiteral(
                             "%1: the file changed by %2 bytes, and this step wants %3 to %4")
                                 .arg(QLatin1String(label))
                                 .arg(change)
                                 .arg(atLeast)
                                 .arg(atMost)),
                __FILE__, __LINE__);
            return false;
        }
        // The sound: where this test finds it in the file, the bytes it made. After it, nothing, or
        // an ID3v1 tag (128 bytes, which the writer gives an MP3).
        const qsizetype at = soundAt(format, now);
        const qsizetype after = at < 0 ? -1 : now.size() - at - made.rest.size();
        if (at < 0 || after < 0
            || !(after == 0
                || (format == QLatin1String("mp3") && after == 128
                    && now.mid(now.size() - 128, 3) == "TAG"))) {
            QTest::qFail(qPrintable(QStringLiteral("%1: the sound is not where it should be: it "
                                                   "would begin at %2 and leave %3 bytes after it")
                                 .arg(QLatin1String(label))
                                 .arg(at)
                                 .arg(after)),
                __FILE__, __LINE__);
            return false;
        }
        if (QCryptographicHash::hash(
                QByteArrayView(now).sliced(at, made.rest.size()), QCryptographicHash::Sha256)
            != restHash) {
            qsizetype first = 0;
            while (first < made.rest.size() && now.at(at + first) == made.rest.at(first))
                ++first;
            QTest::qFail(
                qPrintable(QStringLiteral(
                    "%1: the sound is not the bytes it was; the first that differs is %2 of %3")
                        .arg(QLatin1String(label))
                        .arg(first)
                        .arg(made.rest.size())),
                __FILE__, __LINE__);
            return false;
        }
        const ReadResult result = readTags(path);
        const QList<Picture> read = result.tags.pictures;
        bool same
            = result.tags.first(QStringLiteral("Title")) == title && read.size() == pictures.size();
        for (qsizetype i = 0; same && i < read.size(); ++i)
            same = read.at(i).data == pictures.at(i).data;
        if (!same) {
            QTest::qFail(qPrintable(QLatin1String(label)
                             + QStringLiteral(": the tag does not read as what was saved")),
                __FILE__, __LINE__);
            return false;
        }
        return true;
    };

    const Picture small = picture(40000, 11), large = picture(3 * kMiB + 12345, 12),
                  larger = picture(3 * kMiB + 12345 + 70000, 13);
    // An MP4 keeps the room a smaller tag leaves (a `free` atom), so its file does not shrink, and
    // a tag that fits that room does not make it grow. The others give the room back.
    const bool keepsRoom = format == QLatin1String("m4a");
    // 1. Past any padding by a little: the whole sound moves back less than one step.
    QVERIFY(save("a small cover", QStringLiteral("One"), { small }, 20000, 60000));
    // 2. By more than a step, and not by a whole number of steps.
    QVERIFY(save(
        "a large cover", QStringLiteral("Two"), { large }, 3 * kMiB - 40000, 3 * kMiB + 80000));
    // 3. A little again, with megabytes of tag before the sound.
    QVERIFY(save("a larger cover", QStringLiteral("Three"), { larger }, -4096, 90000));
    // 4. Much smaller: the sound moves forward by more than a step.
    QVERIFY(save("the small cover again", QStringLiteral("Four"), { small }, -(4 * kMiB),
        keepsRoom ? 0 : -(2 * kMiB)));
    // 5. No cover.
    QVERIFY(save("no cover", QStringLiteral("Five"), { }, -(kMiB), 0));
    // 6. And large once more, on a file that has been through all that.
    QVERIFY(save("a large cover again", QStringLiteral("Six"), { large }, keepsRoom ? 0 : 2 * kMiB,
        4 * kMiB));
}

QTEST_MAIN(SaveRestTest)
#include "tst_tagsafesave_rest.moc"
