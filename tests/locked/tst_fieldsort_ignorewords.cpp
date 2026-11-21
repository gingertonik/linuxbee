// LOCKED ACCEPTANCE TEST (the order of values: ignore words for title, artist and album only).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/track-list.md, T-COMPARE.
//   W-SORT MusicBee wiki, "Sorting/Grouping Preferences": the ignore words are "ignored when
//          sorting by title, artist, or album"
//   T-x    a linuxBee decision in track-list.md (the page names no field more closely)
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

// "The Zen" against "Tiger": -1 as it is written, 1 with "The" dropped ("zen" after "tiger").
int order(const char16_t *field, const SortOptions &options = { })
{
    return sign(compareFieldValues(s(field), s(u"The Zen"), s(u"Tiger"), options));
}

int prepared(const char16_t *field, const SortOptions &options = { })
{
    return sign(comparePrepared(prepareFieldValue(s(field), s(u"The Zen"), options),
        prepareFieldValue(s(field), s(u"Tiger"), options)));
}

}

class FieldSortIgnoreWordsTest : public QObject {
    Q_OBJECT
private slots:
    void theThreeAndTheirKin_data();
    void theThreeAndTheirKin();
    void notTheOthers_data();
    void notTheOthers();
    void otherWords();
};

void FieldSortIgnoreWordsTest::theThreeAndTheirKin_data()
{
    QTest::addColumn<QString>("field");
    // W-SORT: title, artist, album.
    QTest::newRow("Title") << s(u"Title");
    QTest::newRow("Artist") << s(u"Artist");
    QTest::newRow("Album") << s(u"Album");
    // T-COMPARE: the other names of an artist.
    QTest::newRow("Album Artist") << s(u"Album Artist");
    QTest::newRow("Display Artist") << s(u"Display Artist");
    // T-COMPARE: names ignore case, as everywhere.
    QTest::newRow("album artist") << s(u"album artist");
    QTest::newRow("TITLE") << s(u"TITLE");
}

void FieldSortIgnoreWordsTest::theThreeAndTheirKin()
{
    QFETCH(QString, field);
    const char16_t *name = reinterpret_cast<const char16_t *>(field.utf16());
    QCOMPARE(order(name), 1);
    QCOMPARE(prepared(name), 1);
    SortOptions none;
    none.ignoreWords.clear();
    QCOMPARE(order(name, none), -1);
}

void FieldSortIgnoreWordsTest::notTheOthers_data()
{
    QTest::addColumn<QString>("field");
    // T-COMPARE: every other text field sorts as it is written.
    for (const char16_t *name : { u"Composer", u"Conductor", u"Genre", u"Grouping", u"Publisher",
             u"Comment", u"Lyricist", u"Filename", u"Path", u"Sort Title", u"My Own Field" })
        QTest::newRow(qPrintable(s(name))) << s(name);
}

void FieldSortIgnoreWordsTest::notTheOthers()
{
    QFETCH(QString, field);
    const char16_t *name = reinterpret_cast<const char16_t *>(field.utf16());
    QCOMPARE(order(name), -1);
    QCOMPARE(prepared(name), -1);
}

void FieldSortIgnoreWordsTest::otherWords()
{
    SortOptions more;
    more.ignoreWords = { s(u"The"), s(u"A"), s(u"Les") };
    QCOMPARE(sign(compareFieldValues(
                 s(u"Album"), s(u"A Night at the Opera"), s(u"Moving Pictures"), more)),
        1);
    QCOMPARE(sign(compareFieldValues(s(u"Genre"), s(u"A Cappella"), s(u"Blues"), more)), -1);
    QCOMPARE(sign(compareFieldValues(s(u"Composer"), s(u"Les Baxter"), s(u"Mancini"), more)), -1);
    QCOMPARE(
        sign(compareFieldValues(s(u"Artist"), s(u"Les Rita Mitsouko"), s(u"Mancini"), more)), 1);
    // Blank still last, in either kind of field.
    QCOMPARE(sign(compareFieldValues(s(u"Genre"), QString(), s(u"The Zen"))), 1);
    QCOMPARE(sign(compareFieldValues(s(u"Title"), s(u"The"), QString())), -1);
}

QTEST_GUILESS_MAIN(FieldSortIgnoreWordsTest)
#include "tst_fieldsort_ignorewords.moc"
