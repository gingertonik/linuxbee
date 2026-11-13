#include "rules.h"
#include "values.h"

#include <QRegularExpression>

namespace linuxbee {

namespace {

// U-SUB: a top-level rule is at depth 1; depth 9 or more is refused.
constexpr int kMaxDepth = 8;

// U-ARITY: the problem with a rule's values, or an empty string when they fit.
QString arityProblem(const Rule &rule)
{
    const int count = rule.values.size();

    switch (rule.op) {
    case RuleOperator::HasValue:
    case RuleOperator::HasNoValue:
        return count == 0 ? QString() : QStringLiteral("takes no values");
    case RuleOperator::IsAnyOf:
    case RuleOperator::IsNotAnyOf:
        return count >= 1 ? QString() : QStringLiteral("needs at least one value");
    case RuleOperator::GreaterThan:
    case RuleOperator::LessThan:
        if (count != 1)
            return QStringLiteral("needs exactly one number");
        return rules::readNumber(rule.values.first()) ? QString()
                                                      : QStringLiteral("the value is not a number");
    case RuleOperator::InRange: {
        if (count != 2)
            return QStringLiteral("needs two numbers");
        const auto low = rules::readNumber(rule.values.at(0));
        const auto high = rules::readNumber(rule.values.at(1));
        if (!low || !high)
            return QStringLiteral("the values are not numbers");
        return *low <= *high ? QString() : QStringLiteral("the lower number comes first");
    }
    case RuleOperator::Before:
    case RuleOperator::After:
        if (count != 1)
            return QStringLiteral("needs exactly one date");
        return rules::readDate(rule.values.first()) ? QString()
                                                    : QStringLiteral("the value is not a date");
    case RuleOperator::InTheLast:
    case RuleOperator::NotInTheLast: {
        if (count != 2)
            return QStringLiteral("needs a whole number and a unit");
        bool ok = false;
        const int amount = rule.values.at(0).trimmed().toInt(&ok);
        if (!ok || amount < 1)
            return QStringLiteral("the amount is not a whole number of 1 or more");
        const QString unit = rule.values.at(1);
        if (unit != QLatin1String("days") && unit != QLatin1String("weeks")
            && unit != QLatin1String("months") && unit != QLatin1String("years"))
            return QStringLiteral("the unit is not days, weeks, months or years");
        return QString();
    }
    case RuleOperator::Matches:
    case RuleOperator::DoesNotMatch:
        if (count != 1)
            return QStringLiteral("needs exactly one regular expression");
        return QRegularExpression(rule.values.first()).isValid()
            ? QString()
            : QStringLiteral("the regular expression does not compile");
    default:
        // The rest take exactly one value; an empty text counts as a value.
        return count == 1 ? QString() : QStringLiteral("needs exactly one value");
    }
}

void validateList(const QList<Rule> &rules, const QString &prefix, int depth, QStringList *problems)
{
    for (int i = 0; i < rules.size(); ++i) {
        const Rule &rule = rules.at(i);
        const QString position = prefix.isEmpty() ? QString::number(i + 1)
                                                  : QStringLiteral("%1.%2").arg(prefix).arg(i + 1);

        QString problem;
        if (rule.field.trimmed().isEmpty())
            problem = QStringLiteral("the field name is blank");
        else if (const QString arity = arityProblem(rule); !arity.isEmpty())
            problem = arity;
        else if (depth > kMaxDepth)
            problem = QStringLiteral("nested more than %1 levels deep").arg(kMaxDepth);

        if (!problem.isEmpty())
            problems->append(QStringLiteral("rule %1: %2").arg(position, problem));

        validateList(rule.subRules, position, depth + 1, problems);
    }
}

}

QStringList validateRules(const RuleSet &rules)
{
    QStringList problems;
    validateList(rules.rules, QString(), 1, &problems);
    return problems;
}

}
