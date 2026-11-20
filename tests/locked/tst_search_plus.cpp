// LOCKED ACCEPTANCE TEST (search: a `+` on its own joins as OR does). Written by the reviewer,
// not the builder. The builder makes these pass by changing src/, never by editing this file. A
// test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/search.md, S-PARSE and S-CANONICAL. MusicBee's page on searching gives one
// example in which `+` joins two artists as OR does; linuxBee reads `+` so, and only `+` standing
// alone. The same example has an `or` in lower case: linuxBee keeps that a word (S-PARSE says why).
#include <QtTest>

#include "search/search.h"

using linuxbee::SearchQuery;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

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

MapSource by(const QString &artist, const QString &title)
{
    MapSource t;
    t.m_values = { { s(u"Artist"), artist }, { s(u"Title"), title } };
    return t;
}

}

class SearchPlusTest : public QObject {
    Q_OBJECT
private slots:
    void canonical_data();
    void canonical();
    void matches_data();
    void matches();
};

void SearchPlusTest::canonical_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");
    QTest::newRow("joins") << s(u"philter + sadistik") << s(u"\"philter\" OR \"sadistik\"");
    QTest::newRow("as OR binds") << s(u"x a + b y") << s(u"\"x\" \"a\" OR \"b\" \"y\"");
    QTest::newRow("with OR") << s(u"a OR b + c") << s(u"\"a\" OR \"b\" OR \"c\"");
    QTest::newRow("phrases") << s(u"\"machinae supremacy\" + \"we are the ocean\"")
                             << s(u"\"machinae supremacy\" OR \"we are the ocean\"");
    QTest::newRow("fields") << s(u"artist:a + -title:b") << s(u"artist:\"a\" OR -title:\"b\"");
    QTest::newRow("glued is a character") << s(u"a+b") << s(u"\"a+b\"");
    QTest::newRow("glued at the end") << s(u"c++") << s(u"\"c++\"");
    QTest::newRow("quoted is text") << s(u"a \"+\" b") << s(u"\"a\" \"+\" \"b\"");
    QTest::newRow("negated is text") << s(u"a -+") << s(u"\"a\" -\"+\"");
    QTest::newRow("as a field's text") << s(u"title:+") << s(u"title:\"+\"");
    QTest::newRow("nothing before") << s(u"+ a") << s(u"\"a\"");
    QTest::newRow("nothing after") << s(u"a +") << s(u"\"a\"");
    QTest::newRow("two in a row") << s(u"a + + b") << s(u"\"a\" OR \"b\"");
    QTest::newRow("OR then +") << s(u"a OR + b") << s(u"\"a\" OR \"b\"");
    QTest::newRow("alone") << s(u"+") << QString();
    QTest::newRow("or in lower case is a word") << s(u"a or b") << s(u"\"a\" \"or\" \"b\"");
    QTest::newRow("the wiki's example, as read here")
        << s(u"Philter OR Sadistik or \"Machinae Supremacy\" + \"We Are The Ocean\"")
        << s(u"\"philter\" OR \"sadistik\" \"or\" \"machinae supremacy\" OR \"we are the ocean\"");
}

void SearchPlusTest::canonical()
{
    // S-PARSE, S-CANONICAL: `+` is written as OR, and the canonical text parses to the same.
    QFETCH(QString, text);
    QFETCH(QString, expected);
    const SearchQuery query = SearchQuery::parse(text);
    QCOMPARE(query.canonical(), expected);
    QCOMPARE(SearchQuery::parse(query.canonical()).canonical(), expected);
}

void SearchPlusTest::matches_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("artist");
    QTest::addColumn<bool>("expected");
    QTest::newRow("either, the first") << s(u"philter + sadistik") << s(u"Philter") << true;
    QTest::newRow("either, the second") << s(u"philter + sadistik") << s(u"Sadistik") << true;
    QTest::newRow("neither") << s(u"philter + sadistik") << s(u"SoulEye") << false;
    QTest::newRow("without +, both are needed")
        << s(u"philter sadistik") << s(u"Sadistik") << false;
    QTest::newRow("a + b c, c missing")
        << s(u"philter + sadistik souleye") << s(u"Sadistik") << false;
    QTest::newRow("glued + is looked for") << s(u"c++") << s(u"C++ Band") << true;
    QTest::newRow("glued + is not a join") << s(u"a+b") << s(u"a") << false;
}

void SearchPlusTest::matches()
{
    QFETCH(QString, text);
    QFETCH(QString, artist);
    QFETCH(bool, expected);
    QCOMPARE(SearchQuery::parse(text).matches(by(artist, s(u"Some Title"))), expected);
}

QTEST_GUILESS_MAIN(SearchPlusTest)
#include "tst_search_plus.moc"
