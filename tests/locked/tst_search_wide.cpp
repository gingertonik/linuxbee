// LOCKED ACCEPTANCE TEST (search: every text field by default, looked for in one joined value).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/search.md, S-FOLD, S-FIELDS, S-MATCH and S-CACHE. The owner's ruling of
// 2026-11-20: a plain word is looked for in every text field but Lyrics, and in Filename; not in
// the seven number and flag fields, since `1` would find every first track.
#include <QtTest>

#include "search/search.h"

using linuxbee::SearchCache;
using linuxbee::SearchOptions;
using linuxbee::SearchQuery;
using linuxbee::searchValuesFolded;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QChar kUnit(0x1F);

// Field names ignore case, as every real source's do.
class MapSource : public linuxbee::TagSource {
public:
    std::optional<QString> value(const QString &fieldName) const override
    {
        for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it)
            if (it.key().compare(fieldName, Qt::CaseInsensitive) == 0)
                return it.value();
        return std::nullopt;
    }
    QMap<QString, QString> m_values;
};

MapSource with(const QMap<QString, QString> &values)
{
    MapSource t;
    t.m_values = values;
    return t;
}

// The answer without a cache, with a fresh one, and with one used twice: all must agree.
bool found(const QString &text, const MapSource &track, const SearchOptions &options = { })
{
    const SearchQuery query = SearchQuery::parse(text);
    const bool plain = query.matches(track, options);
    SearchCache cache;
    const bool fresh = query.matches(track, options, &cache);
    const bool again = query.matches(track, options, &cache);
    if (fresh != plain || again != plain)
        qWarning("a cache changed the answer for [%s]", qPrintable(text));
    return fresh == plain && again == plain ? plain : !plain;
}

}

class SearchWideTest : public QObject {
    Q_OBJECT
private slots:
    void defaults();
    void searched_data();
    void searched();
    void notSearched_data();
    void notSearched();
    void numbersFindNothing();
    void oneFieldAtATime();
    void unitSeparator();
    void joinedValue();
    void anotherList();
    void foldIsTheFullFold();
};

void SearchWideTest::defaults()
{
    // S-FIELDS: the nine of before first, in their order, then the rest.
    QCOMPARE(linuxbee::defaultSearchFields(),
        QStringList({ s(u"Title"), s(u"Artist"), s(u"Album"), s(u"Album Artist"), s(u"Genre"),
            s(u"Composer"), s(u"Year"), s(u"Comment"), s(u"Filename"), s(u"Conductor"),
            s(u"Publisher"), s(u"Grouping"), s(u"Mood"), s(u"Lyricist"), s(u"Copyright"),
            s(u"ISRC"), s(u"Sort Title"), s(u"Sort Artist"), s(u"Sort Album"),
            s(u"Sort Album Artist"), s(u"Sort Composer"), s(u"Original Year"),
            s(u"Original Artist"), s(u"Original Album"), s(u"Subtitle"), s(u"Language"),
            s(u"Initial Key"), s(u"Encoded By"), s(u"Mix Artist") }));
    QCOMPARE(SearchOptions().fields, linuxbee::defaultSearchFields());
}

void SearchWideTest::searched_data()
{
    QTest::addColumn<QString>("field");
    for (const QString &field : linuxbee::defaultSearchFields())
        QTest::newRow(qPrintable(field)) << field;
}

void SearchWideTest::searched()
{
    // S-FIELDS: a plain word is found in each default field, and only there in this track.
    QFETCH(QString, field);
    const MapSource track = with({ { field, s(u"Zqx Wunder") }, { s(u"Track#"), s(u"3") } });
    QVERIFY(found(s(u"zqx"), track));
    QVERIFY(found(s(u"WÜNDER"), track));
    QVERIFY(!found(s(u"zqy"), track));
}

void SearchWideTest::notSearched_data()
{
    QTest::addColumn<QString>("field");
    for (const char16_t *field : { u"Track#", u"Track Count", u"Disc#", u"Disc Count", u"BPM",
             u"Compilation", u"Love", u"Lyrics", u"Rating", u"Play Count" })
        QTest::newRow(qPrintable(s(field))) << s(field);
}

void SearchWideTest::notSearched()
{
    // S-FIELDS: the seven number and flag fields, Lyrics, and what the library counts, are not
    // looked in by a plain word; named, they are.
    QFETCH(QString, field);
    const MapSource track = with({ { field, s(u"Zqx 1") }, { s(u"Title"), s(u"Song") } });
    QVERIFY(!found(s(u"zqx"), track));
    QVERIFY(found(field.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\":zqx").arg(field)
                                                   : field + s(u":zqx"),
        track));
}

void SearchWideTest::numbersFindNothing()
{
    // The reason for leaving them out: `1` does not find every first track of a one-disc album.
    const MapSource first = with({ { s(u"Title"), s(u"Opening") }, { s(u"Track#"), s(u"1") },
        { s(u"Track Count"), s(u"10") }, { s(u"Disc#"), s(u"1") }, { s(u"Disc Count"), s(u"1") },
        { s(u"BPM"), s(u"120") }, { s(u"Compilation"), s(u"1") }, { s(u"Love"), s(u"1") } });
    QVERIFY(!found(s(u"1"), first));
    QVERIFY(found(s(u"track#:1"), first));
    const MapSource year = with({ { s(u"Title"), s(u"Opening") }, { s(u"Year"), s(u"1991") } });
    QVERIFY(found(s(u"1991"), year)); // Year stays
}

void SearchWideTest::oneFieldAtATime()
{
    // S-MATCH: a phrase does not run from one field into the next, joined or not.
    const MapSource track = with({ { s(u"Title"), s(u"Love") }, { s(u"Artist"), s(u"Story") },
        { s(u"Publisher"), s(u"Night") }, { s(u"Sort Artist"), s(u"Story, The") } });
    QVERIFY(!found(s(u"\"love story\""), track));
    QVERIFY(!found(s(u"\"story night\""), track));
    QVERIFY(!found(s(u"\"evest\""), track));
    QVERIFY(found(s(u"love story night"), track)); // words in different fields
    QVERIFY(found(s(u"\"story, the\""), track));
    QVERIFY(!found(
        s(u"\"night story\""), track)); // Publisher and Sort Artist, next in the joined value
}

void SearchWideTest::unitSeparator()
{
    // S-FOLD: U+001F is read as a space, in a value and in what is typed, so no tag and no
    // term can make a phrase run across fields.
    QCOMPARE(linuxbee::foldForSearch(QStringLiteral("A") + kUnit + QStringLiteral("b")), s(u"a b"));
    const MapSource broken
        = with({ { s(u"Title"), QStringLiteral("Love") + kUnit + QStringLiteral("Story") },
            { s(u"Artist"), s(u"Night") } });
    QVERIFY(found(s(u"\"love story\""), broken));
    QVERIFY(found(s(u"title:\"love story\""), broken));
    QVERIFY(!found(QStringLiteral("\"story") + kUnit + QStringLiteral("night\""), broken));
    QVERIFY(!found(s(u"\"story night\""), broken));
    const MapSource two = with({ { s(u"Title"), s(u"x") }, { s(u"Artist"), s(u"y") } });
    QVERIFY(!found(QStringLiteral("x") + kUnit + QStringLiteral("y"), two));
    QVERIFY(!found(QStringLiteral("\"x") + kUnit + QStringLiteral("y\""), two));
    SearchCache cache;
    QVERIFY(!SearchQuery::parse(s(u"zzzz")).matches(broken, { }, &cache));
    QCOMPARE(cache.joined, s(u"love story") + kUnit + s(u"night"));
}

void SearchWideTest::joinedValue()
{
    // S-CACHE: a term with no field is looked for in one value of the track, made once: the
    // folded values of the search fields the track knows, blank ones too, in the order of the
    // list, each from the next by U+001F. Each value is folded once and counted once.
    const MapSource track = with(
        { { s(u"Artist"), s(u"Ünit") }, { s(u"Title"), s(u"Söng") }, { s(u"Composer"), QString() },
            { s(u"Mix Artist"), s(u"DJ Ö") }, { s(u"Track#"), s(u"1") } });
    SearchCache cache;
    QVERIFY(!cache.joinedMade);
    qint64 folded = searchValuesFolded();
    QVERIFY(!SearchQuery::parse(s(u"zzzz")).matches(track, { }, &cache));
    QCOMPARE(searchValuesFolded() - folded, 4); // Title, Artist, Composer, Mix Artist; not Track#
    QVERIFY(cache.joinedMade);
    QCOMPARE(cache.joinedFor, linuxbee::defaultSearchFields());
    QCOMPARE(cache.joined, s(u"song") + kUnit + s(u"unit") + kUnit + kUnit + s(u"dj o"));
    folded = searchValuesFolded();
    QVERIFY(SearchQuery::parse(s(u"song unit")).matches(track, { }, &cache));
    QVERIFY(SearchQuery::parse(s(u"\"dj o\"")).matches(track, { }, &cache));
    QVERIFY(
        SearchQuery::parse(s(u"title:song")).matches(track, { }, &cache)); // the field is held too
    QVERIFY(!SearchQuery::parse(s(u"composer:")).matches(track, { }, &cache));
    QCOMPARE(searchValuesFolded(), folded); // nothing folded again
    QVERIFY(SearchQuery::parse(s(u"track#:1")).matches(track, { }, &cache));
    QCOMPARE(searchValuesFolded(), folded + 1);
    // Without a cache the answer is the same, and every value is folded again.
    folded = searchValuesFolded();
    QVERIFY(!SearchQuery::parse(s(u"zzzz")).matches(track));
    QVERIFY(searchValuesFolded() - folded >= 1);
    // A track that knows no search field: an empty joined value, and no plain word holds.
    const MapSource none = with({ { s(u"Track#"), s(u"1") } });
    SearchCache empty;
    QVERIFY(!SearchQuery::parse(s(u"x")).matches(none, { }, &empty));
    QVERIFY(SearchQuery::parse(s(u"-x")).matches(none, { }, &empty));
    QVERIFY(empty.joinedMade);
    QCOMPARE(empty.joined, QString());
}

void SearchWideTest::anotherList()
{
    // S-CACHE: the joined value is for one list; another list makes it again, from what the
    // cache holds, folding nothing it has.
    const MapSource track = with({ { s(u"Title"), s(u"Song") }, { s(u"Artist"), s(u"Unit") } });
    SearchOptions titleOnly;
    titleOnly.fields = { s(u"Title") };
    SearchCache cache;
    QVERIFY(SearchQuery::parse(s(u"unit")).matches(track, { }, &cache));
    const qint64 folded = searchValuesFolded();
    QVERIFY(!SearchQuery::parse(s(u"unit")).matches(track, titleOnly, &cache));
    QCOMPARE(cache.joinedFor, titleOnly.fields);
    QCOMPARE(cache.joined, s(u"song"));
    QCOMPARE(searchValuesFolded(), folded);
    QVERIFY(SearchQuery::parse(s(u"unit")).matches(track, { }, &cache));
    QCOMPARE(cache.joined, s(u"song") + kUnit + s(u"unit"));
    SearchOptions none;
    none.fields = { };
    QVERIFY(!SearchQuery::parse(s(u"song")).matches(track, none, &cache));
    QCOMPARE(searchValuesFolded(), folded);
}

void SearchWideTest::foldIsTheFullFold()
{
    // S-FOLD, written out here as it stands: U+001F to a space, NFKD, marks removed, lower
    // case. However the fold is made faster, its answer is this one, on every text below.
    const auto full = [](const QString &text) {
        const QString decomposed = QString(text)
                                       .replace(kUnit, QLatin1Char(' '))
                                       .normalized(QString::NormalizationForm_KD);
        QString kept;
        for (const QChar c : decomposed) {
            const QChar::Category category = c.category();
            if (category != QChar::Mark_NonSpacing && category != QChar::Mark_SpacingCombining
                && category != QChar::Mark_Enclosing)
                kept.append(c);
        }
        return kept.toLower();
    };
    QStringList texts;
    QString everyAscii;
    for (char16_t c = 0x20; c < 0x7F; ++c) {
        texts.append(QString(QChar(c)));
        everyAscii.append(QChar(c));
    }
    for (char16_t a = 0x20; a < 0x7F; ++a)
        for (char16_t b = 0x20; b < 0x7F; ++b)
            texts.append(QString(QChar(a)) + QChar(b));
    texts << everyAscii << everyAscii.toUpper()
          << QString(everyAscii).replace(QLatin1Char('a'), kUnit);
    // The values of the search tests, and texts that end or start a run of other characters.
    for (const char16_t *text :
        { u"D\u00e9j\u00e0 Vu (Live at the BBC)", u"Beyonc\u00e9; Jay-Z", u"B'Day", u"R&B",
            u"from the OR sessions: take 2", u"01 deja vu.flac", u"Beyonce\u0301", u"\ufb01ne",
            u"BEYONC\u00c9", u"S\u00f6ng 1 of 2", u"\u00dcnit", u"DJ \u00d6", u"Zqx Wunder",
            u"W\u00dcNDER", u"Story, The", u"Philter OR Sadistik", u"c++",
            u"\u00c1ngel R\u00edo Se\u00f1or Caf\u00e9 \u00dcber Gar\u00e7on M\u00e4dchen",
            u"e\u0301", u"\u0301e", u"a\u0301b\u0302C", u"A\u030a\u212b\u00c5",
            u"\uff22\uff45\uff41\uff54", u"\u00bd and \u2153", u"\u0130stanbul ISTANBUL",
            u"\u03a3\u039f\u03a6\u039f\u03a3 \u03c3\u03bf\u03c6\u03bf\u03c2",
            u"Stra\u00dfe STRASSE", u"\U0001D400\U0001D401 x", u"\U0001F3B5 song \U0001F3B5",
            u"\ud55c\uad6d\uc5b4 Korean", u"\u3071\u309a \u30d1", u"\u00a0nbsp\u00a0", u"tab\there",
            u"\u2126 \u212a K", u"\u1e9b\u0323", u"x\u0323\u0307y", u"\u0649\u0670 \u0627\u064b",
            u"\u0928\u093f\u0930\u094d", u"Love\u001fStory", u"\u001f", u"A\u001f\u00c9" })
        texts.append(s(text));
    int checked = 0;
    for (const QString &text : texts) {
        if (linuxbee::foldForSearch(text) != full(text))
            QFAIL(qPrintable(QStringLiteral("[%1] folds to [%2], not [%3]")
                    .arg(text, linuxbee::foldForSearch(text), full(text))));
        ++checked;
    }
    QVERIFY(checked > 9000);
}

QTEST_GUILESS_MAIN(SearchWideTest)
#include "tst_search_wide.moc"
