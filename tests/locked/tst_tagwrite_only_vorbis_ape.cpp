// LOCKED ACCEPTANCE TEST (tag writing: a save changes only what it names, Vorbis comments and
// APEv2). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason,
// and left alone.
//
// Expected values are the rule W-ONLY in docs/spec/tag-writing.md, its parts "In a file of
// Vorbis comments" and "In a WavPack file".
//
// How it looks: this test reads the tag of each file itself, as the format lays it out, and
// compares its entries as bytes. The tag library is not asked what a file holds: it is what is
// being tested. The fixtures were saved once by the tag library when they were made
// (tests/data/tags/make_only_vorbis_ape.py says why), so an entry the tag library leaves alone is
// the same bytes before and after.
//   FLAC       "fLaC", then blocks of one byte of kind (its top bit marks the last), three of
//              length, and the content. Kind 4 holds the comments, kind 1 is padding.
//   Ogg, Opus  pages ("OggS", 27 bytes of header, a table of segment lengths, the segments). A
//              segment shorter than 255 ends a packet. The second packet holds the comments,
//              after "\x03vorbis" or "OpusTags".
//   comments   a length and the vendor's name, a count, then for each a length and "NAME=value".
//              Lengths are four bytes, low byte first.
//   WavPack    the APEv2 tag ends the file: "APETAGEX" opens its 32 bytes of footer, which hold
//              the size of the items and the footer at byte 12 and the count at byte 16. An item
//              is the length of its value, four bytes of flags, its name, a zero, the value.
//   BYTES  compared or found in the file's raw bytes
//   RT     a round trip through readTags, beside a BYTES check
#include <algorithm>

#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

using linuxbee::Picture;
using linuxbee::TagChanges;
using linuxbee::TrackTags;
using linuxbee::WriteResult;

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

quint32 low(const QByteArray &d, qsizetype at)
{
    return at + 4 <= d.size() ? qFromLittleEndian<quint32>(d.constData() + at) : 0;
}

struct Entry {
    QString name; // as the file spells it
    QByteArray value;
    QByteArray whole; // the entry as it is in the file
};

struct Tag {
    bool ok = false;
    QList<Entry> entries;
    QList<QByteArray> blocks; // FLAC: the blocks that are neither comments nor padding
    QByteArray sound;
};

// BYTES: Vorbis comments, from `at` in `d`.
bool commentsAt(const QByteArray &d, qsizetype at, Tag &t)
{
    qsizetype p = at + 4 + low(d, at);
    const quint32 count = low(d, p);
    p += 4;
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype n = low(d, p);
        if (p + 4 + n > d.size())
            return false;
        const QByteArray text = d.mid(p + 4, n);
        const qsizetype eq = text.indexOf('=');
        if (eq < 1)
            return false;
        t.entries.append({ QString::fromUtf8(text.left(eq)), text.mid(eq + 1), d.mid(p, 4 + n) });
        p += 4 + n;
    }
    return true;
}

Tag flacTag(const QByteArray &d)
{
    Tag t;
    if (!d.startsWith("fLaC"))
        return t;
    qsizetype p = 4;
    bool last = false;
    bool found = false;
    while (!last && p + 4 <= d.size()) {
        const int kind = uchar(d[p]) & 0x7f;
        last = (uchar(d[p]) & 0x80) != 0;
        const qsizetype n = (uchar(d[p + 1]) << 16) | (uchar(d[p + 2]) << 8) | uchar(d[p + 3]);
        if (kind == 4)
            found = commentsAt(d, p + 4, t);
        else if (kind != 1)
            t.blocks.append(char(kind) + d.mid(p + 4, n));
        p += 4 + n;
    }
    t.sound = d.mid(p);
    t.ok = found && last;
    return t;
}

// BYTES: the packets of an Ogg file, put together from the segments of its pages.
QList<QByteArray> packetsOf(const QByteArray &d)
{
    QList<QByteArray> out;
    QByteArray packet;
    qsizetype p = 0;
    while (p + 27 <= d.size() && d.mid(p, 4) == "OggS") {
        const int segments = uchar(d[p + 26]);
        qsizetype body = p + 27 + segments;
        for (int i = 0; i < segments; ++i) {
            const int n = uchar(d[p + 27 + i]);
            packet.append(d.mid(body, n));
            body += n;
            if (n < 255) {
                out.append(packet);
                packet.clear();
            }
        }
        p = body;
    }
    return out;
}

Tag oggTag(const QByteArray &d)
{
    Tag t;
    const QList<QByteArray> packets = packetsOf(d);
    if (packets.size() < 3)
        return t;
    const QByteArray &comments = packets.at(1);
    if (comments.startsWith("\x03vorbis"))
        t.ok = commentsAt(comments, 7, t);
    else if (comments.startsWith("OpusTags"))
        t.ok = commentsAt(comments, 8, t);
    for (qsizetype i = 2; i < packets.size(); ++i)
        t.sound.append(packets.at(i));
    return t;
}

Tag apeTag(const QByteArray &d)
{
    Tag t;
    const qsizetype footer = d.size() - 32;
    if (footer < 0 || d.mid(footer, 8) != "APETAGEX")
        return t;
    const qsizetype size = low(d, footer + 12);
    const quint32 count = low(d, footer + 16);
    qsizetype p = footer + 32 - size;
    if (p < 0)
        return t;
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype n = low(d, p);
        const qsizetype zero = d.indexOf('\0', p + 8);
        if (zero < 0 || zero + 1 + n > footer)
            return t;
        t.entries.append({ QString::fromUtf8(d.mid(p + 8, zero - p - 8)), d.mid(zero + 1, n),
            d.mid(p, zero + 1 + n - p) });
        p = zero + 1 + n;
    }
    t.sound = d.left(d.indexOf("APETAGEX"));
    t.ok = p == footer;
    return t;
}

Tag tagOf(const QString &fixture, const QByteArray &d)
{
    if (fixture.endsWith(QStringLiteral(".flac")))
        return flacTag(d);
    if (fixture.endsWith(QStringLiteral(".wv")))
        return apeTag(d);
    return oggTag(d);
}

bool among(const QStringList &names, const QString &name)
{
    return names.contains(name, Qt::CaseInsensitive);
}

// The entries called by one of `names`, whatever the case of their letters.
QList<Entry> called(const Tag &t, const QStringList &names)
{
    QList<Entry> out;
    for (const Entry &e : t.entries) {
        if (among(names, e.name))
            out.append(e);
    }
    return out;
}

QStringList namesOf(const QList<Entry> &entries)
{
    QStringList out;
    for (const Entry &e : entries)
        out.append(e.name);
    return out;
}

QList<QByteArray> valuesOf(const QList<Entry> &entries)
{
    QList<QByteArray> out;
    for (const Entry &e : entries)
        out.append(e.value);
    return out;
}

QList<QByteArray> notCalled(const Tag &t, const QStringList &names)
{
    QList<QByteArray> out;
    for (const Entry &e : t.entries) {
        if (!among(names, e.name))
            out.append(e.whole);
    }
    return out;
}

QString listed(const Tag &t, const QList<QByteArray> &wholes)
{
    QStringList out;
    for (const QByteArray &w : wholes) {
        QString name = QStringLiteral("?");
        for (const Entry &e : t.entries) {
            if (e.whole == w)
                name = e.name;
        }
        out.append(name + QLatin1Char('(') + QString::number(w.size()) + QLatin1Char(')'));
    }
    return out.join(QStringLiteral(", "));
}

struct Step {
    WriteResult result;
    Tag before;
    Tag after;
};

// One save, with the file read as bytes before it and after it.
Step step(const QString &path, const TagChanges &changes)
{
    Step st;
    st.before = tagOf(path, bytesOf(path));
    st.result = linuxbee::writeTags(path, changes);
    st.after = tagOf(path, bytesOf(path));
    return st;
}

// W-ONLY. Empty when the save was made and every entry that is not named is, after it, the bytes
// it was, in the order it had among the entries not named, and the sound is the bytes it was,
// and (unless the pictures are what is named) the other blocks of a FLAC file; otherwise what is
// wrong.
QString whatElse(const Step &st, const QStringList &named, bool picturesNamed = false)
{
    if (st.result.status != WriteResult::Status::Ok)
        return QStringLiteral("the save was not made: status %1").arg(int(st.result.status));
    if (!st.before.ok || !st.after.ok)
        return QStringLiteral("the tag could not be found in the bytes (before: %1, after: %2)")
            .arg(st.before.ok)
            .arg(st.after.ok);
    if (st.after.sound != st.before.sound || st.after.sound.isEmpty())
        return QStringLiteral("the sound is not the bytes it was");
    if (!picturesNamed && st.after.blocks != st.before.blocks)
        return QStringLiteral("a block of the file that holds no comments is not the bytes it was");
    const QList<QByteArray> a = notCalled(st.before, named);
    const QList<QByteArray> b = notCalled(st.after, named);
    if (a == b)
        return { };
    QList<QByteArray> gone;
    QList<QByteArray> rest = b;
    for (const QByteArray &w : a) {
        if (!rest.removeOne(w))
            gone.append(w);
    }
    if (gone.isEmpty() && rest.isEmpty())
        return QStringLiteral("entries that were not named stand in another order");
    return QStringLiteral(
        "not named, and no longer there as they were: [%1]; not named, and new or different: [%2]")
        .arg(listed(st.before, gone), listed(st.after, rest));
}

#define ONLY(st, ...)                                                                              \
    do {                                                                                           \
        const QString somethingElse = whatElse(st, __VA_ARGS__);                                   \
        QVERIFY2(somethingElse.isEmpty(), qPrintable(somethingElse));                              \
    } while (false)

struct Box {
    QTemporaryDir dir;
    QString copy(const QString &fixture)
    {
        const QString to = dir.path() + QLatin1Char('/') + fixture;
        QFile::remove(to);
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + fixture, to))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return to;
    }
};

TagChanges field(const char16_t *name, const QStringList &values)
{
    TagChanges c;
    c.fields.insert(s(name), values);
    return c;
}

TagChanges other(const char16_t *key, const QStringList &values)
{
    TagChanges c;
    c.other.insert(s(key), values);
    return c;
}

// Picture bytes that are not in the fixtures.
Picture cover(int seed)
{
    Picture p;
    p.type = QStringLiteral("Front Cover");
    p.mimeType = QStringLiteral("image/jpeg");
    p.data = QByteArray::fromHex("ffd8ffe000104a46494600");
    for (int i = 0; i < 700; ++i)
        p.data.append(char((i * 7 + seed * 13) % 251));
    p.data.append(QByteArray::fromHex("ffd9"));
    p.byteCount = p.data.size();
    return p;
}

bool isWv(const QString &fixture)
{
    return fixture.endsWith(QStringLiteral(".wv"));
}

void everyFixture()
{
    QTest::addColumn<QString>("fixture");
    QTest::newRow("FLAC") << QStringLiteral("only.flac");
    QTest::newRow("Ogg Vorbis") << QStringLiteral("only.ogg");
    QTest::newRow("Opus") << QStringLiteral("only.opus");
    QTest::newRow("WavPack") << QStringLiteral("only.wv");
}

}

class TagWriteOnlyVorbisApeTest : public QObject {
    Q_OBJECT
private slots:
    void theFixturesAreAsThisTestTakesThem();
    void aTitle_data() { everyFixture(); }
    void aTitle();
    void aCustomKey_data() { everyFixture(); }
    void aCustomKey();
    void aGainInSmallLetters();
    void aFieldOfThreeValues_data() { everyFixture(); }
    void aFieldOfThreeValues();
    void aFieldTakenOut_data() { everyFixture(); }
    void aFieldTakenOut();
    void aNewKey_data() { everyFixture(); }
    void aNewKey();
    void thePictures_data() { everyFixture(); }
    void thePictures();
    void aSaveAndTheSaveThatTakesItBack_data() { everyFixture(); }
    void aSaveAndTheSaveThatTakesItBack();
    void threeSavesMakeNothingGrow_data() { everyFixture(); }
    void threeSavesMakeNothingGrow();
};

void TagWriteOnlyVorbisApeTest::theFixturesAreAsThisTestTakesThem()
{
    // Not a rule of the program: what the cases below rest on.
    const QString dir = QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/");
    const Tag flac = flacTag(bytesOf(dir + QStringLiteral("only.flac")));
    QVERIFY(flac.ok);
    QCOMPARE(int(flac.entries.size()), 13);
    QCOMPARE(int(flac.blocks.size()), 2); // what the sound is, and a picture
    QVERIFY(flac.sound.size() > 50);
    for (const char16_t *name : { u"only.ogg", u"only.opus" }) {
        const Tag ogg = oggTag(bytesOf(dir + s(name)));
        QVERIFY(ogg.ok);
        QCOMPARE(int(ogg.entries.size()), 15);
        QCOMPARE(int(called(ogg, { s(u"METADATA_BLOCK_PICTURE") }).size()), 2);
        QCOMPARE(
            valuesOf(called(ogg, { s(u"ARTIST") })), QList<QByteArray>({ "Artist A", "Artist B" }));
        QVERIFY(ogg.sound.size() > 30); // the Opus fixture is thirteen packets of three bytes
    }
    const Tag wv = apeTag(bytesOf(dir + QStringLiteral("only.wv")));
    QVERIFY(wv.ok);
    QCOMPARE(namesOf(wv.entries),
        QStringList({ s(u"Album"), s(u"Artist"), s(u"Bin"), s(u"Composer"), s(u"Cover Art (Front)"),
            s(u"Link"), s(u"mixed Case"), s(u"replaygain_track_gain"), s(u"Title"), s(u"Track"),
            s(u"Year") }));
    QCOMPARE(called(wv, { s(u"Artist") }).value(0).value, QByteArray("Artist A\0Artist B", 17));
    QVERIFY(wv.sound.size() > 50);
}

void TagWriteOnlyVorbisApeTest::aTitle()
{
    // W-ONLY: a title, and every other entry is the bytes it was. In a WavPack file the item
    // keeps the name the file gave it ("Title", not "TITLE").
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const Step st = step(path, field(u"Title", { s(u"New Title") }));
    ONLY(st, QStringList({ s(u"TITLE") }));
    QCOMPARE(st.after.entries.size(), st.before.entries.size());
    QCOMPARE(namesOf(called(st.after, { s(u"TITLE") })),
        QStringList({ isWv(fixture) ? s(u"Title") : s(u"TITLE") })); // BYTES
    QCOMPARE(valuesOf(called(st.after, { s(u"TITLE") })), QList<QByteArray>({ "New Title" }));
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Title")), s(u"New Title")); // RT
}

void TagWriteOnlyVorbisApeTest::aCustomKey()
{
    // A key of the user's own, changed and then taken out.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const char16_t *key = isWv(fixture) ? u"MIXED CASE" : u"MY CUSTOM THING";
    const QStringList named({ s(key) });
    Step st = step(path, other(key, { s(u"changed") }));
    ONLY(st, named);
    QCOMPARE(namesOf(called(st.after, named)),
        QStringList({ isWv(fixture) ? s(u"mixed Case") : s(u"MY CUSTOM THING") })); // BYTES
    QCOMPARE(valuesOf(called(st.after, named)), QList<QByteArray>({ "changed" }));
    QCOMPARE(
        linuxbee::readTags(path).tags.other.value(s(key)), QStringList({ s(u"changed") })); // RT
    st = step(path, other(key, { }));
    ONLY(st, named);
    QCOMPARE(int(called(st.after, named).size()), 0);
    QVERIFY(!linuxbee::readTags(path).tags.other.contains(s(key))); // RT
}

void TagWriteOnlyVorbisApeTest::aGainInSmallLetters()
{
    // W-ONLY: the gain another tagger wrote into a WavPack file as "replaygain_track_gain" is
    // changed under that name, and taken out.
    Box box;
    const QString path = box.copy(QStringLiteral("only.wv"));
    const QStringList gain({ s(u"REPLAYGAIN_TRACK_GAIN") });
    Step st = step(path, other(u"REPLAYGAIN_TRACK_GAIN", { s(u"+1.00 dB") }));
    ONLY(st, gain);
    QCOMPARE(
        namesOf(called(st.after, gain)), QStringList({ s(u"replaygain_track_gain") })); // BYTES
    QCOMPARE(valuesOf(called(st.after, gain)), QList<QByteArray>({ "+1.00 dB" }));
    st = step(path, other(u"REPLAYGAIN_TRACK_GAIN", { }));
    ONLY(st, gain);
    QCOMPARE(int(called(st.after, gain).size()), 0);
}

void TagWriteOnlyVorbisApeTest::aFieldOfThreeValues()
{
    // W-MULTI: a comment for each value; in an APEv2 item, the values with a zero between them.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const Step st = step(path, field(u"Artist", { s(u"One"), s(u"Two"), s(u"Three") }));
    ONLY(st, QStringList({ s(u"ARTIST") }));
    if (isWv(fixture)) {
        QCOMPARE(
            namesOf(called(st.after, { s(u"ARTIST") })), QStringList({ s(u"Artist") })); // BYTES
        QCOMPARE(valuesOf(called(st.after, { s(u"ARTIST") })),
            QList<QByteArray>({ QByteArray("One\0Two\0Three", 13) }));
    } else {
        QCOMPARE(valuesOf(called(st.after, { s(u"ARTIST") })),
            QList<QByteArray>({ "One", "Two", "Three" })); // BYTES
    }
    QCOMPARE(linuxbee::readTags(path).tags.fields.value(s(u"Artist")),
        QStringList({ s(u"One"), s(u"Two"), s(u"Three") })); // RT
}

void TagWriteOnlyVorbisApeTest::aFieldTakenOut()
{
    QFETCH(QString, fixture);
    Box box;
    const Step st = step(box.copy(fixture), field(u"Composer", { }));
    ONLY(st, QStringList({ s(u"COMPOSER") }));
    QCOMPARE(int(called(st.after, { s(u"COMPOSER") }).size()), 0);
    QCOMPARE(st.after.entries.size(), st.before.entries.size() - 1);
}

void TagWriteOnlyVorbisApeTest::aNewKey()
{
    QFETCH(QString, fixture);
    Box box;
    const Step st = step(box.copy(fixture), other(u"A NEW KEY", { s(u"its value") }));
    ONLY(st, QStringList({ s(u"A NEW KEY") }));
    QCOMPARE(valuesOf(called(st.after, { s(u"A NEW KEY") })), QList<QByteArray>({ "its value" }));
    QCOMPARE(st.after.entries.size(), st.before.entries.size() + 1);
}

void TagWriteOnlyVorbisApeTest::thePictures()
{
    // W-PICTURES: the pictures are blocks of their own in a FLAC file, comments named
    // METADATA_BLOCK_PICTURE in Ogg and Opus, and the item "Cover Art (Front)" in a WavPack file,
    // which keeps that name. BYTES: the picture's bytes end the block or the item; in Ogg and
    // Opus the comment holds them in base64, after 42 bytes that say what the picture is.
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const QStringList named({ s(u"METADATA_BLOCK_PICTURE"), s(u"Cover Art (Front)") });
    TagChanges c;
    c.pictures = QList<Picture>({ cover(1) });
    Step st = step(path, c);
    ONLY(st, named, true);
    if (fixture.endsWith(QStringLiteral(".flac"))) {
        QCOMPARE(int(st.after.blocks.size()), 2);
        QCOMPARE(st.after.blocks.at(0), st.before.blocks.at(0));
        QVERIFY(st.after.blocks.at(1).endsWith(cover(1).data));
    } else if (isWv(fixture)) {
        QCOMPARE(namesOf(called(st.after, named)), QStringList({ s(u"Cover Art (Front)") }));
        QVERIFY(called(st.after, named).value(0).value.endsWith(cover(1).data));
    } else {
        QCOMPARE(int(called(st.after, named).size()), 1);
        QVERIFY(
            QByteArray::fromBase64(called(st.after, named).value(0).value).endsWith(cover(1).data));
    }
    QCOMPARE(int(linuxbee::readTags(path).tags.pictures.size()), 1); // RT
    c.pictures = QList<Picture>();
    st = step(path, c);
    ONLY(st, named, true);
    QCOMPARE(int(called(st.after, named).size()), 0);
    if (fixture.endsWith(QStringLiteral(".flac")))
        QCOMPARE(int(st.after.blocks.size()), 1);
    QCOMPARE(int(linuxbee::readTags(path).tags.pictures.size()), 0); // RT
}

void TagWriteOnlyVorbisApeTest::aSaveAndTheSaveThatTakesItBack()
{
    // W-ONLY: what Undo does. Three fields and a key of the user's own changed, and then given
    // their old values again: every entry of the tag is the bytes it was at the start, in the
    // same order. (No key is taken out here: an APEv2 item that is taken out and put back is a
    // new item, and a new item is named in capitals.)
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    const char16_t *key = isWv(fixture) ? u"MIXED CASE" : u"MY CUSTOM THING";
    const QStringList named({ s(u"TITLE"), s(u"ARTIST"), s(u"COMPOSER"), s(key) });
    TagChanges c;
    c.fields = {
        { s(u"Title"), { s(u"Another Title") } },
        { s(u"Artist"), { s(u"Somebody") } },
        { s(u"Composer"), { s(u"Another Composer") } },
    };
    c.other = { { s(key), { s(u"another value") } } };
    const Step there = step(path, c);
    ONLY(there, named);
    c.fields = {
        { s(u"Title"), { s(u"Old Title") } },
        { s(u"Artist"), { s(u"Artist A"), s(u"Artist B") } },
        { s(u"Composer"), { s(u"A Composer") } },
    };
    c.other = { { s(key), { isWv(fixture) ? s(u"mixed") : s(u"custom value") } } };
    const Step back = step(path, c);
    ONLY(back, named);
    QVERIFY2(notCalled(back.after, { }) == notCalled(there.before, { }),
        "after the second save the entries are not, one for one and in their order, the bytes they "
        "were at the start");
}

void TagWriteOnlyVorbisApeTest::threeSavesMakeNothingGrow()
{
    QFETCH(QString, fixture);
    Box box;
    const QString path = box.copy(fixture);
    QCOMPARE(step(path, field(u"Title", { s(u"One") })).result.status, WriteResult::Status::Ok);
    QCOMPARE(step(path, field(u"Artist", { s(u"Two"), s(u"Three") })).result.status,
        WriteResult::Status::Ok);
    const Step st = step(path, field(u"Album", { s(u"Four") }));
    ONLY(st, QStringList({ s(u"ALBUM") }));
    QCOMPARE(st.after.entries.size(),
        tagOf(fixture,
            bytesOf(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + fixture))
            .entries.size());
}

QTEST_MAIN(TagWriteOnlyVorbisApeTest)
#include "tst_tagwrite_only_vorbis_ape.moc"
