#include "rules.h"

#include <iterator>

namespace linuxbee {

namespace {

// U-NAMES: the ids in enum order, so an id's index is its operator's value.
const char *const kOperatorIds[] = {
    "is",
    "is-not",
    "contains",
    "does-not-contain",
    "starts-with",
    "ends-with",
    "is-any-of",
    "is-not-any-of",
    "matches",
    "does-not-match",
    "has-value",
    "has-no-value",
    "greater-than",
    "less-than",
    "in-range",
    "before",
    "after",
    "in-the-last",
    "not-in-the-last",
    "matches-case",
    "does-not-match-case",
};

constexpr int kOperatorCount = int(std::size(kOperatorIds));

}

QString ruleOperatorId(RuleOperator op)
{
    const int index = int(op);
    if (index < 0 || index >= kOperatorCount)
        return { };
    return QString::fromLatin1(kOperatorIds[index]);
}

// U-NAMES: MusicBee's names for the regular expressions; the id with spaces otherwise.
QString ruleOperatorLabel(RuleOperator op)
{
    switch (op) {
    case RuleOperator::MatchesCase:
        return QStringLiteral("match RegEx");
    case RuleOperator::Matches:
        return QStringLiteral("match RegEx/i");
    case RuleOperator::DoesNotMatchCase:
        return QStringLiteral("does not match RegEx");
    case RuleOperator::DoesNotMatch:
        return QStringLiteral("does not match RegEx/i");
    default:
        return ruleOperatorId(op).replace(QLatin1Char('-'), QLatin1Char(' '));
    }
}

bool ruleOperatorFromId(const QString &id, RuleOperator *op)
{
    for (int i = 0; i < kOperatorCount; ++i) {
        if (id == QLatin1String(kOperatorIds[i])) {
            if (op)
                *op = RuleOperator(i);
            return true;
        }
    }
    return false;
}

}
