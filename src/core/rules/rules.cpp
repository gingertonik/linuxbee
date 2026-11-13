#include "rules.h"

// Stub. Plan 06 replaces every body in this file.
namespace linuxbee {

QStringList validateRules(const RuleSet &rules)
{
    Q_UNUSED(rules);
    return { };
}

bool matchesRules(const RuleSet &rules, const TagSource &track, const RuleContext &context)
{
    Q_UNUSED(rules);
    Q_UNUSED(track);
    Q_UNUSED(context);
    return false;
}

}
