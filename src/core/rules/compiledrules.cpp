#include "rules/compiledrules.h"

#include "rules/compiledrules_p.h"
#include "rules/rules.h"

namespace linuxbee {

CompiledRules::CompiledRules() = default;

// RC-VALID: the set is checked once, here, and the answer is kept.
CompiledRules::CompiledRules(const RuleSet &rules)
{
    const auto data = QSharedPointer<Data>::create();
    data->rules = rules;
    data->problems = validateRules(rules);
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
