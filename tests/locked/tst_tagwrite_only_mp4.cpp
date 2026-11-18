// LOCKED ACCEPTANCE TEST (tag writing: a save changes only what it names, MPEG-4). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are the rule W-ONLY in docs/spec/tag-writing.md, its part "In an MPEG-4 file".
//
// How it looks: this test reads the items of the file itself, as the format lays them out (atoms
// of four bytes of size and four of name, one inside another: moov, udta, meta, ilst, and in ilst
// one atom for each item), and compares items as bytes. The tag library is not asked what a file
// holds: it is what is being tested. The fixture only.m4a was saved once by the tag library when
// it was made (tests/data/tags/make_only_m4a.py says why), so an item the tag library leaves alone
// is the same bytes before and after.
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

struct Atom {
    QByteArray name;
    qsizetype start = 0; // of its eight bytes of size and name
    qsizetype body = 0;
    qsizetype end = 0;
};

// BYTES: the atoms that stand one after another from `from` to `to`.
QList<Atom> atomsIn(const QByteArray &d, qsizetype from, qsizetype to)
{
    QList<Atom> out;
    qsizetype p = from;
    while (p + 8 <= to) {
        const qsizetype n = qFromBigEndian<quint32>(d.constData() + p);
        if (n < 8 || p + n > to)
            break;
        out.append(Atom { d.mid(p + 4, 4), p, p + 8, p + n });
        p += n;
    }
    return out;
}

Atom atomNamed(const QByteArray &d, qsizetype from, qsizetype to, const char *name)
{
    for (const Atom &a : atomsIn(d, from, to)) {
        if (a.name == name)
            return a;
    }
    return { };
}

struct Item {
    QString name; // "trkn", or "----:com.apple.iTunes:My Custom Thing" for a freeform item
    QByteArray whole; // the item's atom, as it is in the file
    QList<QByteArray>
        data; // the content of each of its data atoms, after their eight bytes of kind
};

struct Tag {
    bool ok = false;
    QList<Item> items;
};

// BYTES: the items of a file. moov holds udta, udta holds meta, meta (after four bytes of
// version) holds ilst, and ilst holds the items. A freeform item is named "----" and holds a
// "mean" and a "name" atom (each after four bytes of version) beside its data.
Tag tagOf(const QByteArray &d)
{
    Tag t;
    const Atom moov = atomNamed(d, 0, d.size(), "moov");
    const Atom udta = atomNamed(d, moov.body, moov.end, "udta");
    const Atom meta = atomNamed(d, udta.body, udta.end, "meta");
    const Atom ilst = atomNamed(d, meta.body + 4, meta.end, "ilst");
    if (ilst.end == 0)
        return t;
    for (const Atom &a : atomsIn(d, ilst.body, ilst.end)) {
        Item item;
        item.name = QString::fromLatin1(a.name);
        item.whole = d.mid(a.start, a.end - a.start);
        QString mean;
        QString name;
        for (const Atom &part : atomsIn(d, a.body, a.end)) {
            if (part.name == "mean")
                mean = QString::fromUtf8(d.mid(part.body + 4, part.end - part.body - 4));
            else if (part.name == "name")
                name = QString::fromUtf8(d.mid(part.body + 4, part.end - part.body - 4));
            else if (part.name == "data")
                item.data.append(d.mid(part.body + 8, part.end - part.body - 8));
        }
        if (a.name == "----")
            item.name = QStringLiteral("----:") + mean + QLatin1Char(':') + name;
        t.items.append(item);
    }
    t.ok = true;
    return t;
}

// BYTES: the sound is the content of the atom mdat.
QByteArray soundOf(const QByteArray &d)
{
    const Atom mdat = atomNamed(d, 0, d.size(), "mdat");
    return d.mid(mdat.body, mdat.end - mdat.body);
}

// A freeform item of Apple's name space, by the last part of its name.
QString apple(const char16_t *name)
{
    return QStringLiteral("----:com.apple.iTunes:") + s(name);
}

bool among(const QStringList &names, const QString &name)
{
    return names.contains(name, Qt::CaseInsensitive);
}

// The items called by one of `names`, whatever the case of their letters.
QList<Item> called(const Tag &t, const QStringList &names)
{
    QList<Item> out;
    for (const Item &item : t.items) {
        if (among(names, item.name))
            out.append(item);
    }
    return out;
}

QStringList namesOf(const QList<Item> &items)
{
    QStringList out;
    for (const Item &item : items)
        out.append(item.name);
    return out;
}

QList<QByteArray> notCalled(const Tag &t, const QStringList &names)
{
    QList<QByteArray> out;
    for (const Item &item : t.items) {
        if (!among(names, item.name))
            out.append(item.whole);
    }
    return out;
}

QString listed(const Tag &t, const QList<QByteArray> &wholes)
{
    QStringList out;
    for (const QByteArray &w : wholes) {
        QString name = QStringLiteral("?");
        for (const Item &item : t.items) {
            if (item.whole == w)
                name = item.name;
        }
        out.append(name + QLatin1Char('(') + QString::number(w.size()) + QLatin1Char(')'));
    }
    return out.join(QStringLiteral(", "));
}

struct Step {
    WriteResult result;
    QByteArray was;
    QByteArray is;
    Tag before;
    Tag after;
};

// One save, with the file read as bytes before it and after it.
Step step(const QString &path, const TagChanges &changes)
{
    Step st;
    st.was = bytesOf(path);
    st.result = linuxbee::writeTags(path, changes);
    st.is = bytesOf(path);
    st.before = tagOf(st.was);
    st.after = tagOf(st.is);
    return st;
}

// W-ONLY. Empty when the save was made and every item that is not named is, after it, the bytes
// it was, in the order it had among the items not named, and the sound is the bytes it was;
// otherwise what is wrong.
QString whatElse(const Step &st, const QStringList &named)
{
    if (st.result.status != WriteResult::Status::Ok)
        return QStringLiteral("the save was not made: status %1").arg(int(st.result.status));
    if (!st.before.ok || !st.after.ok)
        return QStringLiteral("the items could not be found in the bytes (before: %1, after: %2)")
            .arg(st.before.ok)
            .arg(st.after.ok);
    if (soundOf(st.is) != soundOf(st.was) || soundOf(st.is).isEmpty())
        return QStringLiteral("the sound is not the bytes it was");
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
        return QStringLiteral("items that were not named stand in another order");
    return QStringLiteral(
        "not named, and no longer there as they were: [%1]; not named, and new or different: [%2]")
        .arg(listed(st.before, gone), listed(st.after, rest));
}

#define ONLY(st, named)                                                                            \
    do {                                                                                           \
        const QString somethingElse = whatElse(st, named);                                         \
        QVERIFY2(somethingElse.isEmpty(), qPrintable(somethingElse));                              \
    } while (false)

struct Box {
    QTemporaryDir dir;
    QString copy()
    {
        const QString to = dir.path() + QStringLiteral("/only.m4a");
        QFile::remove(to);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/only.m4a"), to))
            QTest::qFail("could not copy the fixture", __FILE__, __LINE__);
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

// Picture bytes that are not in the fixture.
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

}

class TagWriteOnlyMp4Test : public QObject {
    Q_OBJECT
private slots:
    void theFixtureIsAsThisTestTakesIt();
    void aTitle();
    void aValueUnderANameInSmallLetters();
    void aValueUnderANameInMixedCase();
    void oneKeyUnderTwoNames();
    void aFieldOfTwoValues();
    void aNumber();
    void aFieldTakenOut();
    void aNewKeyAndTheRating();
    void thePictures();
    void aSaveAndTheSaveThatTakesItBack();
    void threeSavesMakeNothingGrow();
};

void TagWriteOnlyMp4Test::theFixtureIsAsThisTestTakesIt()
{
    // Not a rule of the program: what the cases below rest on.
    const QByteArray d
        = bytesOf(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/only.m4a"));
    const Tag t = tagOf(d);
    QVERIFY(t.ok);
    QCOMPARE(int(t.items.size()), 25);
    QVERIFY(soundOf(d).size() > 50);
    QCOMPARE(namesOf(called(t, { apple(u"replaygain_track_gain") })),
        QStringList({ apple(u"replaygain_track_gain") }));
    QCOMPARE(called(t, { apple(u"replaygain_track_gain") }).value(0).data,
        QList<QByteArray>({ "-5.00 dB" }));
    QCOMPARE(namesOf(called(t, { apple(u"My Custom Thing") })),
        QStringList({ apple(u"My Custom Thing") }));
    QCOMPARE(namesOf(called(t, { apple(u"publisher") })),
        QStringList({ apple(u"PUBLISHER"), apple(u"publisher") }));
    QCOMPARE(called(t, { apple(u"MOOD") }).value(0).data, QList<QByteArray>({ "Calm", "Bright" }));
    QCOMPARE(int(called(t, { s(u"----:org.example:foo") }).size()), 1);
    QCOMPARE(int(called(t, { s(u"rtng") }).size()), 1);
    QCOMPARE(called(t, { s(u"covr") }).value(0).data.size(), 2);
    QCOMPARE(called(t, { s(u"\u00a9nam") }).value(0).data, QList<QByteArray>({ "Old Title" }));
}

void TagWriteOnlyMp4Test::aTitle()
{
    // W-ONLY: the finding itself. A title, and no item gets a twin in capitals: there are as many
    // items as there were, and all but the title are the bytes they were.
    Box box;
    const QString path = box.copy();
    const Step st = step(path, field(u"Title", { s(u"New Title") }));
    ONLY(st, QStringList({ s(u"\u00a9nam") }));
    QCOMPARE(st.after.items.size(), st.before.items.size());
    QCOMPARE(called(st.after, { s(u"\u00a9nam") }).value(0).data,
        QList<QByteArray>({ "New Title" })); // BYTES
    QCOMPARE(linuxbee::readTags(path).tags.first(s(u"Title")), s(u"New Title")); // RT
}

void TagWriteOnlyMp4Test::aValueUnderANameInSmallLetters()
{
    // W-ONLY, the audit's F3: the gain another tagger wrote as "replaygain_track_gain". Changing
    // it changes that item, under the name the file gives it; taking it out takes it out.
    Box box;
    const QString path = box.copy();
    const QStringList gain({ apple(u"replaygain_track_gain") });
    Step st = step(path, other(u"REPLAYGAIN_TRACK_GAIN", { s(u"+1.00 dB") }));
    ONLY(st, gain);
    QCOMPARE(namesOf(called(st.after, gain)),
        QStringList({ apple(u"replaygain_track_gain") })); // BYTES: one, spelled as it was
    QCOMPARE(called(st.after, gain).value(0).data, QList<QByteArray>({ "+1.00 dB" }));
    QCOMPARE(linuxbee::readTags(path).tags.other.value(s(u"REPLAYGAIN_TRACK_GAIN")),
        QStringList({ s(u"+1.00 dB") })); // RT
    st = step(path, other(u"REPLAYGAIN_TRACK_GAIN", { }));
    ONLY(st, gain);
    QCOMPARE(int(called(st.after, gain).size()), 0);
    QVERIFY(!linuxbee::readTags(path).tags.other.contains(s(u"REPLAYGAIN_TRACK_GAIN"))); // RT
    // The peak beside it was never named.
    QCOMPARE(called(st.after, { apple(u"replaygain_track_peak") }).value(0).data,
        QList<QByteArray>({ "0.988" }));
}

void TagWriteOnlyMp4Test::aValueUnderANameInMixedCase()
{
    Box box;
    const QString path = box.copy();
    const QStringList custom({ apple(u"My Custom Thing") });
    Step st = step(path, other(u"MY CUSTOM THING", { s(u"changed") }));
    ONLY(st, custom);
    QCOMPARE(namesOf(called(st.after, custom)), QStringList({ apple(u"My Custom Thing") }));
    QCOMPARE(called(st.after, custom).value(0).data, QList<QByteArray>({ "changed" }));
    st = step(path, other(u"MY CUSTOM THING", { }));
    ONLY(st, custom);
    QCOMPARE(int(called(st.after, custom).size()), 0);
}

void TagWriteOnlyMp4Test::oneKeyUnderTwoNames()
{
    // W-ONEHOME: a file that holds the publisher under "publisher" and "PUBLISHER" (a save before
    // this rule made such twins). aTitle shows that a save which does not name it leaves both;
    // naming the field leaves one item, under the name the field is written under.
    Box box;
    const QString path = box.copy();
    const QStringList homes({ apple(u"publisher"), apple(u"LABEL"), apple(u"ORGANIZATION") });
    const Step st = step(path, field(u"Publisher", { s(u"One Label") }));
    ONLY(st, homes);
    QCOMPARE(namesOf(called(st.after, homes)), QStringList({ apple(u"LABEL") }));
    QCOMPARE(called(st.after, homes).value(0).data, QList<QByteArray>({ "One Label" }));
    QCOMPARE(linuxbee::readTags(path).tags.fields.value(s(u"Publisher")),
        QStringList({ s(u"One Label") })); // RT
}

void TagWriteOnlyMp4Test::aFieldOfTwoValues()
{
    // W-MULTI: one item, one data atom a value.
    Box box;
    const QString path = box.copy();
    const QStringList mood({ apple(u"MOOD") });
    const Step st = step(path, field(u"Mood", { s(u"One"), s(u"Two"), s(u"Three") }));
    ONLY(st, mood);
    QCOMPARE(int(called(st.after, mood).size()), 1);
    QCOMPARE(called(st.after, mood).value(0).data, QList<QByteArray>({ "One", "Two", "Three" }));
}

void TagWriteOnlyMp4Test::aNumber()
{
    // W-NUMBERS: the number and its count are one item. BYTES: two zero bytes, the number and the
    // count as two bytes each, two zero bytes.
    Box box;
    const Step st = step(box.copy(), field(u"Track#", { s(u"4") }));
    ONLY(st, QStringList({ s(u"trkn") }));
    QCOMPARE(called(st.after, { s(u"trkn") }).value(0).data,
        QList<QByteArray>({ QByteArray("\0\0\0\x04\0\x0c\0\0", 8) }));
}

void TagWriteOnlyMp4Test::aFieldTakenOut()
{
    Box box;
    const QString path = box.copy();
    const Step st = step(path, field(u"Composer", { }));
    ONLY(st, QStringList({ s(u"\u00a9wrt") }));
    QCOMPARE(int(called(st.after, { s(u"\u00a9wrt") }).size()), 0);
    QCOMPARE(st.after.items.size(), st.before.items.size() - 1);
}

void TagWriteOnlyMp4Test::aNewKeyAndTheRating()
{
    // A key the file does not have is one item more. W-RATING: the star rating is the freeform
    // item RATING; the atom "rtng" is a shop's advisory mark, and is not named by it.
    Box box;
    const QString path = box.copy();
    Step st = step(path, other(u"A NEW KEY", { s(u"its value") }));
    ONLY(st, QStringList({ apple(u"A NEW KEY") }));
    QCOMPARE(called(st.after, { apple(u"A NEW KEY") }).value(0).data,
        QList<QByteArray>({ "its value" }));
    QCOMPARE(st.after.items.size(), st.before.items.size() + 1);
    st = step(path, field(u"Rating", { s(u"3.5") }));
    ONLY(st, QStringList({ apple(u"RATING") }));
    QCOMPARE(called(st.after, { apple(u"RATING") }).value(0).data, QList<QByteArray>({ "70" }));
    QCOMPARE(int(called(st.after, { s(u"rtng") }).size()), 1);
}

void TagWriteOnlyMp4Test::thePictures()
{
    // W-PICTURES: the pictures are the item "covr".
    Box box;
    const QString path = box.copy();
    TagChanges c;
    c.pictures = QList<Picture>({ cover(1) });
    Step st = step(path, c);
    ONLY(st, QStringList({ s(u"covr") }));
    QCOMPARE(called(st.after, { s(u"covr") }).value(0).data,
        QList<QByteArray>({ cover(1).data })); // BYTES
    c.pictures = QList<Picture>();
    st = step(path, c);
    ONLY(st, QStringList({ s(u"covr") }));
    QCOMPARE(int(called(st.after, { s(u"covr") }).size()), 0);
}

void TagWriteOnlyMp4Test::aSaveAndTheSaveThatTakesItBack()
{
    // W-ONLY: what Undo does. Four fields and a freeform value changed, and then given their old
    // values again: every item of the file is the bytes it was at the start, in the same order.
    Box box;
    const QString path = box.copy();
    const QStringList named({ s(u"\u00a9nam"), s(u"\u00a9lyr"), s(u"trkn"), s(u"\u00a9day"),
        apple(u"replaygain_track_gain") });
    TagChanges c;
    c.fields = {
        { s(u"Title"), { s(u"Another Title") } },
        { s(u"Lyrics"), { s(u"other lyrics") } },
        { s(u"Track#"), { s(u"9") } },
        { s(u"Year"), { s(u"1990") } },
    };
    c.other = { { s(u"REPLAYGAIN_TRACK_GAIN"), { s(u"+2.00 dB") } } };
    const Step there = step(path, c);
    ONLY(there, named);
    c.fields = {
        { s(u"Title"), { s(u"Old Title") } },
        { s(u"Lyrics"), { s(u"m4a lyrics") } },
        { s(u"Track#"), { s(u"3") } },
        { s(u"Year"), { s(u"2001-05-12") } },
    };
    c.other = { { s(u"REPLAYGAIN_TRACK_GAIN"), { s(u"-5.00 dB") } } };
    const Step back = step(path, c);
    ONLY(back, named);
    QVERIFY2(notCalled(back.after, { }) == notCalled(there.before, { }),
        "after the second save the items are not, one for one and in their order, the bytes they "
        "were at the start");
}

void TagWriteOnlyMp4Test::threeSavesMakeNothingGrow()
{
    // Three saves of three things: the count of items is what the changes make it, no more.
    Box box;
    const QString path = box.copy();
    QCOMPARE(step(path, field(u"Title", { s(u"One") })).result.status, WriteResult::Status::Ok);
    QCOMPARE(step(path, field(u"Artist", { s(u"Two") })).result.status, WriteResult::Status::Ok);
    const Step st = step(path, field(u"Album", { s(u"Three") }));
    ONLY(st, QStringList({ s(u"\u00a9alb") }));
    QCOMPARE(int(st.after.items.size()), 25);
    QStringList upper;
    for (const Item &item : st.after.items)
        upper.append(item.name.toUpper());
    upper.removeDuplicates();
    QCOMPARE(int(upper.size()), 24); // the one pair of twins the fixture came with, and no other
}

QTEST_MAIN(TagWriteOnlyMp4Test)
#include "tst_tagwrite_only_mp4.moc"
