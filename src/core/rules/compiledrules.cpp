#include "rules/compiledrules.h"

#include "rules/compiledrules_p.h"
#include "rules/rules.h"

#include <atomic>

namespace linuxbee {

namespace {

// RC-PATTERNS: every expression of the rules, sub-rules included, built once
// into the table, keyed by its text. A text already held is not built again.
void holdPatterns(const QList<Rule> &list, HeldPatterns *held)
{
    for (const Rule &rule : list) {
        const bool heedCase
            = rule.op == RuleOperator::MatchesCase || rule.op == RuleOperator::DoesNotMatchCase;
        const bool isExpression
            = heedCase || rule.op == RuleOperator::Matches || rule.op == RuleOperator::DoesNotMatch;
        if (isExpression && !rule.values.isEmpty()) {
            const QString &text = rule.values.first();
            const QString key = rules::patternKey(text, heedCase);
            if (!held->contains(key)) {
                QRegularExpression pattern(text,
                    heedCase ? QRegularExpression::NoPatternOption
                             : QRegularExpression::CaseInsensitiveOption);
                pattern.optimize(); // compile now, not on the first track
                held->insert(key, pattern);
            }
        }
        holdPatterns(rule.subRules, held);
    }
}

}

namespace {
std::atomic<qint64> checkedCount { 0 };
}

void countRuleSetChecked()
{
    checkedCount.fetch_add(1, std::memory_order_relaxed);
}

// RC-COUNT.
qint64 ruleSetsChecked()
{
    return checkedCount.load(std::memory_order_relaxed);
}

CompiledRules::CompiledRules() = default;

// RC-VALID: the set is checked once, here, and the answer is kept.
CompiledRules::CompiledRules(const RuleSet &rules)
{
    countRuleSetChecked();
    const auto data = QSharedPointer<Data>::create();
    data->rules = rules;
    data->problems = validateRules(rules);
    if (data->problems.isEmpty())
        holdPatterns(rules.rules, &data->patterns);
    d = data;
}

bool CompiledRules::isValid() const
{
    return d && d->problems.isEmpty();
}

QStringList CompiledRules::problems() const
{
    return d ? d->problems : QStringList();
}

int CompiledRules::patternCount() const
{
    return d ? int(d->patterns.size()) : 0;
}

}
