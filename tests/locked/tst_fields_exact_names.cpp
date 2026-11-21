// LOCKED ACCEPTANCE TEST (field names: the answer is the same however a name is written). Written
// by the reviewer, not the builder. The builder makes these pass by changing src/, never by
// editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/tag-reading.md R-SOURCE, docs/spec/track-fields.md, docs/spec/library.md
// L-FIELDS: names ignore case. A name written as the lists write it may be found by a quicker
// way than one written otherwise (fix-sr3b); the answer is the same.
#include <QtTest>

#include "fields/trackfields.h"
#include "library/library.h"
#include "tags/tagreader.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// Every spelling of a name but its own: lower, upper, and every other letter changed.
QStringList otherSpellings(const QString &name)
{
    QString swapped = name;
    for (qsizetype i = 0; i < swapped.size(); i += 2)
        swapped[i] = swapped.at(i).isUpper() ? swapped.at(i).toLower() : swapped.at(i).toUpper();
    QStringList out;
    for (const QString &other : { name.toLower(), name.toUpper(), swapped })
        if (other != name && !out.contains(other))
            out.append(other);
    return out;
}

LibraryTrack track()
{
    LibraryTrack t;
    t.id = 7;
    t.path = s(u"/music/Some Artist/01 S\u00f6ng.flac");
    t.tags.fields.insert(s(u"Artist"), { s(u"A"), s(u"B") });
    t.tags.fields.insert(s(u"Title"), { s(u"S\u00f6ng") });
    t.tags.fields.insert(s(u"Year"), { s(u"1999") });
    t.tags.fields.insert(s(u"Track#"), { s(u"1") });
    t.tags.fields.insert(s(u"Comment"), { QString() });
    t.tags.audio.durationMs = 200000;
    t.tags.audio.bitrateKbps = 900;
    t.tags.audio.sampleRateHz = 44100;
    t.tags.audio.channels = 2;
    t.playCount = 3;
    t.skipCount = 1;
    t.sizeBytes = 30000000;
    t.category = s(u"Music");
    t.dateAdded = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5), QTimeZone::UTC);
    return t;
}

}

class FieldsExactNamesTest : public QObject {
    Q_OBJECT
private slots:
    void everySpellingAlike();
    void values();
    void unknown();
};

void FieldsExactNamesTest::everySpellingAlike()
{
    const LibraryTrack t = track();
    const LibraryFieldSource library(t);
    const TrackFieldSource fields(t.tags, t.path);
    const TrackTagSource tagsOnly(t.tags);
    QStringList names = trackFieldNames() + libraryFieldNames();
    QVERIFY(names.size() > 40);
    int checked = 0;
    for (const QString &name : std::as_const(names)) {
        const QStringList others = otherSpellings(name);
        QVERIFY2(!others.isEmpty(), qPrintable(name));
        for (const QString &other : others) {
            if (library.value(other) != library.value(name))
                QFAIL(qPrintable(
                    QStringLiteral("LibraryFieldSource: [%1] and [%2]").arg(name, other)));
            if (fields.value(other) != fields.value(name))
                QFAIL(
                    qPrintable(QStringLiteral("TrackFieldSource: [%1] and [%2]").arg(name, other)));
            if (tagsOnly.value(other) != tagsOnly.value(name))
                QFAIL(qPrintable(QStringLiteral("TrackTagSource: [%1] and [%2]").arg(name, other)));
            ++checked;
        }
        // Every listed name is known to the source that lists it.
        QVERIFY2(library.value(name).has_value(), qPrintable(name));
    }
    for (const QString &name : trackFieldNames())
        QVERIFY2(fields.value(name).has_value(), qPrintable(name));
    for (const QString &name : tagFieldNames())
        QVERIFY2(tagsOnly.value(name).has_value(), qPrintable(name));
    QVERIFY(checked > 100);
}

void FieldsExactNamesTest::values()
{
    // What the names answer, written either way.
    const LibraryTrack t = track();
    const LibraryFieldSource library(t);
    for (const QString &artist : { s(u"Artist"), s(u"ARTIST") })
        QCOMPARE(library.value(artist), std::optional<QString>(s(u"A; B")));
    QCOMPARE(library.value(s(u"Title")), std::optional<QString>(s(u"S\u00f6ng")));
    QCOMPARE(library.value(s(u"Comment")), std::optional<QString>(QString()));
    QCOMPARE(library.value(s(u"Album")), std::optional<QString>(QString())); // known, no value
    QCOMPARE(library.value(s(u"Play Count")), std::optional<QString>(s(u"3")));
    QCOMPARE(library.value(s(u"skip count")), std::optional<QString>(s(u"1")));
    QCOMPARE(library.value(s(u"Category")), std::optional<QString>(s(u"Music")));
    QCOMPARE(library.value(s(u"Date Added")), std::optional<QString>(s(u"2026-01-02T03:04:05")));
    QCOMPARE(library.value(s(u"Filename")), library.value(s(u"filename")));
    QVERIFY(library.value(s(u"Filename")).value_or(QString()).contains(s(u"01 S\u00f6ng")));
    QCOMPARE(library.value(s(u"Path")), library.value(s(u"PATH")));
    QCOMPARE(library.value(s(u"Time")), library.value(s(u"time")));
    const TrackTagSource tagsOnly(t.tags);
    QCOMPARE(tagsOnly.value(s(u"Artist")), std::optional<QString>(s(u"A; B")));
    QCOMPARE(tagsOnly.value(s(u"Filename")), std::nullopt); // not a tag
    QCOMPARE(tagsOnly.value(s(u"Play Count")), std::nullopt);
    const TrackFieldSource fields(t.tags, t.path);
    QCOMPARE(fields.value(s(u"Play Count")), std::nullopt); // the library's, not the file's
}

void FieldsExactNamesTest::unknown()
{
    const LibraryTrack t = track();
    const LibraryFieldSource library(t);
    const TrackFieldSource fields(t.tags, t.path);
    const TrackTagSource tagsOnly(t.tags);
    for (const char16_t *name :
        { u"Nosuch", u"", u"Artist ", u" Artist", u"Play Count ", u"Artists", u"Filenam" }) {
        QCOMPARE(library.value(s(name)), std::nullopt);
        QCOMPARE(fields.value(s(name)), std::nullopt);
        QCOMPARE(tagsOnly.value(s(name)), std::nullopt);
    }
}

QTEST_GUILESS_MAIN(FieldsExactNamesTest)
#include "tst_fields_exact_names.moc"
