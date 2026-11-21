// LOCKED ACCEPTANCE TEST (search: accents are ignored by default, and heeded on request).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/search.md, S-FOLD, S-PARSE, S-MATCH, S-CACHE and S-CANONICAL. The owner's
// ruling of 2026-11-20: a search ignores accents by default; a box in the preferences heeds them.
#include <QtTest>

#include "search/search.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

class MapSource : public TagSource {
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

MapSource track()
{
    MapSource t;
    t.m_values = { { s(u"Artist"), s(u"Beyonc\u00e9") }, { s(u"Title"), s(u"Bjork Song") },
        { s(u"Album"), s(u"Ti\u00ebsto Mix") }, { s(u"Comment"), s(u"\ufb01ne") } };
    return t;
}

SearchOptions heed()
{
    SearchOptions o;
    o.ignoreAccents = false;
    return o;
}

// The answer without a cache, with a fresh one, and with one used twice: all must agree.
bool found(const QString &text, const SearchOptions &options)
{
    const SearchQuery query = SearchQuery::parse(text);
    const bool plain = query.matches(track(), options);
    SearchCache cache;
    const bool fresh = query.matches(track(), options, &cache);
    const bool again = query.matches(track(), options, &cache);
    if (fresh != plain || again != plain)
        qWarning("a cache changed the answer for [%s]", qPrintable(text));
    return fresh == plain && again == plain ? plain : !plain;
}

}

class SearchAccentsTest : public QObject {
    Q_OBJECT
private slots:
    void defaults();
    void fold_data();
    void fold();
    void bothFormsParsed();
    void matches_data();
    void matches();
    void aCacheKnowsItsWay();
    void prepared();
    void equality();
};

void SearchAccentsTest::defaults()
{
    QVERIFY(SearchOptions().ignoreAccents);
    QCOMPARE(SearchOptions().fields, defaultSearchFields());
    QVERIFY(!SearchCache().accentsHeeded);
}

void SearchAccentsTest::fold_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("ignoring");
    QTest::addColumn<QString>("heeding");
    QTest::newRow("acute") << s(u"Beyonc\u00e9") << s(u"beyonce") << s(u"beyonc\u00e9");
    QTest::newRow("decomposed is composed")
        << s(u"Beyonce\u0301") << s(u"beyonce") << s(u"beyonc\u00e9");
    QTest::newRow("upper") << s(u"BJ\u00d6RK") << s(u"bjork") << s(u"bj\u00f6rk");
    QTest::newRow("ligature") << s(u"\ufb01ne") << s(u"fine") << s(u"fine");
    QTest::newRow("fullwidth") << s(u"\uff22\uff25\uff21\uff34") << s(u"beat") << s(u"beat");
    QTest::newRow("unit separator") << s(u"a\u001fB") << s(u"a b") << s(u"a b");
    QTest::newRow("plain") << s(u"Plain Text 1") << s(u"plain text 1") << s(u"plain text 1");
    QTest::newRow("empty") << QString() << QString() << QString();
}

void SearchAccentsTest::fold()
{
    // S-FOLD: ignoring, NFKD with the marks removed, lower case; heeding, NFKC, lower case.
    QFETCH(QString, text);
    QFETCH(QString, ignoring);
    QFETCH(QString, heeding);
    QCOMPARE(foldForSearch(text), ignoring);
    QCOMPARE(foldForSearch(text, true), ignoring);
    QCOMPARE(foldForSearch(text, false), heeding);
}

void SearchAccentsTest::bothFormsParsed()
{
    // S-PARSE: each term's text is folded both ways; a field name ignores accents always.
    const SearchQuery q = SearchQuery::parse(s(u"Beyonc\u00e9 -\u00c1rtist:Ti\u00cbsto"));
    QCOMPARE(q.groups().size(), 2);
    QCOMPARE(q.groups().at(0).at(0).text, s(u"beyonce"));
    QCOMPARE(q.groups().at(0).at(0).textKept, s(u"beyonc\u00e9"));
    QCOMPARE(q.groups().at(1).at(0).field, s(u"artist"));
    QCOMPARE(q.groups().at(1).at(0).text, s(u"tiesto"));
    QCOMPARE(q.groups().at(1).at(0).textKept, s(u"ti\u00ebsto"));
    QVERIFY(q.groups().at(1).at(0).negated);
}

void SearchAccentsTest::matches_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("ignoring");
    QTest::addColumn<bool>("heeding");
    QTest::newRow("plain for accented") << s(u"beyonce") << true << false;
    QTest::newRow("accented for accented") << s(u"beyonc\u00e9") << true << true;
    QTest::newRow("upper accented") << s(u"BEYONC\u00c9") << true << true;
    QTest::newRow("decomposed typed") << s(u"beyonce\u0301") << true << true;
    QTest::newRow("accented for plain") << s(u"bj\u00f6rk") << true << false;
    QTest::newRow("plain for plain") << s(u"bjork") << true << true;
    QTest::newRow("named field") << s(u"artist:beyonce") << true << false;
    QTest::newRow("named field, accented") << s(u"artist:beyonc\u00e9") << true << true;
    QTest::newRow("negated") << s(u"-beyonce") << false << true;
    QTest::newRow("diaeresis") << s(u"tiesto") << true << false;
    QTest::newRow("ligature either way") << s(u"fine") << true << true;
    QTest::newRow("or") << s(u"beyonce OR bjork") << true << true;
    QTest::newRow("phrase") << s(u"\"ti\u00ebsto mix\"") << true << true;
}

void SearchAccentsTest::matches()
{
    QFETCH(QString, text);
    QFETCH(bool, ignoring);
    QFETCH(bool, heeding);
    QCOMPARE(found(text, { }), ignoring);
    QCOMPARE(found(text, heed()), heeding);
}

void SearchAccentsTest::aCacheKnowsItsWay()
{
    // S-CACHE: a cache holds values folded one way; used the other way it is emptied first and
    // folded that way, so the answer is always the one asked for.
    const MapSource t = track();
    SearchCache cache;
    const SearchQuery plain = SearchQuery::parse(s(u"beyonce"));
    QVERIFY(plain.matches(t, { }, &cache));
    QVERIFY(!cache.accentsHeeded);
    qint64 folded = searchValuesFolded();
    QVERIFY(!plain.matches(t, heed(), &cache));
    QVERIFY(cache.accentsHeeded);
    QCOMPARE(searchValuesFolded() - folded, 4); // the four fields the track knows, again
    QVERIFY(cache.joined.contains(s(u"beyonc\u00e9")));
    folded = searchValuesFolded();
    QVERIFY(!plain.matches(t, heed(), &cache));
    QVERIFY(SearchQuery::parse(s(u"artist:beyonc\u00e9")).matches(t, heed(), &cache));
    QCOMPARE(searchValuesFolded(), folded); // held, the right way
    QVERIFY(plain.matches(t, { }, &cache)); // and back
    QVERIFY(!cache.accentsHeeded);
    QVERIFY(!cache.joined.contains(s(u"beyonc\u00e9")));
    QCOMPARE(searchValuesFolded() - folded, 4);
}

void SearchAccentsTest::prepared()
{
    // S-CACHE: prepareSearch folds the way the options say, and a match that way folds nothing.
    const MapSource t = track();
    SearchCache cache;
    prepareSearch(t, heed(), &cache);
    QVERIFY(cache.accentsHeeded);
    QVERIFY(cache.joinedMade);
    qint64 folded = searchValuesFolded();
    QVERIFY(SearchQuery::parse(s(u"ti\u00ebsto")).matches(t, heed(), &cache));
    QCOMPARE(searchValuesFolded(), folded);
    prepareSearch(t, { }, &cache); // the other way: made again
    QVERIFY(!cache.accentsHeeded);
    QCOMPARE(searchValuesFolded() - folded, 4);
    folded = searchValuesFolded();
    prepareSearch(t, { }, &cache); // made already that way: nothing
    QCOMPARE(searchValuesFolded(), folded);
}

void SearchAccentsTest::equality()
{
    // S-CANONICAL: the canonical text is the accent-ignoring form; == tells apart what it cannot.
    QCOMPARE(SearchQuery::parse(s(u"Beyonc\u00e9")).canonical(), s(u"\"beyonce\""));
    QVERIFY(SearchQuery::parse(s(u"beyonce")) != SearchQuery::parse(s(u"beyonc\u00e9")));
    QVERIFY(SearchQuery::parse(s(u"Beyonc\u00e9")) == SearchQuery::parse(s(u"BEYONC\u00c9")));
    QVERIFY(SearchQuery::parse(s(u"beyonce\u0301")) == SearchQuery::parse(s(u"beyonc\u00e9")));
    QVERIFY(SearchQuery::parse(s(u"a  b")) == SearchQuery::parse(s(u"a b")));
    QVERIFY(SearchQuery::parse(s(u"a b")) != SearchQuery::parse(s(u"a OR b")));
    QVERIFY(SearchQuery::parse(s(u"a")) != SearchQuery::parse(s(u"-a")));
    QVERIFY(SearchQuery::parse(s(u"title:a")) != SearchQuery::parse(s(u"a")));
    QVERIFY(SearchQuery() == SearchQuery::parse(QString()));
}

QTEST_GUILESS_MAIN(SearchAccentsTest)
#include "tst_search_accents.moc"
