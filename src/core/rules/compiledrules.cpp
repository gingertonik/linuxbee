#include "rules/compiledrules.h"

#include "rules/compiledrules_p.h"
#include "rules/rules.h"

namespace linuxbee {

namespace {

// RC-PATTERNS: every expression of the rules, sub-rules included, built once
// into the table, keyed by its text. A text already held is not built again.
void holdPatterns(const QList<Rule> &list, HeldPatterns *held)
{
    for (const Rule &rule : list) {
        const bool isExpression
            = rule.op == RuleOperator::Matches || rule.op == RuleOperator::DoesNotMatch;
        if (isExpression && !rule.values.isEmpty()) {
            const QString &text = rule.values.first();
            if (!held->contains(text)) {
                QRegularExpression pattern(text, QRegularExpression::CaseInsensitiveOption);
                pattern.optimize(); // compile now, not on the first track
                held->insert(text, pattern);
            }
        }
        holdPatterns(rule.subRules, held);
    }
}

}

CompiledRules::CompiledRules() = default;

// RC-VALID: the set is checked once, here, and the answer is kept.
CompiledRules::CompiledRules(const RuleSet &rules)
{
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
