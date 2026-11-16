#include "rules.h"
#include "values.h"

#include "rules/compiledrules_p.h"

#include <QRegularExpression>

#include <algorithm>

namespace linuxbee {

namespace {

bool isNegativeText(RuleOperator op)
{
    return op == RuleOperator::IsNot || op == RuleOperator::DoesNotContain
        || op == RuleOperator::IsNotAnyOf || op == RuleOperator::DoesNotMatch;
}

// The positive operator a negative one stands for: "is-not" is the negation of "is".
RuleOperator positiveOf(RuleOperator op)
{
    switch (op) {
    case RuleOperator::IsNot:
        return RuleOperator::Is;
    case RuleOperator::DoesNotContain:
        return RuleOperator::Contains;
    case RuleOperator::IsNotAnyOf:
        return RuleOperator::IsAnyOf;
    case RuleOperator::DoesNotMatch:
        return RuleOperator::Matches;
    default:
        return op;
    }
}

// U-TEXT: the same text ignoring case, or both read as numbers and equal.
bool sameText(const QString &value, const QString &wanted)
{
    if (QString::compare(value, wanted, Qt::CaseInsensitive) == 0)
        return true;
    const auto a = rules::readNumber(value);
    const auto b = rules::readNumber(wanted);
    return a && b && *a == *b;
}

// The expression's answer for one value. An expression held ready is used as it
// is; one that is not held is built now, ignoring case.
bool regexMatches(const QString &text, const QString &value, const HeldPatterns *held)
{
    if (held) {
        const auto found = held->constFind(text);
        if (found != held->constEnd())
            return found->match(value).hasMatch();
    }
    const QRegularExpression pattern(text, QRegularExpression::CaseInsensitiveOption);
    return pattern.isValid() && pattern.match(value).hasMatch();
}

// Whether one value passes the test of a positive text operator.
bool valuePasses(RuleOperator positive, const QString &value, const QStringList &wanted,
    const HeldPatterns *held)
{
    if (wanted.isEmpty())
        return false;
    const QString &first = wanted.first();
    switch (positive) {
    case RuleOperator::Is:
        return sameText(value, first);
    case RuleOperator::Contains:
        return value.contains(first, Qt::CaseInsensitive);
    case RuleOperator::StartsWith:
        return value.startsWith(first, Qt::CaseInsensitive);
    case RuleOperator::EndsWith:
        return value.endsWith(first, Qt::CaseInsensitive);
    case RuleOperator::IsAnyOf:
        return std::any_of(
            wanted.cbegin(), wanted.cend(), [&](const QString &w) { return sameText(value, w); });
    case RuleOperator::Matches:
        return regexMatches(first, value, held);
    default:
        return false;
    }
}

// U-EMPTY: a positive operator holds when one value passes; a negative one
// when none does. With no values, positive fails and negative holds.
bool textHolds(const Rule &rule, const QStringList &values, const HeldPatterns *held)
{
    const RuleOperator positive = positiveOf(rule.op);
    const bool anyPasses = std::any_of(values.cbegin(), values.cend(),
        [&](const QString &value) { return valuePasses(positive, value, rule.values, held); });
    return isNegativeText(rule.op) ? !anyPasses : anyPasses;
}

std::optional<double> firstNumber(const QStringList &values)
{
    return values.isEmpty() ? std::nullopt : rules::readNumber(values.first());
}

std::optional<QDateTime> firstDate(const QStringList &values)
{
    return values.isEmpty() ? std::nullopt : rules::readDate(values.first());
}

// U-LAST: the first value is at or after "now" minus the span, and not after now.
bool inTheLast(const Rule &rule, const QStringList &values, const RuleContext &context)
{
    const auto when = firstDate(values);
    if (!when || rule.values.size() != 2)
        return false;
    bool ok = false;
    const int amount = rule.values.at(0).trimmed().toInt(&ok);
    if (!ok || amount < 1)
        return false;

    const QDateTime now = context.now.toUTC();
    const QString &unit = rule.values.at(1);
    QDateTime start;
    if (unit == QLatin1String("days"))
        start = now.addDays(-amount);
    else if (unit == QLatin1String("weeks"))
        start = now.addDays(-7 * amount);
    else if (unit == QLatin1String("months"))
        start = now.addMonths(-amount);
    else if (unit == QLatin1String("years"))
        start = now.addYears(-amount);
    else
        return false;
    return *when >= start && *when <= now;
}

// One rule's own condition, against the track's values for its field.
bool conditionHolds(const Rule &rule, const QStringList &values, const RuleContext &context,
    const HeldPatterns *held)
{
    switch (rule.op) {
    case RuleOperator::Is:
    case RuleOperator::IsNot:
    case RuleOperator::Contains:
    case RuleOperator::DoesNotContain:
    case RuleOperator::StartsWith:
    case RuleOperator::EndsWith:
    case RuleOperator::IsAnyOf:
    case RuleOperator::IsNotAnyOf:
    case RuleOperator::Matches:
    case RuleOperator::DoesNotMatch:
        return textHolds(rule, values, held);
    case RuleOperator::HasValue:
        return !values.isEmpty();
    case RuleOperator::HasNoValue:
        return values.isEmpty();
    case RuleOperator::GreaterThan:
    case RuleOperator::LessThan: {
        const auto have = firstNumber(values);
        if (!have || rule.values.isEmpty())
            return false;
        const auto want = rules::readNumber(rule.values.first());
        if (!want)
            return false;
        return rule.op == RuleOperator::GreaterThan ? *have > *want : *have < *want;
    }
    case RuleOperator::InRange: {
        const auto have = firstNumber(values);
        if (!have || rule.values.size() != 2)
            return false;
        const auto low = rules::readNumber(rule.values.at(0));
        const auto high = rules::readNumber(rule.values.at(1));
        return low && high && *have >= *low && *have <= *high;
    }
    case RuleOperator::Before:
    case RuleOperator::After: {
        const auto have = firstDate(values);
        if (!have || rule.values.isEmpty())
            return false;
        const auto want = rules::readDate(rule.values.first());
        if (!want)
            return false;
        const QDate day = have->toUTC().date();
        const QDate other = want->toUTC().date();
        return rule.op == RuleOperator::Before ? day < other : day > other;
    }
    case RuleOperator::InTheLast:
        return inTheLast(rule, values, context);
    case RuleOperator::NotInTheLast:
        // U-LAST: a field with no date matches neither operator.
        return firstDate(values).has_value() && !inTheLast(rule, values, context);
    }
    return false;
}

// U-SUB: the rule's own condition and every sub-rule must hold.
bool ruleHolds(
    const Rule &rule, const TagSource &track, const RuleContext &context, const HeldPatterns *held)
{
    const QStringList values = rules::splitValues(track.value(rule.field).value_or(QString()));
    if (!conditionHolds(rule, values, context, held))
        return false;
    return std::all_of(rule.subRules.cbegin(), rule.subRules.cend(),
        [&](const Rule &sub) { return ruleHolds(sub, track, context, held); });
}

// U-MATCH without the validity check: the set against one track. The caller
// has checked the set. held supplies the expressions already built, or is null.
bool evaluate(const RuleSet &set, const TagSource &track, const RuleContext &context,
    const HeldPatterns *held)
{
    if (set.rules.isEmpty())
        return true;
    const auto holds = [&](const Rule &rule) { return ruleHolds(rule, track, context, held); };
    if (set.match == RuleSet::Match::Any)
        return std::any_of(set.rules.cbegin(), set.rules.cend(), holds);
    return std::all_of(set.rules.cbegin(), set.rules.cend(), holds);
}

}

// U-MATCH.
bool matchesRules(const RuleSet &rules, const TagSource &track, const RuleContext &context)
{
    countRuleSetChecked();
    if (!validateRules(rules).isEmpty())
        return false;
    return evaluate(rules, track, context, nullptr);
}

// RC-MATCH: the same evaluation as matchesRules, with the set checked when it
// was made and the expressions taken from the object.
bool CompiledRules::matches(const TagSource &track, const RuleContext &context) const
{
    if (!isValid())
        return false;
    return evaluate(d->rules, track, context, &d->patterns);
}

}
