#include "rules/compiledrules.h"

// Stub. Plan 06b replaces every body in this file.
namespace linuxbee {

struct CompiledRules::Data { };

CompiledRules::CompiledRules() = default;

CompiledRules::CompiledRules(const RuleSet &rules)
{
    Q_UNUSED(rules);
}

bool CompiledRules::isValid() const
{
    return false;
}

QStringList CompiledRules::problems() const
{
    return { };
}

int CompiledRules::patternCount() const
{
    return 0;
}

bool CompiledRules::matches(const TagSource &track, const RuleContext &context) const
{
    Q_UNUSED(track);
    Q_UNUSED(context);
    return false;
}

}
