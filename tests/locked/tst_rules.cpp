// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/rules.md (U-x) worked by hand
// on the track below; the working is in the comment where it is not obvious.
// What the MusicBee wiki documents about rules (W-R: "Rule-Based Features",
// "Auto-Playlists") is only: match ANY or ALL, sub-rules that must also hold,
// and the operator names "is not", "contains", "is any of", "is not any of".
#include <QtTest>

#include "rules/rules.h"

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

MapSource track()
{
    MapSource t;
    t.m_values = {
        { s(u"Title"), s(u"Paranoid Android") },
        { s(u"Artist"), s(u"Radiohead") },
        { s(u"Genre"), s(u"Rock; Alternative") }, // two values (U-VALUES)
        { s(u"Year"), s(u"1997-05-26") },
        { s(u"Rating"), s(u"4.5") },
        { s(u"Track#"), s(u"02") },
        { s(u"Time"), s(u"6:27") }, // 387 seconds
        { s(u"Date Added"), s(u"2026-11-01T10:00:00") },
        { s(u"Last Played"), QString() }, // known, blank
    };
    return t;
}

linuxbee::RuleContext now()
{
    linuxbee::RuleContext c;
    c.now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    return c;
}

// One rule as JSON: {"field":F,"op":OP,"values":[...]}
QString one(const char *field, const char *op, const QStringList &values = { })
{
    QStringList quoted;
    for (const QString &v : values)
        quoted.append(QLatin1Char('"') + v + QLatin1Char('"'));
    return QStringLiteral("{\"field\":\"%1\",\"op\":\"%2\",\"values\":[%3]}")
        .arg(QLatin1String(field), QLatin1String(op), quoted.join(QLatin1Char(',')));
}

QString set(const QString &rulesJson, const char *match = "all")
{
    return QStringLiteral("{\"match\":\"%1\",\"rules\":[%2]}").arg(QLatin1String(match), rulesJson);
}

// -1: the JSON did not load. 0: no match. 1: match.
int verdict(const QString &json, const MapSource &source)
{
    RuleSet rules;
    QString error;
    if (!linuxbee::rulesFromJson(json, &rules, &error))
        return -1;
    return linuxbee::matchesRules(rules, source, now()) ? 1 : 0;
}

}

class RulesTest : public QObject {
    Q_OBJECT
private slots:
    void operatorNames();
    void single_data();
    void single();
    void combining();
    void validate_data();
    void validate();
    void invalidMatchesNothing();
    void json();
    void jsonErrors_data();
    void jsonErrors();
    void builtInCode();
};

void RulesTest::operatorNames()
{
    // U-NAMES: the ids, in the order of the enum.
    const QStringList ids = { s(u"is"), s(u"is-not"), s(u"contains"), s(u"does-not-contain"),
        s(u"starts-with"), s(u"ends-with"), s(u"is-any-of"), s(u"is-not-any-of"), s(u"matches"),
        s(u"does-not-match"), s(u"has-value"), s(u"has-no-value"), s(u"greater-than"), s(u"less-than"),
        s(u"in-range"), s(u"before"), s(u"after"), s(u"in-the-last"), s(u"not-in-the-last") };
    for (int i = 0; i < ids.size(); ++i) {
        QCOMPARE(linuxbee::ruleOperatorId(RuleOperator(i)), ids.at(i));
        RuleOperator back = RuleOperator::NotInTheLast;
        if (i == int(RuleOperator::NotInTheLast))
            back = RuleOperator::Is;
        QVERIFY(linuxbee::ruleOperatorFromId(ids.at(i), &back));
        QCOMPARE(int(back), i);
    }
    RuleOperator op = RuleOperator::Contains;
    QVERIFY(!linuxbee::ruleOperatorFromId(s(u"IS"), &op)); // ids are lower case
    QVERIFY(!linuxbee::ruleOperatorFromId(s(u"equals"), &op));
    QVERIFY(!linuxbee::ruleOperatorFromId(QString(), &op));
}

void RulesTest::single_data()
{
    QTest::addColumn<QString>("rule");
    QTest::addColumn<bool>("expected");
    auto row = [](const char *name, const QString &rule, bool expected) {
        QTest::newRow(name) << rule << expected;
    };
    // U-TEXT: case is ignored.
    row("is", one("Artist", "is", { s(u"Radiohead") }), true);
    row("is-case", one("Artist", "is", { s(u"radioHEAD") }), true);
    row("is-part", one("Artist", "is", { s(u"Radio") }), false);
    row("is-not", one("Artist", "is-not", { s(u"Muse") }), true);
    row("is-not-same", one("Artist", "is-not", { s(u"radiohead") }), false);
    row("contains", one("Title", "contains", { s(u"NOID an") }), true);
    row("contains-no", one("Title", "contains", { s(u"xyz") }), false);
    row("does-not-contain", one("Title", "does-not-contain", { s(u"xyz") }), true);
    row("does-not-contain-no", one("Title", "does-not-contain", { s(u"android") }), false);
    row("starts-with", one("Title", "starts-with", { s(u"para") }), true);
    row("starts-with-no", one("Title", "starts-with", { s(u"android") }), false);
    row("ends-with", one("Title", "ends-with", { s(u"DROID") }), true);
    row("ends-with-no", one("Title", "ends-with", { s(u"paranoid") }), false);
    // U-VALUES: a field with several values is tested value by value. A
    // positive operator needs one value to pass; a negative one needs none to.
    row("multi-is-first", one("Genre", "is", { s(u"Rock") }), true);
    row("multi-is-second", one("Genre", "is", { s(u"alternative") }), true);
    row("multi-is-whole", one("Genre", "is", { s(u"Rock; Alternative") }), false);
    row("multi-is-not", one("Genre", "is-not", { s(u"Rock") }), false);
    row("multi-is-not-other", one("Genre", "is-not", { s(u"Jazz") }), true);
    row("multi-starts", one("Genre", "starts-with", { s(u"alt") }), true);
    row("multi-not-contain", one("Genre", "does-not-contain", { s(u"tern") }), false);
    row("any-of", one("Genre", "is-any-of", { s(u"Jazz"), s(u"Alternative") }), true);
    row("any-of-no", one("Genre", "is-any-of", { s(u"Jazz"), s(u"Pop") }), false);
    row("not-any-of", one("Genre", "is-not-any-of", { s(u"Jazz"), s(u"Pop") }), true);
    row("not-any-of-no", one("Genre", "is-not-any-of", { s(u"Pop"), s(u"rock") }), false);
    // U-REGEX: searched anywhere in a value unless anchored; case ignored.
    row("matches", one("Title", "matches", { s(u"^para.*android$") }), true);
    row("matches-part", one("Title", "matches", { s(u"noid") }), true);
    row("matches-value", one("Genre", "matches", { s(u"^alt") }), true);
    row("matches-no", one("Title", "matches", { s(u"^android") }), false);
    row("does-not-match", one("Title", "does-not-match", { s(u"^android") }), true);
    row("does-not-match-no", one("Genre", "does-not-match", { s(u"^rock$") }), false);
    // U-EMPTY: a blank field and a field the track does not have are the same.
    row("has-value", one("Artist", "has-value"), true);
    row("has-value-blank", one("Last Played", "has-value"), false);
    row("has-value-unknown", one("Comment", "has-value"), false);
    row("has-no-value-blank", one("Last Played", "has-no-value"), true);
    row("has-no-value-unknown", one("Comment", "has-no-value"), true);
    row("has-no-value-no", one("Artist", "has-no-value"), false);
    row("is-on-unknown", one("Comment", "is", { s(u"x") }), false);
    row("is-not-on-unknown", one("Comment", "is-not", { s(u"x") }), true);
    // U-NUMBER: "02" is 2. A clock time is its seconds: 6:27 is 387.
    row("number-is", one("Track#", "is", { s(u"2") }), true);
    row("number-is-any", one("Track#", "is-any-of", { s(u"1"), s(u"2.0") }), true);
    row("number-is-not", one("Track#", "is-not", { s(u"2") }), false);
    row("greater", one("Rating", "greater-than", { s(u"4") }), true);
    row("greater-equal", one("Rating", "greater-than", { s(u"4.5") }), false);
    row("less", one("Rating", "less-than", { s(u"5") }), true);
    row("less-equal", one("Rating", "less-than", { s(u"4.5") }), false);
    row("range", one("Rating", "in-range", { s(u"4"), s(u"4.5") }), true); // inclusive
    row("range-low-edge", one("Rating", "in-range", { s(u"4.5"), s(u"5") }), true);
    row("range-no", one("Rating", "in-range", { s(u"1"), s(u"4") }), false);
    row("time-greater", one("Time", "greater-than", { s(u"5:00") }), true); // 387 > 300
    row("time-less", one("Time", "less-than", { s(u"6:27") }), false);
    row("time-seconds", one("Time", "greater-than", { s(u"386") }), true);
    row("time-range", one("Time", "in-range", { s(u"6:00"), s(u"0:07:00") }), true); // 360..420
    row("number-on-text", one("Artist", "greater-than", { s(u"0") }), false);
    row("number-on-text-less", one("Artist", "less-than", { s(u"0") }), false);
    row("number-on-blank", one("Last Played", "less-than", { s(u"10") }), false);
    // U-DATE: a year is its 1 January, a year-month its first day; days are compared.
    row("before-year", one("Year", "before", { s(u"2000") }), true);
    row("after-year", one("Year", "after", { s(u"1997") }), true); // 26 May is after 1 January
    row("before-same-day", one("Year", "before", { s(u"1997-05-26") }), false);
    row("after-same-day", one("Year", "after", { s(u"1997-05-26") }), false);
    row("after-month", one("Year", "after", { s(u"1997-05") }), true);
    row("before-month", one("Year", "before", { s(u"1997-06") }), true);
    row("date-on-text", one("Artist", "before", { s(u"2000") }), false);
    row("date-on-blank", one("Last Played", "after", { s(u"1900") }), false);
    // U-LAST: now is 2026-11-13 12:00 UTC; Date Added is 2026-11-01 10:00 UTC.
    row("last-14-days", one("Date Added", "in-the-last", { s(u"14"), s(u"days") }), true); // from 30 Oct 12:00
    row("last-12-days", one("Date Added", "in-the-last", { s(u"12"), s(u"days") }), false); // from 1 Nov 12:00
    row("last-2-weeks", one("Date Added", "in-the-last", { s(u"2"), s(u"weeks") }), true);
    row("last-1-month", one("Date Added", "in-the-last", { s(u"1"), s(u"months") }), true); // from 13 Oct
    row("last-1-year", one("Date Added", "in-the-last", { s(u"1"), s(u"years") }), true);
    row("not-last-14-days", one("Date Added", "not-in-the-last", { s(u"14"), s(u"days") }), false);
    row("not-last-12-days", one("Date Added", "not-in-the-last", { s(u"12"), s(u"days") }), true);
    // Never played: not "in the last" anything, so "not in the last" holds.
    row("last-on-blank", one("Last Played", "in-the-last", { s(u"30"), s(u"days") }), false);
    row("not-last-on-blank", one("Last Played", "not-in-the-last", { s(u"30"), s(u"days") }), true);
    row("last-on-old", one("Year", "in-the-last", { s(u"20"), s(u"years") }), false);
    row("last-on-older", one("Year", "in-the-last", { s(u"30"), s(u"years") }), true);
}

void RulesTest::single()
{
    QFETCH(QString, rule);
    QFETCH(bool, expected);
    QCOMPARE(verdict(set(rule), track()), expected ? 1 : 0);
    // One rule gives the same answer under "any".
    QCOMPARE(verdict(set(rule, "any"), track()), expected ? 1 : 0);
}

void RulesTest::combining()
{
    const QString yes = one("Artist", "is", { s(u"Radiohead") });
    const QString no = one("Artist", "is", { s(u"Muse") });
    const QString yes2 = one("Rating", "greater-than", { s(u"4") });
    // U-MATCH
    QCOMPARE(verdict(set(yes + QLatin1Char(',') + yes2, "all"), track()), 1);
    QCOMPARE(verdict(set(yes + QLatin1Char(',') + no, "all"), track()), 0);
    QCOMPARE(verdict(set(yes + QLatin1Char(',') + no, "any"), track()), 1);
    QCOMPARE(verdict(set(no + QLatin1Char(',') + no, "any"), track()), 0);
    // No rules: everything matches, under either word.
    QCOMPARE(verdict(set(QString(), "all"), track()), 1);
    QCOMPARE(verdict(set(QString(), "any"), track()), 1);
    QCOMPARE(verdict(s(u"{}"), track()), 1);
    MapSource nothing;
    QCOMPARE(verdict(set(QString()), nothing), 1);

    // U-SUB: a rule with sub-rules holds when it and every sub-rule hold.
    auto with = [](const QString &rule, const QString &subs) {
        return rule.chopped(1) + QStringLiteral(",\"and\":[%1]}").arg(subs);
    };
    QCOMPARE(verdict(set(with(yes, yes2)), track()), 1);
    QCOMPARE(verdict(set(with(yes, yes2 + QLatin1Char(',') + no)), track()), 0);
    QCOMPARE(verdict(set(with(no, yes2)), track()), 0);
    // The W-R example shape: (Genre is Rock AND Rating > 4) OR Artist is Muse.
    QCOMPARE(verdict(set(with(one("Genre", "is", { s(u"Rock") }), yes2) + QLatin1Char(',') + no, "any"), track()), 1);
    QCOMPARE(verdict(set(with(one("Genre", "is", { s(u"Jazz") }), yes2) + QLatin1Char(',') + no, "any"), track()), 0);
    // Sub-rules of sub-rules.
    QCOMPARE(verdict(set(with(yes, with(yes2, yes))), track()), 1);
    QCOMPARE(verdict(set(with(yes, with(yes2, no))), track()), 0);
}

void RulesTest::validate_data()
{
    QTest::addColumn<QString>("json");
    QTest::addColumn<QString>("problemStartsWith"); // empty: valid
    auto row = [](const char *name, const QString &json, const char16_t *problem) {
        QTest::newRow(name) << json << QString::fromUtf16(problem);
    };
    // U-ARITY
    row("ok", set(one("Artist", "is", { s(u"x") })), u"");
    row("empty-set", set(QString()), u"");
    row("no-field", set(one("", "is", { s(u"x") })), u"rule 1:");
    row("blank-field", set(one("  ", "is", { s(u"x") })), u"rule 1:");
    row("is-no-value", set(one("A", "is")), u"rule 1:");
    row("is-two-values", set(one("A", "is", { s(u"x"), s(u"y") })), u"rule 1:");
    row("is-empty-text", set(one("A", "is", { QString() })), u""); // an empty text is a value
    row("contains-none", set(one("A", "contains")), u"rule 1:");
    row("any-of-none", set(one("A", "is-any-of")), u"rule 1:");
    row("any-of-many", set(one("A", "is-any-of", { s(u"x"), s(u"y"), s(u"z") })), u"");
    row("has-value-extra", set(one("A", "has-value", { s(u"x") })), u"rule 1:");
    row("has-no-value-ok", set(one("A", "has-no-value")), u"");
    row("greater-text", set(one("A", "greater-than", { s(u"lots") })), u"rule 1:");
    row("greater-clock", set(one("A", "greater-than", { s(u"1:02:03") })), u"");
    row("less-two", set(one("A", "less-than", { s(u"1"), s(u"2") })), u"rule 1:");
    row("range-one", set(one("A", "in-range", { s(u"1") })), u"rule 1:");
    row("range-reversed", set(one("A", "in-range", { s(u"5"), s(u"1") })), u"rule 1:");
    row("range-equal", set(one("A", "in-range", { s(u"3"), s(u"3") })), u"");
    row("before-text", set(one("A", "before", { s(u"yesterday") })), u"rule 1:");
    row("before-bad-date", set(one("A", "before", { s(u"2026-13-40") })), u"rule 1:");
    row("after-datetime", set(one("A", "after", { s(u"2026-11-13T12:00:00") })), u"");
    row("last-no-unit", set(one("A", "in-the-last", { s(u"3") })), u"rule 1:");
    row("last-bad-unit", set(one("A", "in-the-last", { s(u"3"), s(u"fortnights") })), u"rule 1:");
    row("last-zero", set(one("A", "in-the-last", { s(u"0"), s(u"days") })), u"rule 1:");
    row("last-fraction", set(one("A", "not-in-the-last", { s(u"1.5"), s(u"days") })), u"rule 1:");
    row("regex-bad", set(one("A", "matches", { s(u"(unclosed") })), u"rule 1:");
    row("regex-ok", set(one("A", "does-not-match", { s(u"^a+$") })), u"");
    // The position is in the message: second rule; second sub-rule of the first rule.
    row("second", set(one("A", "is", { s(u"x") }) + QLatin1Char(',') + one("B", "is")), u"rule 2:");
    row("sub", set(one("A", "is", { s(u"x") }).chopped(1) + s(u",\"and\":[") + one("B", "is", { s(u"y") })
                + QLatin1Char(',') + one("C", "is") + s(u"]}")),
        u"rule 1.2:");
}

void RulesTest::validate()
{
    QFETCH(QString, json);
    QFETCH(QString, problemStartsWith);
    RuleSet rules;
    QString error;
    QVERIFY2(linuxbee::rulesFromJson(json, &rules, &error), qPrintable(error));
    const QStringList problems = linuxbee::validateRules(rules);
    if (problemStartsWith.isEmpty()) {
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('|'))));
    } else {
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems.first().startsWith(problemStartsWith), qPrintable(problems.first()));
    }
}

void RulesTest::invalidMatchesNothing()
{
    // U-MATCH: one bad rule spoils the set, under "any" too.
    const QString good = one("Artist", "is", { s(u"Radiohead") });
    const QString bad = one("Artist", "matches", { s(u"(unclosed") });
    QCOMPARE(verdict(set(good + QLatin1Char(',') + bad, "any"), track()), 0);
    QCOMPARE(verdict(set(bad, "all"), track()), 0);
    // U-SUB: nine levels of sub-rules is one too many.
    Rule deep;
    deep.field = s(u"Artist");
    deep.values = { s(u"Radiohead") };
    for (int i = 0; i < 7; ++i) {
        Rule outer = deep;
        outer.subRules = { deep };
        deep = outer;
    }
    RuleSet eight;
    eight.rules = { deep };
    QVERIFY(linuxbee::validateRules(eight).isEmpty());
    QVERIFY(linuxbee::matchesRules(eight, track(), now()));
    Rule nine = deep;
    nine.subRules = { deep };
    RuleSet tooDeep;
    tooDeep.rules = { nine };
    QVERIFY(!linuxbee::validateRules(tooDeep).isEmpty());
    QVERIFY(!linuxbee::matchesRules(tooDeep, track(), now()));
}

void RulesTest::json()
{
    // U-JSON: compact, keys in the order match, rules; field, op, values, and.
    RuleSet rules;
    rules.match = RuleSet::Match::Any;
    Rule a;
    a.field = s(u"Genre");
    a.op = RuleOperator::IsAnyOf;
    a.values = { s(u"Rock"), s(u"say \"hi\"") };
    Rule sub;
    sub.field = s(u"Rating");
    sub.op = RuleOperator::GreaterThan;
    sub.values = { s(u"3") };
    a.subRules = { sub };
    Rule b;
    b.field = s(u"Last Played");
    b.op = RuleOperator::HasNoValue;
    rules.rules = { a, b };
    const QString text = s(u"{\"match\":\"any\",\"rules\":[{\"field\":\"Genre\",\"op\":\"is-any-of\","
                           u"\"values\":[\"Rock\",\"say \\\"hi\\\"\"],\"and\":[{\"field\":\"Rating\","
                           u"\"op\":\"greater-than\",\"values\":[\"3\"]}]},{\"field\":\"Last Played\","
                           u"\"op\":\"has-no-value\",\"values\":[]}]}");
    QCOMPARE(linuxbee::rulesToJson(rules), text);
    QCOMPARE(linuxbee::rulesToJson(RuleSet()), s(u"{\"match\":\"all\",\"rules\":[]}"));

    RuleSet back;
    QString error;
    QVERIFY(linuxbee::rulesFromJson(text, &back, &error));
    QCOMPARE(linuxbee::rulesToJson(back), text);
    QVERIFY(back.match == RuleSet::Match::Any);
    QCOMPARE(back.rules.size(), 2);
    QCOMPARE(back.rules.at(0).subRules.size(), 1);
    QCOMPARE(back.rules.at(0).values.at(1), s(u"say \"hi\""));

    // Reading is forgiving about what may be left out, and about white space.
    QVERIFY(linuxbee::rulesFromJson(s(u" { \"rules\" : [ { \"field\":\"A\", \"op\":\"has-value\" } ] } "), &back, &error));
    QVERIFY(back.match == RuleSet::Match::All);
    QCOMPARE(back.rules.size(), 1);
    QVERIFY(back.rules.at(0).values.isEmpty());
    // Non-Latin text survives.
    Rule u;
    u.field = s(u"Title");
    u.values = { s(u"\u65e5\u672c\u8a9e \u00e9") };
    RuleSet unicode;
    unicode.rules = { u };
    QVERIFY(linuxbee::rulesFromJson(linuxbee::rulesToJson(unicode), &back, &error));
    QCOMPARE(back.rules.at(0).values.at(0), s(u"\u65e5\u672c\u8a9e \u00e9"));
}

void RulesTest::jsonErrors_data()
{
    QTest::addColumn<QString>("json");
    // U-JSON: each of these is refused.
    QTest::newRow("empty") << QString();
    QTest::newRow("not-json") << s(u"match all");
    QTest::newRow("array") << s(u"[]");
    QTest::newRow("bad-match") << s(u"{\"match\":\"some\",\"rules\":[]}");
    QTest::newRow("rules-not-list") << s(u"{\"rules\":{}}");
    QTest::newRow("rule-not-object") << s(u"{\"rules\":[\"x\"]}");
    QTest::newRow("no-field") << s(u"{\"rules\":[{\"op\":\"is\",\"values\":[\"x\"]}]}");
    QTest::newRow("field-not-text") << s(u"{\"rules\":[{\"field\":3,\"op\":\"is\",\"values\":[\"x\"]}]}");
    QTest::newRow("no-op") << s(u"{\"rules\":[{\"field\":\"A\",\"values\":[\"x\"]}]}");
    QTest::newRow("unknown-op") << s(u"{\"rules\":[{\"field\":\"A\",\"op\":\"equals\",\"values\":[\"x\"]}]}");
    QTest::newRow("values-not-list") << s(u"{\"rules\":[{\"field\":\"A\",\"op\":\"is\",\"values\":\"x\"}]}");
    QTest::newRow("value-not-text") << s(u"{\"rules\":[{\"field\":\"A\",\"op\":\"is\",\"values\":[3]}]}");
    QTest::newRow("and-not-list") << s(u"{\"rules\":[{\"field\":\"A\",\"op\":\"has-value\",\"and\":{}}]}");
    QTest::newRow("unknown-key") << s(u"{\"match\":\"all\",\"rules\":[],\"limit\":25}");
    QTest::newRow("unknown-rule-key") << s(u"{\"rules\":[{\"field\":\"A\",\"op\":\"has-value\",\"not\":true}]}");
}

void RulesTest::jsonErrors()
{
    QFETCH(QString, json);
    RuleSet rules;
    Rule keep;
    keep.field = s(u"untouched");
    rules.rules = { keep };
    QString error;
    QVERIFY(!linuxbee::rulesFromJson(json, &rules, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(rules.rules.size(), 1); // U-JSON: the target is left alone on failure
    QCOMPARE(rules.rules.at(0).field, s(u"untouched"));
    QVERIFY(!linuxbee::rulesFromJson(json, &rules, nullptr)); // no error wanted
}

void RulesTest::builtInCode()
{
    // Rules built in code behave like rules read from JSON.
    Rule r;
    r.field = s(u"Genre");
    r.op = RuleOperator::Is;
    r.values = { s(u"alternative") };
    RuleSet rules;
    rules.rules = { r };
    QVERIFY(linuxbee::matchesRules(rules, track(), now()));
    // U-VALUES: a value is trimmed before it is compared.
    MapSource padded = track();
    padded.m_values.insert(s(u"Genre"), s(u" Rock ;  Alternative "));
    QVERIFY(linuxbee::matchesRules(rules, padded, now()));
    // U-LAST: `now` in another time zone is the same moment.
    Rule last;
    last.field = s(u"Date Added");
    last.op = RuleOperator::InTheLast;
    last.values = { s(u"12"), s(u"days") };
    rules.rules = { last };
    linuxbee::RuleContext later;
    later.now = QDateTime(QDate(2026, 11, 13), QTime(14, 0), QTimeZone::fromSecondsAheadOfUtc(4 * 3600));
    // 14:00 at UTC+4 is 10:00 UTC; twelve days back is 1 Nov 10:00 UTC, the very moment.
    QVERIFY(linuxbee::matchesRules(rules, track(), later));
}

QTEST_APPLESS_MAIN(RulesTest)
#include "tst_rules.moc"
