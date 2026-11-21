// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/track-list.md (T-KIND, T-COMPARE,
// T-SUMMARY). W-SORT is the MusicBee wiki's "Sorting/Grouping" preferences.
#include <QtTest>

#include "sort/fieldsort.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

int sign(int v)
{
    return v < 0 ? -1 : (v > 0 ? 1 : 0);
}

}

class FieldSortTest : public QObject {
    Q_OBJECT
private slots:
    void kinds();
    void compare_data();
    void compare();
    void ignoreWords();
    void ordering();
    void summary_data();
    void summary();
};

void FieldSortTest::kinds()
{
    // T-KIND
    for (const char16_t *n : { u"Track#", u"Disc#", u"Track Count", u"Disc Count", u"BPM", u"Play Count",
             u"Skip Count", u"Bitrate", u"Sample Rate", u"Channels", u"Rating" })
        QVERIFY2(fieldKind(s(n)) == FieldKind::Number, qPrintable(s(n)));
    QVERIFY(fieldKind(s(u"Time")) == FieldKind::Duration);
    QVERIFY(fieldKind(s(u"Size")) == FieldKind::Size);
    for (const char16_t *n : { u"Title", u"Artist", u"Year", u"Date Added", u"Last Played", u"Custom1", u"", u"Anything" })
        QVERIFY2(fieldKind(s(n)) == FieldKind::Text, qPrintable(s(n)));
    QVERIFY(fieldKind(s(u"track#")) == FieldKind::Number); // names are not case sensitive
    QVERIFY(fieldKind(s(u"TIME")) == FieldKind::Duration);
}

void FieldSortTest::compare_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QString>("a");
    QTest::addColumn<QString>("b");
    QTest::addColumn<int>("expected");
    auto row = [](const char *name, const char16_t *f, const char16_t *a, const char16_t *b, int e) {
        QTest::newRow(name) << s(f) << s(a) << s(b) << e;
    };
    // T-COMPARE, text
    row("plain", u"Title", u"Apple", u"Banana", -1);
    row("case ignored", u"Title", u"apple", u"APPLE", 0);
    row("accents ignored", u"Artist", u"Bj\u00f6rk", u"bjork", 0);
    row("accent sorts with its letter", u"Artist", u"\u00c9dith Piaf", u"Eels", -1);
    row("spaces around ignored", u"Title", u"  Apple ", u"Apple", 0);
    row("prefix first", u"Title", u"Go", u"Gone", -1);
    row("blank after text", u"Title", u"", u"Zebra", 1);
    row("blank after text 2", u"Title", u"   ", u"Zebra", 1);
    row("two blanks tie", u"Title", u"", u"  ", 0);
    row("digits by value", u"Title", u"Track 2", u"Track 10", -1);
    row("digits by value 2", u"Album", u"Now 9", u"Now 100", -1);
    row("leading zeros tie", u"Title", u"Part 007", u"Part 7", 0);
    row("long digit runs", u"Title", u"99999999999999999999998", u"99999999999999999999999", -1);
    row("digits before letters", u"Title", u"10cc", u"Abba", -1);
    row("year as text", u"Year", u"1999", u"2004", -1);
    row("iso dates", u"Date Added", u"2025-12-31T23:59:59", u"2026-01-01T00:00:00", -1);
    // numbers
    row("number", u"Track#", u"2", u"10", -1);
    row("number with zeros", u"Track#", u"02", u"2", 0);
    row("number then text", u"Track#", u"3/12", u"12", -1);
    row("unit after number", u"Bitrate", u"320k", u"1411k", -1);
    row("decimals", u"BPM", u"120.5", u"120.25", 1);
    row("not a number after numbers", u"Track#", u"A1", u"99", 1);
    row("blank after numbers", u"Play Count", u"", u"0", 1);
    row("two non-numbers tie", u"Track#", u"A", u"B", 0);
    row("negative", u"Rating", u"-1", u"0", -1);
    // durations
    row("duration", u"Time", u"9:59", u"10:00", -1);
    row("hours", u"Time", u"59:59", u"1:00:00", -1);
    row("same length", u"Time", u"3:05", u"03:05", 0);
    row("blank duration last", u"Time", u"", u"0:01", 1);
    // sizes
    row("size", u"Size", u"900.0 MB", u"1.2 GB", -1);
    row("size same unit", u"Size", u"5.5 MB", u"10.1 MB", -1);
    row("size kb", u"Size", u"900 KB", u"1.0 MB", -1);
    row("blank size last", u"Size", u"", u"0.1 MB", 1);
}

void FieldSortTest::compare()
{
    QFETCH(QString, field);
    QFETCH(QString, a);
    QFETCH(QString, b);
    QFETCH(int, expected);
    QCOMPARE(sign(compareFieldValues(field, a, b)), expected);
    QCOMPARE(sign(compareFieldValues(field, b, a)), -expected); // and the other way round
    QCOMPARE(compareFieldValues(field, a, a), 0);
}

void FieldSortTest::ignoreWords()
{
    // T-COMPARE (W-SORT: "ignore words" when sorting, "The" by default)
    QCOMPARE(sign(compareFieldValues(s(u"Artist"), s(u"The Beatles"), s(u"Bob Dylan"))), -1);
    QCOMPARE(sign(compareFieldValues(s(u"Artist"), s(u"the beatles"), s(u"Beatles"))), 0);
    QCOMPARE(sign(compareFieldValues(s(u"Artist"), s(u"Them"), s(u"Talking Heads"))), 1); // a word, not a prefix
    QCOMPARE(sign(compareFieldValues(s(u"Artist"), s(u"The The"), s(u"Talking Heads"))), 1); // one word is dropped: "The"
    QCOMPARE(sign(compareFieldValues(s(u"Artist"), s(u"The"), s(u"Talking Heads"))), 1); // nothing left to drop to
    QCOMPARE(sign(compareFieldValues(s(u"Title"), s(u"The End"), s(u"Eleanor Rigby"))), 1); // title, artist and album (W-SORT)
    SortOptions none;
    none.ignoreWords.clear();
    QCOMPARE(sign(compareFieldValues(s(u"Artist"), s(u"The Beatles"), s(u"Bob Dylan"), none)), 1);
    SortOptions more;
    more.ignoreWords = { s(u"The"), s(u"A"), s(u"Les") };
    QCOMPARE(sign(compareFieldValues(s(u"Artist"), s(u"A Perfect Circle"), s(u"Queen"), more)), -1);
    QCOMPARE(sign(compareFieldValues(s(u"Artist"), s(u"LES Rita Mitsouko"), s(u"Queen"), more)), 1);
    QCOMPARE(sign(compareFieldValues(s(u"Artist"), s(u"Abba"), s(u"Queen"), more)), -1);
    // Never for a number.
    QCOMPARE(sign(compareFieldValues(s(u"Track#"), s(u"The 2"), s(u"1"))), 1);
}

void FieldSortTest::ordering()
{
    // T-COMPARE is an ordering: sorting any shuffle gives the same list.
    const QStringList expected = { s(u"10cc"), s(u"Abba"), s(u"The Beatles"), s(u"Bj\u00f6rk"), s(u"Bob Dylan"),
        s(u"Them"), s(u"The Who"), s(u"") };
    QStringList work = { s(u"The Who"), s(u""), s(u"Bob Dylan"), s(u"10cc"), s(u"Them"), s(u"Bj\u00f6rk"),
        s(u"The Beatles"), s(u"Abba") };
    for (int round = 0; round < 3; ++round) {
        std::rotate(work.begin(), work.begin() + 3, work.end());
        QStringList sorted = work;
        std::stable_sort(sorted.begin(), sorted.end(),
            [](const QString &a, const QString &b) { return compareFieldValues(QStringLiteral("Artist"), a, b) < 0; });
        QCOMPARE(sorted, expected);
    }
}

void FieldSortTest::summary_data()
{
    QTest::addColumn<int>("count");
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<qint64>("ms");
    QTest::addColumn<QString>("expected");
    auto row = [](const char *name, int c, qint64 b, qint64 ms, const char16_t *e) {
        QTest::newRow(name) << c << b << ms << s(e);
    };
    // T-SUMMARY
    row("nothing", 0, 0, 0, u"0 tracks, 0.0 MB, 0:00:00");
    row("one", 1, 5767168, 245000, u"1 track, 5.5 MB, 0:04:05");
    row("two", 2, 10485760, 3599999, u"2 tracks, 10.0 MB, 0:59:59");
    row("thousands", 1234, Q_INT64_C(6012954214), Q_INT64_C(273906000), u"1,234 tracks, 5.6 GB, 3d 04:05:06");
    row("just under a GB", 300, Q_INT64_C(1073741823), 36000000, u"300 tracks, 1024.0 MB, 10:00:00");
    row("a GB", 300, Q_INT64_C(1073741824), 86399000, u"300 tracks, 1.0 GB, 23:59:59");
    row("a day", 1000000, Q_INT64_C(1099511627776), 86400000, u"1,000,000 tracks, 1024.0 GB, 1d 00:00:00");
    row("negatives are nothing", -3, -1, -1, u"0 tracks, 0.0 MB, 0:00:00");
}

void FieldSortTest::summary()
{
    QFETCH(int, count);
    QFETCH(qint64, bytes);
    QFETCH(qint64, ms);
    QFETCH(QString, expected);
    QCOMPARE(formatTrackSummary(count, bytes, ms), expected);
}

QTEST_APPLESS_MAIN(FieldSortTest)
#include "tst_fieldsort.moc"
