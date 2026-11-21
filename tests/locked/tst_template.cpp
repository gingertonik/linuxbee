// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value below comes from outside the implementation. The tag
// beside each row says where:
//   W-F  MusicBee wiki, "Functions"          https://musicbee.fandom.com/wiki/Functions
//   W-T  MusicBee wiki, "Templates"          https://musicbee.fandom.com/wiki/Templates
//   W-O  MusicBee wiki, "File Organization"  https://musicbee.fandom.com/wiki/File_Organization
//   F-C  MusicBee forum, functions cheatsheet, post #2
//        https://getmusicbee.com/forum/index.php?topic=35868.0
//   T-x  a linuxBee decision in docs/spec/templates.md (the public docs are silent)
//   MATH ordinary arithmetic or the standard Roman numeral system
#include <QtTest>

#include "template/template.h"

using linuxbee::Template;
using linuxbee::TemplateOptions;

namespace {

class MapTagSource : public linuxbee::TagSource
{
public:
    std::optional<QString> value(const QString &fieldName) const override
    {
        const auto it = m_values.constFind(fieldName);
        if (it == m_values.constEnd())
            return std::nullopt;
        return *it;
    }
    QHash<QString, QString> m_values;
};

MapTagSource track()
{
    MapTagSource t;
    t.m_values = {
        {QStringLiteral("Artist"), QStringLiteral("The Beatles")},
        {QStringLiteral("Album Artist"), QStringLiteral("Deafheaven")},
        {QStringLiteral("Title"), QStringLiteral("One Two Three")},
        {QStringLiteral("Feat Title"), QStringLiteral("Song feat. X")},
        {QStringLiteral("Album"), QString()},
        {QStringLiteral("Track#"), QStringLiteral("1")},
        {QStringLiteral("Rating"), QStringLiteral("4")},
        {QStringLiteral("Genre"), QStringLiteral("Jazz-rock")},
        {QStringLiteral("Style"), QStringLiteral("R&B")},
        {QStringLiteral("Composer"), QStringLiteral("Lennon; McCartney")},
        {QStringLiteral("Year"), QStringLiteral("2021-12-31")},
        {QStringLiteral("Year2"), QStringLiteral("31/12/2021")},
        {QStringLiteral("Date Modified"), QStringLiteral("2021-12-31T19:30:00")},
        {QStringLiteral("Kanji"), QStringLiteral("\u65e5\u672c\u8a9e")},
        {QStringLiteral("Odd"), QStringLiteral("$Lower(<Artist>)")},
    };
    return t;
}

void addColumns()
{
    QTest::addColumn<QString>("tmpl");
    QTest::addColumn<QString>("expected");
}

void row(const char *name, const QString &tmpl, const QString &expected)
{
    QTest::newRow(name) << tmpl << expected;
}

void checkRow()
{
    QFETCH(QString, tmpl);
    QFETCH(QString, expected);
    const Template t = Template::parse(tmpl);
    QVERIFY2(t.isValid(), qPrintable(QStringLiteral("parse error at %1: %2")
                                         .arg(t.error().position)
                                         .arg(t.error().message)));
    QCOMPARE(t.evaluate(track()), expected);
}

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

}

class TemplateTest : public QObject
{
    Q_OBJECT
private slots:
    void literals_data();
    void literals() { checkRow(); }
    void fields_data();
    void fields() { checkRow(); }
    void blankFields();
    void strings_data();
    void strings() { checkRow(); }
    void conditionals_data();
    void conditionals() { checkRow(); }
    void replaceAndSplit_data();
    void replaceAndSplit() { checkRow(); }
    void arithmetic_data();
    void arithmetic() { checkRow(); }
    void sortingAndGrouping_data();
    void sortingAndGrouping() { checkRow(); }
    void dates_data();
    void dates() { checkRow(); }
    void parseErrors_data();
    void parseErrors();
    void invalidTemplateEvaluatesEmpty();
    void referencedFields();
    void hostileInput();
};

void TemplateTest::literals_data()
{
    addColumns();
    row("plain", s(u"plain text"), s(u"plain text"));
    row("empty", QString(), QString());
    row("lone-dollar", s(u"50% $ off"), s(u"50% $ off"));                 // T-LEX
    row("dollar-digit", s(u"cost $5"), s(u"cost $5"));                    // T-LEX
    row("top-level-quotes", s(uR"~(a "quoted" b)~"), s(uR"~(a "quoted" b)~")); // T-QUOTE
    row("lone-angle", s(u"a < b"), s(u"a < b"));                          // T-LEX
    row("heart", s(u"<3"), s(u"<3"));                                     // T-LEX
}

void TemplateTest::fields_data()
{
    addColumns();
    row("one", s(u"<Artist>"), s(u"The Beatles"));
    row("two", s(u"<Artist> - <Title>"), s(u"The Beatles - One Two Three"));
    row("name-with-space", s(u"<Album Artist>"), s(u"Deafheaven"));
    row("name-with-hash", s(u"<Track#>"), s(u"1"));
    // W-T: the backslash is plain text to the template engine
    row("backslash", s(uR"~(<Album Artist>\<Title>)~"), s(uR"~(Deafheaven\One Two Three)~"));
    row("unknown-field", s(u"<Nope>"), s(u"<Nope>"));                     // T-UNKNOWN
    row("blank-is-empty", s(u"[<Album>]"), s(u"[]"));                     // T-BLANK
    // T-UNKNOWN: a field's value is data. It is never read as template text.
    row("value-not-reparsed", s(u"<Odd>"), s(u"$Lower(<Artist>)"));
    row("value-not-reparsed-in-arg", s(u"$Upper(<Odd>)"), s(u"$LOWER(<ARTIST>)"));
}

void TemplateTest::blankFields()
{
    TemplateOptions unknown;
    unknown.unknownForBlank = true;
    // W-O: "replace missing tags with the value 'Unknown X' where X is the tag name"
    QCOMPARE(Template::parse(s(u"<Album>")).evaluate(track(), unknown), s(u"Unknown Album"));
    QCOMPARE(Template::parse(s(u"<Artist>")).evaluate(track(), unknown), s(u"The Beatles"));
    // W-T tip: $IsNull(<Tag>,,<Tag>) keeps "Unknown" out of the output
    QCOMPARE(Template::parse(s(u"$IsNull(<Album>,,<Album>)")).evaluate(track(), unknown), QString());
    // T-BLANK: a field inside a function argument is never replaced
    QCOMPARE(Template::parse(s(u"$Upper(<Album>)")).evaluate(track(), unknown), QString());
}

void TemplateTest::strings_data()
{
    addColumns();
    row("upper", s(u"$Upper(<Artist>)"), s(u"THE BEATLES"));              // W-F
    row("lower", s(u"$Lower(<Artist>)"), s(u"the beatles"));              // W-F
    row("trim", s(uR"~($Trim("  x  "))~"), s(u"x"));                      // W-F
    row("left", s(u"$Left(<Artist>,3)"), s(u"The"));                      // W-F
    row("right", s(u"$Right(<Artist>,7)"), s(u"Beatles"));                // W-F
    row("left-longer-than-text", s(u"$Left(<Artist>,99)"), s(u"The Beatles")); // T-RANGE
    row("left-non-latin", s(u"$Left(<Kanji>,2)"), s(u"\u65e5\u672c"));    // W-F
    row("cutleft", s(u"$CutLeft(<Artist>,4)"), s(u"Beatles"));            // F-C
    row("cutright", s(u"$CutRight(<Artist>,8)"), s(u"The"));              // F-C
    row("len", s(u"$Len(<Artist>)"), s(u"11"));                           // F-C
    row("pad", s(u"$Pad(<Track#>,3)"), s(u"001"));                        // F-C
    row("pad-already-long", s(u"$Pad(1234,3)"), s(u"1234"));              // T-RANGE
    row("unquoted-arg-trimmed", s(u"$Upper( ab c )"), s(u"AB C"));        // T-QUOTE
    row("quoted-arg-verbatim", s(uR"~($Upper(" ab "))~"), s(u" AB "));    // W-F
    row("quoted-comma", s(uR"~($Upper("a,b"))~"), s(u"A,B"));             // W-F
    row("quoted-paren", s(uR"~($Upper("a)b"))~"), s(u"A)B"));             // W-F
    row("bare-parens-balance", s(u"$Upper(a (b) c)"), s(u"A (B) C"));     // T-QUOTE
    row("name-case", s(u"$upper(<Artist>)"), s(u"THE BEATLES"));          // T-NAMES
    // F-C: pieces written side by side in one argument are joined
    row("concatenation", s(uR"~($Upper(<Artist>" - "<Title>))~"), s(u"THE BEATLES - ONE TWO THREE"));
    row("nested", s(u"$Upper($Left(<Artist>,3))"), s(u"THE"));            // W-F
    row("text-around", s(u"[$Left(<Artist>,3)]"), s(u"[The]"));
    row("first", s(u"$First(<Composer>)"), s(u"Lennon"));                 // W-F
    row("roman-3", s(u"$RomanNumerals(3)"), s(u"III"));                   // F-C
    row("roman-4", s(u"$RomanNumerals(4)"), s(u"IV"));                    // MATH
    row("roman-1994", s(u"$RomanNumerals(1994)"), s(u"MCMXCIV"));         // MATH
    row("roman-out-of-range", s(u"$RomanNumerals(0)"), s(u"0"));          // T-RANGE
    row("pad-bad-count", s(u"$Pad(<Track#>,x)"), s(u"1"));                // T-RANGE
}

void TemplateTest::conditionals_data()
{
    addColumns();
    row("isnull-blank", s(uR"~($IsNull(<Album>,"",<Album>))~"), QString());        // F-C
    row("isnull-empty-arg", s(u"$IsNull(<Album>,,<Album>)"), QString());           // W-T
    row("isnull-true", s(u"$IsNull(<Album>,none,<Album>)"), s(u"none"));           // W-F
    row("isnull-false", s(u"$IsNull(<Artist>,none,<Artist>)"), s(u"The Beatles")); // W-F
    row("if-eq-text", s(u"$If(<Artist>=The Beatles,yes,no)"), s(u"yes"));          // W-F
    row("if-eq-quoted", s(uR"~($If(<Artist>="The Beatles",yes,no))~"), s(u"yes")); // W-F
    row("if-eq-fields", s(u"$If(<Artist>=<Title>,yes,no)"), s(u"no"));             // W-F
    row("if-eq-case", s(u"$If(<Artist>=the beatles,yes,no)"), s(u"yes"));          // T-COMPARE
    row("if-gt", s(u"$If(<Rating>>3,high,low)"), s(u"high"));                      // W-F
    row("if-lt", s(u"$If(<Rating><3,high,low)"), s(u"low"));                       // W-F
    row("if-lt-two-digits", s(u"$If(<Track#><10,a,b)"), s(u"a"));                  // W-F, numeric
    row("if-numeric-eq", s(u"$If(<Rating>=4.0,yes,no)"), s(u"yes"));               // T-COMPARE
    row("if-empty-false-branch", s(u"$If(<Rating>=5,five,)"), QString());          // W-F
    row("contains-true", s(u"$Contains(<Genre>,rock)"), s(u"T"));                  // F-C
    row("contains-false", s(u"$Contains(<Genre>,metal)"), s(u"F"));                // W-F
    row("contains-case", s(u"$Contains(<Title>,one)"), s(u"T"));                   // T-CASE
    row("if-contains-quoted-T", s(uR"~($If($Contains(<Genre>,rock)="T",yes,no))~"), s(u"yes")); // W-F
    row("if-contains-bare-T", s(u"$If($Contains(<Genre>,rock)=T,yes,no)"), s(u"yes"));          // F-C
    row("if-contains-no-T", s(u"$If($Contains(<Genre>,rock),yes,no)"), s(u"yes"));              // F-C
    row("and", s(u"$If($And($Contains(<Genre>,rock),$Contains(<Genre>,metal)),yes,no)"), s(u"no")); // W-F
    row("or", s(u"$If($Or($Contains(<Genre>,rock)=T,$Contains(<Genre>,metal)=T),yes,no)"), s(u"yes")); // F-C
    row("not", s(u"$Not(<Artist>=<Title>)"), s(u"T"));                             // F-C
    row("if-not", s(u"$If($Not(<Artist>=<Title>),diff,same)"), s(u"diff"));        // F-C
    row("ismatch-true", s(uR"~($IsMatch(<Artist>,"^The\s"))~"), s(u"T"));          // F-C
    row("ismatch-false", s(uR"~($IsMatch(<Title>,"^The\s"))~"), s(u"F"));          // F-C
    row("nested-if", s(u"$If(<Rating>>4,top,$If(<Rating>>3,good,poor))"), s(u"good")); // W-F
}

void TemplateTest::replaceAndSplit_data()
{
    addColumns();
    row("replace", s(u"$Replace(<Style>,&,%26)"), s(u"R%26B"));                    // W-T
    row("rxreplace", s(uR"~($RxReplace(<Feat Title>,"feat[\W\.]",ft.))~"), s(u"Song ft. X")); // F-C
    row("rxreplace-groups", s(uR"~($RxReplace(<Year>,"(\d+)-(\d+)-(\d+)","$3/$2/$1"))~"), s(u"31/12/2021")); // T-REGEX
    row("rxmatch-iso", s(uR"~($RxMatch(<Year>,"\d{4}"))~"), s(u"2021"));           // F-C
    row("rxmatch-dmy", s(uR"~($RxMatch(<Year2>,"\d{4}"))~"), s(u"2021"));          // F-C
    row("rxmatch-none", s(uR"~($RxMatch(<Artist>,"\d{4}"))~"), QString());         // T-RANGE
    row("split", s(uR"~($Split(<Title>," ",2))~"), s(u"Two"));                     // W-F
    row("rsplit", s(uR"~($RSplit(<Title>," ",1))~"), s(u"Three"));                 // W-F
    row("rxsplit", s(uR"~($RxSplit(<Title>,"\s",3))~"), s(u"Three"));              // W-F
    row("replace-empty-search", s(uR"~($Replace(<Style>,"",x))~"), s(u"R&B"));     // T-RANGE
    row("equals-is-text-here", s(u"$Replace(a=b,=,-)"), s(u"a-b"));                // T-COMPARE
    row("split-out-of-range", s(uR"~($Split(<Title>," ",9))~"), QString());        // T-RANGE
}

void TemplateTest::arithmetic_data()
{
    addColumns();
    row("add", s(u"$Add(2,3)"), s(u"5"));                                 // MATH
    row("sub", s(u"$Sub(5,3)"), s(u"2"));                                 // MATH
    row("mul", s(u"$Mul(4,3)"), s(u"12"));                                // MATH
    row("div", s(u"$Div(6,3)"), s(u"2"));                                 // MATH
    row("mod", s(u"$Mod(7,3)"), s(u"1"));                                 // MATH
    row("abs", s(u"$Abs(-3)"), s(u"3"));                                  // F-C
    row("add-fields", s(u"$Add(<Rating>,<Track#>)"), s(u"5"));            // MATH
    row("div-fraction", s(u"$Div(7,2)"), s(u"3.5"));                      // MATH, T-MATH
    row("div-by-zero", s(u"$Div(6,0)"), QString());                       // T-MATH
    row("add-non-number", s(u"$Add(<Artist>,1)"), QString());             // T-MATH
}

void TemplateTest::sortingAndGrouping_data()
{
    addColumns();
    row("sort", s(u"$Sort(<Artist>)"), s(u"Beatles, The"));               // F-C
    row("sort-no-ignore-word", s(u"$Sort(<Title>)"), s(u"One Two Three")); // W-F
    row("removesortwords", s(u"$RemoveSortWords(<Artist>)"), s(u"Beatles")); // F-C
    row("group-3", s(u"$Group(<Album Artist>,3)"), s(u"DEA"));            // W-F
    row("group-4", s(u"$Group(<Album Artist>,4)"), s(u"DEAF"));           // W-F
    row("group-non-letter", s(u"$Group(1999,3)"), s(u"#"));               // T-GROUP
}

void TemplateTest::dates_data()
{
    addColumns();
    row("date-24h", s(u"$Date(<Date Modified>,yyyy/MM/dd HH:mm)"), s(u"2021/12/31 19:30")); // F-C
    row("date-short", s(u"$Date(<Date Modified>,d/M/yy)"), s(u"31/12/21"));                 // W-F
    row("date-quoted-format", s(uR"~($Date(<Date Modified>,"yyyy-MM-dd, HH:mm:ss"))~"),
        s(u"2021-12-31, 19:30:00"));                                                       // W-F
    row("date-only", s(u"$Date(<Year>,yyyy)"), s(u"2021"));                                 // W-F
    row("date-year-only", s(u"$Date(1999,yy)"), s(u"99"));                                  // T-DATE
    row("date-unparseable", s(u"$Date(<Artist>,yyyy)"), QString());                         // T-DATE
}

void TemplateTest::parseErrors_data()
{
    QTest::addColumn<QString>("tmpl");
    QTest::addColumn<int>("position");
    // T-ERRORS: position is the index of the '$' that starts the faulty call
    QTest::newRow("unclosed-call") << s(u"$Upper(<Artist>") << 0;
    QTest::newRow("unknown-function") << s(u"ab $Nope(x)") << 3;
    QTest::newRow("too-few-args") << s(u"$Left(<Artist>)") << 0;
    QTest::newRow("too-many-args") << s(u"x$Upper(a,b)") << 1;
    QTest::newRow("unclosed-quote") << s(uR"~($Upper("abc))~") << 0;
    QTest::newRow("inner-call") << s(u"$Upper($Left(<Artist>))") << 7;
}

void TemplateTest::parseErrors()
{
    QFETCH(QString, tmpl);
    QFETCH(int, position);
    const Template t = Template::parse(tmpl);
    QVERIFY(!t.isValid());
    QCOMPARE(t.error().position, position);
    QVERIFY(!t.error().message.isEmpty());
}

void TemplateTest::invalidTemplateEvaluatesEmpty()
{
    QCOMPARE(Template::parse(s(u"abc $Upper(")).evaluate(track()), QString());
}

void TemplateTest::referencedFields()
{
    const Template t = Template::parse(s(u"$If(<Rating>>3,<Artist>,<Title>) <Artist> <Nope>"));
    QVERIFY(t.isValid());
    QCOMPARE(t.referencedFields(),
             QStringList({s(u"Rating"), s(u"Artist"), s(u"Title"), s(u"Nope")}));
}

void TemplateTest::hostileInput()
{
    // An invalid regular expression is not a crash and not a match. T-REGEX
    QCOMPARE(Template::parse(s(uR"~($IsMatch(<Artist>,"("))~")).evaluate(track()), s(u"F"));
    QCOMPARE(Template::parse(s(uR"~($RxReplace(<Artist>,"(",x))~")).evaluate(track()), s(u"The Beatles"));

    // 200 nested calls parse and evaluate. T-ERRORS
    QString deep = s(u"<Artist>");
    for (int i = 0; i < 200; ++i)
        deep = s(u"$Upper(") + deep + s(u")");
    const Template t = Template::parse(deep);
    QVERIFY(t.isValid());
    QCOMPARE(t.evaluate(track()), s(u"THE BEATLES"));

    // 300 nested calls are refused, not crashed on: the limit is 256 and the
    // error points at the 257th '$'. Each "$Upper(" is 7 characters, so that
    // is index 256 * 7 = 1792. T-ERRORS, MATH
    QString tooDeep = s(u"x");
    for (int i = 0; i < 300; ++i)
        tooDeep = s(u"$Upper(") + tooDeep + s(u")");
    const Template refused = Template::parse(tooDeep);
    QVERIFY(!refused.isValid());
    QCOMPARE(refused.error().position, 1792);

    // A 1 MiB literal comes back unchanged.
    const QString big(1024 * 1024, QLatin1Char('x'));
    QCOMPARE(Template::parse(big).evaluate(track()), big);

    // Non-numeric counts do not crash. T-RANGE
    QCOMPARE(Template::parse(s(u"$Left(<Artist>,abc)")).evaluate(track()), QString());
    QCOMPARE(Template::parse(s(u"$Left(<Artist>,-1)")).evaluate(track()), QString());
}

QTEST_APPLESS_MAIN(TemplateTest)
#include "tst_template.moc"
