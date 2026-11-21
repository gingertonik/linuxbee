// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/search.md (S-x) worked by hand.
// W-S is the MusicBee wiki's "Searching" page, which documents only: all
// fields by default, fieldname:searchtext, quoted phrases, and OR.
#include <QtTest>

#include "search/search.h"

using linuxbee::SearchQuery;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

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

MapSource track()
{
    MapSource t;
    t.m_values = {
        { s(u"Title"), s(u"D\u00e9j\u00e0 Vu (Live at the BBC)") },
        { s(u"Artist"), s(u"Beyonc\u00e9; Jay-Z") },
        { s(u"Album"), s(u"B'Day") },
        { s(u"Album Artist"), s(u"Beyonc\u00e9") },
        { s(u"Genre"), s(u"R&B") },
        { s(u"Composer"), QString() },
        { s(u"Year"), s(u"2006") },
        { s(u"Comment"), s(u"from the OR sessions: take 2") },
        { s(u"Filename"), s(u"01 deja vu.flac") },
        { s(u"Rating"), s(u"4.5") }, // not a default search field
        { s(u"Publisher"), s(u"Columbia") }, // a default search field (S-FIELDS, 2026-11-20)
    };
    return t;
}

}

class SearchTest : public QObject {
    Q_OBJECT
private slots:
    void fold_data();
    void fold();
    void defaults();
    void canonical_data();
    void canonical();
    void matches_data();
    void matches();
    void options();
};

void SearchTest::fold_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");
    // S-FOLD
    QTest::newRow("case") << s(u"The DARK Side") << s(u"the dark side");
    QTest::newRow("accents") << s(u"Beyonc\u00e9 D\u00e9j\u00e0 \u00dcber Sigur R\u00f3s")
                             << s(u"beyonce deja uber sigur ros");
    QTest::newRow("decomposed") << s(u"Beyonce\u0301") << s(u"beyonce"); // e + combining acute
    QTest::newRow("compatibility")
        << s(u"\ufb01ve \uff21") << s(u"five a"); // the fi ligature, a full-width A
    QTest::newRow("untouched") << s(u"r&b 2006 b'day \u65e5\u672c")
                               << s(u"r&b 2006 b'day \u65e5\u672c");
    QTest::newRow("spaces-kept") << s(u"  a  b ") << s(u"  a  b ");
    QTest::newRow("empty") << QString() << QString();
}

void SearchTest::fold()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(linuxbee::foldForSearch(text), expected);
}

void SearchTest::defaults()
{
    // S-FIELDS
    QCOMPARE(linuxbee::defaultSearchFields(),
        QStringList({ s(u"Title"), s(u"Artist"), s(u"Album"), s(u"Album Artist"), s(u"Genre"),
            s(u"Composer"), s(u"Year"), s(u"Comment"), s(u"Filename"), s(u"Conductor"),
            s(u"Publisher"), s(u"Grouping"), s(u"Mood"), s(u"Lyricist"), s(u"Copyright"),
            s(u"ISRC"), s(u"Sort Title"), s(u"Sort Artist"), s(u"Sort Album"),
            s(u"Sort Album Artist"), s(u"Sort Composer"), s(u"Original Year"),
            s(u"Original Artist"), s(u"Original Album"), s(u"Subtitle"), s(u"Language"),
            s(u"Initial Key"), s(u"Encoded By"), s(u"Mix Artist") }));
    QCOMPARE(linuxbee::SearchOptions().fields, linuxbee::defaultSearchFields());
}

void SearchTest::canonical_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");
    auto row = [](const char *name, const char16_t *text, const char16_t *expected) {
        QTest::newRow(name) << QString::fromUtf16(text) << QString::fromUtf16(expected);
    };
    // S-PARSE, S-CANONICAL: every term in quotes, folded; terms of a group joined with OR.
    row("empty", u"", u"");
    row("spaces", u"   \t ", u"");
    row("word", u"Beyonc\u00e9", u"\"beyonce\"");
    row("words", u"deja  VU", u"\"deja\" \"vu\"");
    row("phrase", u"\"live at\" bbc", u"\"live at\" \"bbc\"");
    row("phrase-keeps-spaces", u"\" a  b \"", u"\" a  b \"");
    row("phrase-unclosed", u"\"live at the", u"\"live at the\""); // runs to the end
    row("phrase-glued", u"ab\"c d\"ef", u"\"abc def\""); // one token
    row("empty-phrase", u"\"\" x", u"\"x\"");
    row("field", u"artist:jay", u"artist:\"jay\"");
    row("field-case", u"ARTIST:Jay", u"artist:\"jay\"");
    row("field-phrase", u"album:\"b'day\"", u"album:\"b'day\"");
    row("field-quoted-name", u"\"Album Artist\":beyonce", u"\"album artist\":\"beyonce\"");
    row("field-empty-text", u"composer:", u"composer:\"\""); // "has a value"
    row("field-second-colon", u"comment:take:2", u"comment:\"take:2\"");
    row("colon-first", u":x", u"\":x\""); // no name before it: plain text
    row("negated", u"-live", u"-\"live\"");
    row("negated-field", u"-genre:rock", u"-genre:\"rock\"");
    row("negated-phrase", u"-\"at the\"", u"-\"at the\"");
    row("dash-alone", u"a - b", u"\"a\" \"-\" \"b\"");
    row("dash-inside", u"jay-z", u"\"jay-z\"");
    row("or", u"rock OR pop", u"\"rock\" OR \"pop\"");
    row("or-chain", u"a OR b OR c d", u"\"a\" OR \"b\" OR \"c\" \"d\"");
    row("or-binds-tighter", u"x a OR b y", u"\"x\" \"a\" OR \"b\" \"y\"");
    row("or-lower-case", u"rock or pop",
        u"\"rock\" \"or\" \"pop\""); // only upper case is the operator
    row("or-quoted", u"rock \"OR\" pop", u"\"rock\" \"or\" \"pop\"");
    row("or-leading", u"OR rock", u"\"rock\"");
    row("or-trailing", u"rock OR", u"\"rock\"");
    row("or-doubled", u"a OR OR b", u"\"a\" OR \"b\"");
    row("or-fields", u"genre:rock OR -year:2006", u"genre:\"rock\" OR -year:\"2006\"");
}

void SearchTest::canonical()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    const SearchQuery q = SearchQuery::parse(text);
    QCOMPARE(q.canonical(), expected);
    QCOMPARE(q.isEmpty(), expected.isEmpty());
    // S-CANONICAL: the canonical text parses to the same query.
    QCOMPARE(SearchQuery::parse(q.canonical()).canonical(), expected);
}

void SearchTest::matches_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("expected");
    auto row = [](const char *name, const char16_t *text, bool expected) {
        QTest::newRow(name) << QString::fromUtf16(text) << expected;
    };
    // S-MATCH
    row("empty", u"", true);
    row("word", u"vu", true);
    row("word-no", u"zeppelin", false);
    row("part-of-word", u"eyon", true);
    row("accent-typed-plain", u"beyonce deja", true);
    row("accent-typed-accented", u"D\u00c9J\u00c0", true);
    row("all-words-needed", u"deja zeppelin", false);
    row("words-in-different-fields", u"jay 2006 r&b", true);
    row("phrase", u"\"live at the\"", true);
    row("phrase-order-matters", u"\"at live\"", false);
    row("words-any-order", u"at live", true);
    row("phrase-across-fields", u"\"vu beyonce\"",
        false); // a term is looked for in one field at a time
    row("not-default-field", u"4.5", false); // Rating
    row("publisher-is-a-default-field", u"columbia", true);
    row("filename", u"\"01 deja\"", true);
    row("field", u"artist:jay", true);
    row("field-other-value", u"artist:beyonce", true);
    row("field-no", u"album:beyonce", false);
    row("field-any-name", u"publisher:columbia", true); // a named field need not be a default one
    row("field-number", u"rating:4.5", true);
    row("field-quoted-name", u"\"album artist\":beyonce", true);
    row("field-unknown", u"mood:happy", false);
    row("field-has-value", u"genre:", true);
    row("field-blank", u"composer:", false);
    row("field-unknown-has-value", u"mood:", false);
    row("negated", u"-zeppelin", true);
    row("negated-no", u"-live", false);
    row("negated-with-word", u"deja -remix", true);
    row("negated-field", u"-genre:rock", true);
    row("negated-field-no", u"-genre:r&b", false);
    row("negated-blank", u"-composer:", true);
    row("negated-unknown-field", u"-mood:happy", true);
    row("or", u"zeppelin OR beyonce", true);
    row("or-no", u"zeppelin OR floyd", false);
    row("or-and", u"deja zeppelin OR jay", true); // deja AND (zeppelin OR jay)
    row("or-and-no", u"floyd zeppelin OR jay", false);
    row("or-is-a-word-when-quoted", u"\"the or sessions\"", true);
    row("or-lower-is-a-word", u"sessions or", true); // "or" is in the comment
    row("dash-word", u"jay-z", true);
}

void SearchTest::matches()
{
    QFETCH(QString, text);
    QFETCH(bool, expected);
    QCOMPARE(SearchQuery::parse(text).matches(track()), expected);
}

void SearchTest::options()
{
    // S-FIELDS: the caller chooses where plain words are looked for.
    linuxbee::SearchOptions o;
    o.fields = { s(u"Publisher") };
    QVERIFY(SearchQuery::parse(s(u"columbia")).matches(track(), o));
    QVERIFY(!SearchQuery::parse(s(u"beyonce")).matches(track(), o));
    QVERIFY(SearchQuery::parse(s(u"artist:beyonce"))
            .matches(track(), o)); // a named field is not limited
    o.fields = { };
    QVERIFY(!SearchQuery::parse(s(u"beyonce")).matches(track(), o));
    QVERIFY(SearchQuery::parse(s(u"-beyonce")).matches(track(), o));
    QVERIFY(SearchQuery::parse(QString()).matches(track(), o));
    // The parts are open to callers that want to show or reuse them.
    const SearchQuery q = SearchQuery::parse(s(u"a OR -b:c d"));
    QCOMPARE(q.groups().size(), 2);
    QCOMPARE(q.groups().at(0).size(), 2);
    QCOMPARE(q.groups().at(0).at(1).field, s(u"b"));
    QCOMPARE(q.groups().at(0).at(1).text, s(u"c"));
    QVERIFY(q.groups().at(0).at(1).negated);
    QVERIFY(q.groups().at(1).at(0).field.isEmpty());
    // An empty track matches nothing but the empty query and negations.
    MapSource nothing;
    QVERIFY(!SearchQuery::parse(s(u"a")).matches(nothing));
    QVERIFY(SearchQuery::parse(s(u"-a")).matches(nothing));
}

QTEST_APPLESS_MAIN(SearchTest)
#include "tst_search.moc"
