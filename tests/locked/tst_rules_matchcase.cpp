// LOCKED ACCEPTANCE TEST (rules: a regular expression that heeds case, beside the one that does
// not). Written by the reviewer, not the builder. The builder makes these pass by changing src/,
// never by editing this file. A test believed to be wrong is reported, with the reason, and left
// alone.
//
// Rules: docs/spec/rules.md (U-REGEX, U-NAMES, U-EMPTY, U-ARITY, U-VALID, U-JSON) and
// docs/spec/compiled-rules.md (RC-PATTERNS, RC-MATCH). MusicBee's rules offer a regular
// expression that heeds case and a "/i" form that does not (the wiki, "Filters"). linuxBee's
// `matches` has always ignored case and keeps doing so, so that a stored set keeps its meaning;
// `matches-case` and `does-not-match-case` are the form that heeds it.
#include <QtTest>

#include "rules/compiledrules.h"
#include "rules/rules.h"

using linuxbee::CompiledRules;
using linuxbee::Rule;
using linuxbee::RuleOperator;
using linuxbee::RuleSet;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

class MapSource : public linuxbee::TagSource {
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

MapSource titled(const QString &title)
{
    MapSource t;
    t.m_values = { { s(u"Title"), title }, { s(u"Genre"), s(u"rock; Pop") } };
    return t;
}

Rule rule(
    const char16_t *field, RuleOperator op, const QStringList &values, const QList<Rule> &sub = { })
{
    Rule r;
    r.field = s(field);
    r.op = op;
    r.values = values;
    r.subRules = sub;
    return r;
}

RuleSet set(RuleSet::Match match, const QList<Rule> &rules)
{
    RuleSet r;
    r.match = match;
    r.rules = rules;
    return r;
}

linuxbee::RuleContext now()
{
    linuxbee::RuleContext c;
    c.now = QDateTime(QDate(2026, 11, 20), QTime(12, 0), QTimeZone::UTC);
    return c;
}

// The answer of matchesRules, and that a compiled set gives the same one (RC-MATCH).
bool both(const RuleSet &rules, const MapSource &track, bool *same)
{
    const bool plain = linuxbee::matchesRules(rules, track, now());
    *same = CompiledRules(rules).matches(track, now()) == plain;
    return plain;
}

}

class RulesMatchCaseTest : public QObject {
    Q_OBJECT
private slots:
    void theIds();
    void theOldIdsKeepTheirPlaces();
    void heedsCase_data();
    void heedsCase();
    void bothFormsOfOneTextInOneSet();
    void heldOnceForEachForm();
    void invalidAsMatchesIs_data();
    void invalidAsMatchesIs();
    void storedAndReadBack();
    void theLabelsAPersonReads();
};

void RulesMatchCaseTest::theIds()
{
    // U-NAMES: the two new ids come after `not-in-the-last`, in this order.
    QCOMPARE(linuxbee::ruleOperatorId(RuleOperator::MatchesCase), s(u"matches-case"));
    QCOMPARE(linuxbee::ruleOperatorId(RuleOperator::DoesNotMatchCase), s(u"does-not-match-case"));
    QCOMPARE(int(RuleOperator::MatchesCase), int(RuleOperator::NotInTheLast) + 1);
    QCOMPARE(int(RuleOperator::DoesNotMatchCase), int(RuleOperator::NotInTheLast) + 2);
    RuleOperator op = RuleOperator::Is;
    QVERIFY(linuxbee::ruleOperatorFromId(s(u"matches-case"), &op));
    QVERIFY(op == RuleOperator::MatchesCase);
    QVERIFY(linuxbee::ruleOperatorFromId(s(u"does-not-match-case"), &op));
    QVERIFY(op == RuleOperator::DoesNotMatchCase);
    QVERIFY(!linuxbee::ruleOperatorFromId(s(u"Matches-Case"), &op));
    QVERIFY(!linuxbee::ruleOperatorFromId(s(u"matches-i"), &op));
    QCOMPARE(
        linuxbee::ruleOperatorId(RuleOperator(int(RuleOperator::DoesNotMatchCase) + 1)), QString());
}

void RulesMatchCaseTest::theOldIdsKeepTheirPlaces()
{
    // A stored set names its operators by id: `matches` is what it was, and ignores case.
    RuleOperator op = RuleOperator::Is;
    QVERIFY(linuxbee::ruleOperatorFromId(s(u"matches"), &op));
    QVERIFY(op == RuleOperator::Matches);
    QCOMPARE(int(RuleOperator::Matches), 8);
    bool same = false;
    QVERIFY(
        both(set(RuleSet::Match::All, { rule(u"Title", RuleOperator::Matches, { s(u"^beat") }) }),
            titled(s(u"Beatles")), &same));
    QVERIFY(same);
}

void RulesMatchCaseTest::heedsCase_data()
{
    QTest::addColumn<int>("op");
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("title");
    QTest::addColumn<bool>("expected");
    const int m = int(RuleOperator::MatchesCase);
    const int n = int(RuleOperator::DoesNotMatchCase);
    const int old = int(RuleOperator::Matches);
    QTest::newRow("the same case") << m << s(u"^Beat") << s(u"Beatles") << true;
    QTest::newRow("another case") << m << s(u"^Beat") << s(u"beatles") << false;
    QTest::newRow("another case, the old form") << old << s(u"^Beat") << s(u"beatles") << true;
    QTest::newRow("not anchored") << m << s(u"tle") << s(u"Beatles") << true;
    QTest::newRow("a class of capitals") << m << s(u"^[A-Z]") << s(u"abba") << false;
    QTest::newRow("a class of capitals, held") << m << s(u"^[A-Z]") << s(u"ABBA") << true;
    QTest::newRow("an accented capital") << m << s(u"^É") << s(u"été") << false;
    QTest::newRow("the pattern's own (?i)") << m << s(u"(?i)^beat") << s(u"BEATLES") << true;
    QTest::newRow("negated, another case") << n << s(u"^Beat") << s(u"beatles") << true;
    QTest::newRow("negated, the same case") << n << s(u"^Beat") << s(u"Beatles") << false;
    QTest::newRow("negated, no value") << n << s(u"^Beat") << QString() << true; // U-EMPTY
    QTest::newRow("no value") << m << s(u".*") << QString() << false;
}

void RulesMatchCaseTest::heedsCase()
{
    // U-REGEX
    QFETCH(int, op);
    QFETCH(QString, pattern);
    QFETCH(QString, title);
    QFETCH(bool, expected);
    bool same = false;
    QCOMPARE(both(set(RuleSet::Match::All, { rule(u"Title", RuleOperator(op), { pattern }) }),
                 titled(title), &same),
        expected);
    QVERIFY2(same, "a compiled set answers otherwise");
}

void RulesMatchCaseTest::bothFormsOfOneTextInOneSet()
{
    // RC-PATTERNS: one text under both forms is two expressions, each with its own case.
    const RuleSet all = set(RuleSet::Match::All,
        { rule(u"Title", RuleOperator::Matches, { s(u"^b") }),
            rule(u"Title", RuleOperator::MatchesCase, { s(u"^b") }) });
    const RuleSet any = set(RuleSet::Match::Any,
        { rule(u"Title", RuleOperator::MatchesCase, { s(u"^b") },
              { rule(u"Genre", RuleOperator::MatchesCase, { s(u"^Rock") }) }),
            rule(u"Title", RuleOperator::Matches, { s(u"^b") },
                { rule(u"Genre", RuleOperator::Matches, { s(u"^rock") }) }) });
    bool same = false;
    QVERIFY(both(all, titled(s(u"beat")), &same));
    QVERIFY(same);
    QVERIFY(!both(all, titled(s(u"Beat")), &same));
    QVERIFY(same);
    // In `any`, the first rule fails on the title (`B`, not `b`) and the second holds.
    QVERIFY(both(any, titled(s(u"Beat")), &same));
    QVERIFY(same);
    const CompiledRules compiled(any);
    QVERIFY(compiled.matches(titled(s(u"beat")), now()));
    QVERIFY(!compiled.matches(titled(s(u"Xbeat")), now()));
}

void RulesMatchCaseTest::heldOnceForEachForm()
{
    // RC-PATTERNS: one for each different text in each form.
    using O = RuleOperator;
    QCOMPARE(
        CompiledRules(set(RuleSet::Match::All, { rule(u"Title", O::MatchesCase, { s(u"a") }) }))
            .patternCount(),
        1);
    QCOMPARE(CompiledRules(set(RuleSet::Match::All,
                               { rule(u"Title", O::MatchesCase, { s(u"a") }),
                                   rule(u"Artist", O::DoesNotMatchCase, { s(u"a") }) }))
                 .patternCount(),
        1);
    QCOMPARE(CompiledRules(set(RuleSet::Match::All,
                               { rule(u"Title", O::MatchesCase, { s(u"a") }),
                                   rule(u"Artist", O::Matches, { s(u"a") }) }))
                 .patternCount(),
        2);
    QCOMPARE(CompiledRules(set(RuleSet::Match::All,
                               { rule(u"Title", O::DoesNotMatch, { s(u"a") }),
                                   rule(u"Artist", O::DoesNotMatchCase, { s(u"a") },
                                       { rule(u"Genre", O::MatchesCase, { s(u"A") }) }) }))
                 .patternCount(),
        3);
}

void RulesMatchCaseTest::invalidAsMatchesIs_data()
{
    QTest::addColumn<QStringList>("values");
    QTest::addColumn<QString>("problem");
    QTest::newRow("none") << QStringList() << s(u"rule 1: needs exactly one regular expression");
    QTest::newRow("two") << QStringList({ s(u"a"), s(u"b") })
                         << s(u"rule 1: needs exactly one regular expression");
    QTest::newRow("does not compile")
        << QStringList({ s(u"(") }) << s(u"rule 1: the regular expression does not compile");
}

void RulesMatchCaseTest::invalidAsMatchesIs()
{
    // U-ARITY and U-VALID: as for `matches`, word for word.
    QFETCH(QStringList, values);
    QFETCH(QString, problem);
    for (const RuleOperator op :
        { RuleOperator::Matches, RuleOperator::MatchesCase, RuleOperator::DoesNotMatchCase }) {
        const QStringList lines
            = linuxbee::validateRules(set(RuleSet::Match::All, { rule(u"Title", op, values) }));
        QCOMPARE(lines, QStringList({ problem }));
    }
    QVERIFY(linuxbee::validateRules(
        set(RuleSet::Match::All, { rule(u"Title", RuleOperator::MatchesCase, { s(u"^A") }) }))
            .isEmpty());
}

void RulesMatchCaseTest::storedAndReadBack()
{
    // U-JSON: written and read by id, as every operator is.
    const RuleSet one = set(RuleSet::Match::Any,
        { rule(u"Title", RuleOperator::MatchesCase, { s(u"^A") },
            { rule(u"Artist", RuleOperator::DoesNotMatchCase, { s(u"x") }) }) });
    const QString json = linuxbee::rulesToJson(one);
    QCOMPARE(json,
        s(u"{\"match\":\"any\",\"rules\":[{\"field\":\"Title\",\"op\":\"matches-case\",\"values\":["
          u"\"^A\"],\"and\":[{\"field\":"
          u"\"Artist\",\"op\":\"does-not-match-case\",\"values\":[\"x\"]}]}]}"));
    RuleSet back;
    QString error;
    QVERIFY2(linuxbee::rulesFromJson(json, &back, &error), qPrintable(error));
    QCOMPARE(linuxbee::rulesToJson(back), json);
    QVERIFY(back.rules.first().op == RuleOperator::MatchesCase);
    QVERIFY(back.rules.first().subRules.first().op == RuleOperator::DoesNotMatchCase);
    // A set stored before this version reads as it always did.
    QVERIFY(linuxbee::rulesFromJson(
        s(u"{\"rules\":[{\"field\":\"Title\",\"op\":\"matches\",\"values\":[\"^beat\"]}]}"), &back,
        &error));
    QVERIFY(back.rules.first().op == RuleOperator::Matches);
    QVERIFY(linuxbee::matchesRules(back, titled(s(u"BEATLES")), now()));
}

void RulesMatchCaseTest::theLabelsAPersonReads()
{
    // U-NAMES: what a person reads names the case-heeding form MusicBee's "match RegEx", and the
    // form that ignores case its "/i" one.
    QCOMPARE(linuxbee::ruleOperatorLabel(RuleOperator::MatchesCase), s(u"match RegEx"));
    QCOMPARE(linuxbee::ruleOperatorLabel(RuleOperator::Matches), s(u"match RegEx/i"));
    QCOMPARE(
        linuxbee::ruleOperatorLabel(RuleOperator::DoesNotMatchCase), s(u"does not match RegEx"));
    QCOMPARE(linuxbee::ruleOperatorLabel(RuleOperator::DoesNotMatch), s(u"does not match RegEx/i"));
    // Every other operator: its id with a space for each `-`.
    QCOMPARE(linuxbee::ruleOperatorLabel(RuleOperator::IsNot), s(u"is not"));
    QCOMPARE(linuxbee::ruleOperatorLabel(RuleOperator::NotInTheLast), s(u"not in the last"));
    QCOMPARE(linuxbee::ruleOperatorLabel(RuleOperator::Contains), s(u"contains"));
}

QTEST_GUILESS_MAIN(RulesMatchCaseTest)
#include "tst_rules_matchcase.moc"
