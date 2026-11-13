// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/compiled-rules.md (RC-x). What a
// rule set matches is decided by docs/spec/rules.md and checked by
// tst_rules; here the compiled object is held against matchesRules itself.
#include <QElapsedTimer>
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

MapSource track(const QString &title, const QString &artist, const QString &genre, const QString &year,
    const QString &rating, const QString &added)
{
    MapSource t;
    t.m_values = { { s(u"Title"), title }, { s(u"Artist"), artist }, { s(u"Genre"), genre }, { s(u"Year"), year },
        { s(u"Rating"), rating }, { s(u"Date Added"), added }, { s(u"Last Played"), QString() } };
    return t;
}

QList<MapSource> tracks()
{
    return {
        track(s(u"Paranoid Android"), s(u"Radiohead"), s(u"Rock; Alternative"), s(u"1997"), s(u"4.5"), s(u"2026-11-01T10:00:00")),
        track(s(u"J\u00f3ga"), s(u"Bj\u00f6rk"), s(u"Electronic"), s(u"1997"), s(u"5"), s(u"2026-06-01T10:00:00")),
        track(s(u"So What"), s(u"Miles Davis"), s(u"Jazz"), s(u"1959"), QString(), s(u"2020-01-01T00:00:00")),
        track(s(u"radio song (live)"), s(u"R.E.M."), s(u"Rock"), s(u"1991"), s(u"3"), s(u"2026-11-12T23:00:00")),
        track(QString(), QString(), QString(), QString(), QString(), QString()),
    };
}

linuxbee::RuleContext now()
{
    linuxbee::RuleContext c;
    c.now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    return c;
}

Rule rule(const char16_t *field, RuleOperator op, const QStringList &values = { }, const QList<Rule> &sub = { })
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

// Rule sets of every kind: each operator, both ways of joining, sub-rules,
// sets that are not valid, the empty set.
QList<RuleSet> sets()
{
    using O = RuleOperator;
    const auto All = RuleSet::Match::All;
    const auto Any = RuleSet::Match::Any;
    return {
        set(All, { }),
        set(Any, { }),
        set(All, { rule(u"Artist", O::Is, { s(u"radiohead") }) }),
        set(All, { rule(u"Artist", O::IsNot, { s(u"Radiohead") }) }),
        set(All, { rule(u"Title", O::Contains, { s(u"so") }) }),
        set(All, { rule(u"Title", O::DoesNotContain, { s(u"so") }) }),
        set(All, { rule(u"Title", O::StartsWith, { s(u"para") }) }),
        set(All, { rule(u"Title", O::EndsWith, { s(u"(LIVE)") }) }),
        set(All, { rule(u"Genre", O::IsAnyOf, { s(u"jazz"), s(u"alternative") }) }),
        set(All, { rule(u"Genre", O::IsNotAnyOf, { s(u"jazz"), s(u"alternative") }) }),
        set(All, { rule(u"Title", O::Matches, { s(u"^(so|j)") }) }),
        set(All, { rule(u"Title", O::DoesNotMatch, { s(u"\\(live\\)$") }) }),
        set(All, { rule(u"Rating", O::HasValue) }),
        set(All, { rule(u"Last Played", O::HasNoValue) }),
        set(All, { rule(u"Rating", O::GreaterThan, { s(u"4") }) }),
        set(All, { rule(u"Year", O::LessThan, { s(u"1995") }) }),
        set(All, { rule(u"Year", O::InRange, { s(u"1990"), s(u"1997") }) }),
        set(All, { rule(u"Date Added", O::Before, { s(u"2026-01-01") }) }),
        set(All, { rule(u"Date Added", O::After, { s(u"2026-01-01") }) }),
        set(All, { rule(u"Date Added", O::InTheLast, { s(u"2"), s(u"weeks") }) }),
        set(All, { rule(u"Date Added", O::NotInTheLast, { s(u"2"), s(u"weeks") }) }),
        set(All, { rule(u"No Such Field", O::Is, { s(u"x") }) }),
        set(All, { rule(u"No Such Field", O::HasNoValue) }),
        // joined, and with sub-rules
        set(All, { rule(u"Year", O::Is, { s(u"1997") }), rule(u"Rating", O::GreaterThan, { s(u"4.5") }) }),
        set(Any, { rule(u"Year", O::Is, { s(u"1959") }), rule(u"Title", O::Matches, { s(u"android") }) }),
        set(Any, { rule(u"Genre", O::Is, { s(u"Rock") }, { rule(u"Year", O::GreaterThan, { s(u"1995") }) }),
                     rule(u"Artist", O::Matches, { s(u"^miles") }, { rule(u"Title", O::DoesNotMatch, { s(u"what") }) }) }),
        set(All, { rule(u"Title", O::Matches, { s(u"o") }), rule(u"Artist", O::Matches, { s(u"o") }),
                     rule(u"Genre", O::DoesNotMatch, { s(u"jazz") }) }),
        // not valid: a broken expression, a missing value, one too many, a bad unit, an empty field name
        set(All, { rule(u"Title", O::Matches, { s(u"(unclosed") }) }),
        set(Any, { rule(u"Artist", O::Is, { s(u"Radiohead") }), rule(u"Title", O::Matches, { s(u"[") }) }),
        set(All, { rule(u"Artist", O::Is) }),
        set(All, { rule(u"Rating", O::HasValue, { s(u"x") }) }),
        set(All, { rule(u"Date Added", O::InTheLast, { s(u"2"), s(u"fortnights") }) }),
        set(All, { rule(u"", O::Is, { s(u"x") }) }),
        set(All, { rule(u"Artist", O::Is, { s(u"Radiohead") }, { rule(u"Title", O::Matches, { s(u"*") }) }) }),
    };
}

// Six rules with a regular expression each, for the timing.
RuleSet heavy()
{
    using O = RuleOperator;
    return set(RuleSet::Match::All,
        { rule(u"Title", O::Matches, { s(u"^[a-z0-9 ()\\p{L}]+$") }), rule(u"Artist", O::DoesNotMatch, { s(u"^\\d{4}-\\d{2}-\\d{2}$") }),
            rule(u"Genre", O::DoesNotMatch, { s(u"(polka|yodel|sea shanty)\\b") }), rule(u"Title", O::DoesNotMatch, { s(u"\\b(remaster(ed)?|bonus)\\b") }),
            rule(u"Artist", O::DoesNotMatch, { s(u"^various( artists)?$") }), rule(u"Year", O::Matches, { s(u"^(19|20)\\d\\d") }) });
}

}

class CompiledRulesTest : public QObject {
    Q_OBJECT
private slots:
    void sameAnswers();
    void validity();
    void patterns();
    void copies();
    void compiledOnce();
    void hostile();
};

void CompiledRulesTest::sameAnswers()
{
    // RC-MATCH: every set against every track.
    const QList<RuleSet> all = sets();
    const QList<MapSource> every = tracks();
    int matched = 0, asked = 0;
    for (int i = 0; i < all.size(); ++i) {
        const CompiledRules compiled(all.at(i));
        for (int t = 0; t < every.size(); ++t) {
            const bool want = linuxbee::matchesRules(all.at(i), every.at(t), now());
            QVERIFY2(compiled.matches(every.at(t), now()) == want,
                qPrintable(QStringLiteral("set %1 (%2), track %3: matchesRules says %4")
                               .arg(i).arg(linuxbee::rulesToJson(all.at(i))).arg(t).arg(want ? "yes" : "no")));
            matched += want ? 1 : 0;
            ++asked;
        }
    }
    // The table is worth something only if it holds both answers in number.
    QCOMPARE(asked, int(all.size() * every.size()));
    QVERIFY(all.size() >= 30);
    QVERIFY2(matched >= 40 && matched <= asked - 60, qPrintable(QString::number(matched)));
    // The context is used at each call, not kept from the first.
    const CompiledRules recent(set(RuleSet::Match::All, { rule(u"Date Added", RuleOperator::InTheLast, { s(u"2"), s(u"weeks") }) }));
    linuxbee::RuleContext later = now();
    QVERIFY(recent.matches(every.at(0), later));
    later.now = later.now.addYears(1);
    QVERIFY(!recent.matches(every.at(0), later));
}

void CompiledRulesTest::validity()
{
    // RC-VALID
    for (const RuleSet &one : sets()) {
        const CompiledRules compiled(one);
        const QStringList want = linuxbee::validateRules(one);
        QCOMPARE(compiled.problems(), want);
        QCOMPARE(compiled.isValid(), want.isEmpty());
    }
    const CompiledRules none;
    QVERIFY(!none.isValid());
    QCOMPARE(none.problems(), QStringList());
    QCOMPARE(none.patternCount(), 0);
    QVERIFY(!none.matches(tracks().at(0), now()));
    // A valid set with no rules matches everything, the blank track too.
    const CompiledRules everything(set(RuleSet::Match::All, { }));
    QVERIFY(everything.isValid());
    QVERIFY(everything.matches(tracks().at(4), now()));
}

void CompiledRulesTest::patterns()
{
    // RC-PATTERNS
    using O = RuleOperator;
    QCOMPARE(CompiledRules(set(RuleSet::Match::All, { rule(u"Artist", O::Is, { s(u"x") }) })).patternCount(), 0);
    QCOMPARE(CompiledRules(set(RuleSet::Match::All, { rule(u"Title", O::Matches, { s(u"a") }) })).patternCount(), 1);
    QCOMPARE(CompiledRules(heavy()).patternCount(), 6);
    // Sub-rules count; the same text twice is held once; texts differing in case are two.
    QCOMPARE(CompiledRules(set(RuleSet::Match::Any,
                               { rule(u"Title", O::Matches, { s(u"a+") }, { rule(u"Artist", O::DoesNotMatch, { s(u"b+") }) }),
                                   rule(u"Genre", O::DoesNotMatch, { s(u"a+") }), rule(u"Artist", O::Matches, { s(u"A+") }) }))
                 .patternCount(),
        3);
    // A set that is not valid holds none.
    QCOMPARE(CompiledRules(set(RuleSet::Match::All, { rule(u"Title", O::Matches, { s(u"a") }), rule(u"Artist", O::Matches, { s(u"(") }) }))
                 .patternCount(),
        0);
    // A text that only looks like an expression, under another operator, is none.
    QCOMPARE(CompiledRules(set(RuleSet::Match::All, { rule(u"Title", O::Contains, { s(u"a+") }) })).patternCount(), 0);
}

void CompiledRulesTest::copies()
{
    // RC-COPY
    const MapSource t = tracks().at(0);
    CompiledRules copy;
    {
        const CompiledRules original(heavy());
        copy = original;
        QVERIFY(original.matches(t, now()));
    }
    QVERIFY(copy.isValid());
    QCOMPARE(copy.patternCount(), 6);
    QVERIFY(copy.matches(t, now()));
    CompiledRules other(copy);
    copy = CompiledRules();
    QVERIFY(!copy.isValid());
    QVERIFY(other.matches(t, now()));
    QList<CompiledRules> many(50, other);
    QVERIFY(many.at(49).matches(t, now()));
}

void CompiledRulesTest::compiledOnce()
{
    // RC-PATTERNS: one object asked about many tracks, against matchesRules
    // asked about each (which builds every expression anew). Both loops give
    // the same answers; the first must take under a fifth of the time. Three
    // tries, so that a busy machine does not fail it by one bad moment.
    const RuleSet rules = heavy();
    const QList<MapSource> every = tracks();
    const int rounds = 400; // times five tracks
    const linuxbee::RuleContext context = now();
    double best = 1e9;
    for (int attempt = 0; attempt < 3 && best > 0.2; ++attempt) {
        QElapsedTimer timer;
        int yesOnce = 0, yesEach = 0;
        timer.start();
        const CompiledRules once(rules);
        for (int i = 0; i < rounds; ++i)
            for (const MapSource &t : every)
                yesOnce += once.matches(t, context) ? 1 : 0;
        const qint64 kept = qMax<qint64>(timer.nsecsElapsed(), 1);
        timer.restart();
        for (int i = 0; i < rounds; ++i)
            for (const MapSource &t : every)
                yesEach += linuxbee::matchesRules(rules, t, context) ? 1 : 0;
        const qint64 remade = qMax<qint64>(timer.nsecsElapsed(), 1);
        QCOMPARE(yesOnce, yesEach);
        QCOMPARE(yesOnce, rounds * 4); // every track but the blank one
        best = qMin(best, double(kept) / double(remade));
    }
    qInfo("kept/remade = %.3f", best);
    QVERIFY2(best <= 0.2, qPrintable(QStringLiteral("kept/remade = %1; the expressions are being built again for each track").arg(best)));
}

void CompiledRulesTest::hostile()
{
    // RC-HOSTILE: bounded, and the same answer as matchesRules.
    MapSource t;
    t.m_values = { { s(u"Title"), QString(40, QLatin1Char('a')) + QLatin1Char('b') } };
    const RuleSet rules = set(RuleSet::Match::All, { rule(u"Title", RuleOperator::Matches, { s(u"(a+)+$") }) });
    QElapsedTimer timer;
    timer.start();
    const bool compiled = CompiledRules(rules).matches(t, now());
    const bool plain = linuxbee::matchesRules(rules, t, now());
    QVERIFY2(timer.elapsed() < 20000, "a hostile expression ran for more than twenty seconds");
    QCOMPARE(compiled, plain);
}

QTEST_APPLESS_MAIN(CompiledRulesTest)
#include "tst_compiledrules.moc"
