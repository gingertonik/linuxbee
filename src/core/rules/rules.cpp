#include "rules.h"

// Stub. Plan 06 replaces every body in this file.
namespace linuxbee {

QString ruleOperatorId(RuleOperator op)
{
    Q_UNUSED(op);
    return { };
}

bool ruleOperatorFromId(const QString &id, RuleOperator *op)
{
    Q_UNUSED(id);
    Q_UNUSED(op);
    return false;
}

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

QString rulesToJson(const RuleSet &rules)
{
    Q_UNUSED(rules);
    return { };
}

bool rulesFromJson(const QString &json, RuleSet *rules, QString *error)
{
    Q_UNUSED(json);
    Q_UNUSED(rules);
    Q_UNUSED(error);
    return false;
}

}
