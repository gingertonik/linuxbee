#include "rules.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace linuxbee {

namespace {

// Qt writes the escaping; the text is only the string itself, without the
// array brackets that carry it.
QString jsonString(const QString &text)
{
    const QString array = QString::fromUtf8(
        QJsonDocument(QJsonArray { QJsonValue(text) }).toJson(QJsonDocument::Compact))
                              .trimmed();
    return array.mid(1, array.size() - 2);
}

void writeRule(const Rule &rule, QString *out);

void writeRules(const QList<Rule> &rules, QString *out)
{
    for (int i = 0; i < rules.size(); ++i) {
        if (i > 0)
            out->append(QLatin1Char(','));
        writeRule(rules.at(i), out);
    }
}

void writeRule(const Rule &rule, QString *out)
{
    out->append(QStringLiteral("{\"field\":")).append(jsonString(rule.field));
    out->append(QStringLiteral(",\"op\":")).append(jsonString(ruleOperatorId(rule.op)));
    out->append(QStringLiteral(",\"values\":["));
    for (int i = 0; i < rule.values.size(); ++i) {
        if (i > 0)
            out->append(QLatin1Char(','));
        out->append(jsonString(rule.values.at(i)));
    }
    out->append(QLatin1Char(']'));
    if (!rule.subRules.isEmpty()) {
        out->append(QStringLiteral(",\"and\":["));
        writeRules(rule.subRules, out);
        out->append(QLatin1Char(']'));
    }
    out->append(QLatin1Char('}'));
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool readRuleList(const QJsonValue &value, QList<Rule> *rules, QString *error, const QString &key);

// Reads one rule. Unknown keys are refused, so a typo is not silently ignored.
bool readRule(const QJsonValue &value, Rule *rule, QString *error)
{
    if (!value.isObject())
        return fail(error, QStringLiteral("a rule is not an object"));
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString &key = it.key();
        if (key != QLatin1String("field") && key != QLatin1String("op")
            && key != QLatin1String("values") && key != QLatin1String("and"))
            return fail(error, QStringLiteral("unknown key \"%1\" in a rule").arg(key));
    }

    Rule result;
    const QJsonValue field = object.value(QStringLiteral("field"));
    if (!field.isString())
        return fail(error, QStringLiteral("a rule has no field name, or it is not text"));
    result.field = field.toString();

    const QJsonValue op = object.value(QStringLiteral("op"));
    if (!op.isString() || !ruleOperatorFromId(op.toString(), &result.op))
        return fail(error, QStringLiteral("a rule has no known operator (op)"));

    const QJsonValue values = object.value(QStringLiteral("values"));
    if (!values.isUndefined()) {
        if (!values.isArray())
            return fail(error, QStringLiteral("values is not a list"));
        for (const QJsonValue &item : values.toArray()) {
            if (!item.isString())
                return fail(error, QStringLiteral("a value is not text"));
            result.values.append(item.toString());
        }
    }

    const QJsonValue subRules = object.value(QStringLiteral("and"));
    if (!subRules.isUndefined()
        && !readRuleList(subRules, &result.subRules, error, QStringLiteral("and")))
        return false;

    *rule = result;
    return true;
}

bool readRuleList(const QJsonValue &value, QList<Rule> *rules, QString *error, const QString &key)
{
    if (!value.isArray())
        return fail(error, QStringLiteral("%1 is not a list").arg(key));
    QList<Rule> result;
    for (const QJsonValue &item : value.toArray()) {
        Rule rule;
        if (!readRule(item, &rule, error))
            return false;
        result.append(rule);
    }
    *rules = result;
    return true;
}

}

QString rulesToJson(const RuleSet &rules)
{
    QString out = QStringLiteral("{\"match\":");
    out.append(jsonString(
        rules.match == RuleSet::Match::Any ? QStringLiteral("any") : QStringLiteral("all")));
    out.append(QStringLiteral(",\"rules\":["));
    writeRules(rules.rules, &out);
    out.append(QLatin1String("]}"));
    return out;
}

bool rulesFromJson(const QString &json, RuleSet *rules, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("not JSON: %1").arg(parseError.errorString()));
    if (!document.isObject())
        return fail(error, QStringLiteral("a rule set is a JSON object"));

    const QJsonObject object = document.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString &key = it.key();
        if (key != QLatin1String("match") && key != QLatin1String("rules"))
            return fail(error, QStringLiteral("unknown key \"%1\"").arg(key));
    }

    RuleSet result;
    const QJsonValue match = object.value(QStringLiteral("match"));
    if (!match.isUndefined()) {
        if (!match.isString())
            return fail(error, QStringLiteral("match must be \"all\" or \"any\""));
        if (match.toString() == QLatin1String("any"))
            result.match = RuleSet::Match::Any;
        else if (match.toString() == QLatin1String("all"))
            result.match = RuleSet::Match::All;
        else
            return fail(error, QStringLiteral("match must be \"all\" or \"any\""));
    }

    const QJsonValue ruleList = object.value(QStringLiteral("rules"));
    if (!ruleList.isUndefined()
        && !readRuleList(ruleList, &result.rules, error, QStringLiteral("rules")))
        return false;

    *rules = result;
    return true;
}

}
