#pragma once
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "template/template.h"

// Rules decide whether a track belongs: to an auto-playlist, to a filter, to
// an organiser exception. Contract: docs/spec/rules.md. Nothing here reads
// the disk or the clock.
namespace linuxbee {

enum class RuleOperator {
    Is,
    IsNot,
    Contains,
    DoesNotContain,
    StartsWith,
    EndsWith,
    IsAnyOf,
    IsNotAnyOf,
    Matches, // regular expression
    DoesNotMatch,
    HasValue,
    HasNoValue,
    GreaterThan,
    LessThan,
    InRange, // inclusive
    Before,
    After,
    InTheLast,
    NotInTheLast,
};

struct Rule {
    QString field; // a field name, as a template writes it without the brackets
    RuleOperator op = RuleOperator::Is;
    QStringList values; // what the operator compares with (U-ARITY)
    QList<Rule> subRules; // all must hold as well
};

struct RuleSet {
    enum class Match { All, Any };
    Match match = Match::All;
    QList<Rule> rules;
};

struct RuleContext {
    // "Now" for InTheLast and NotInTheLast. The caller supplies it, so a result
    // can be repeated.
    QDateTime now;
};

// U-NAMES: "is", "is-not", "contains", ... and back. An unknown id gives false.
QString ruleOperatorId(RuleOperator op);
bool ruleOperatorFromId(const QString &id, RuleOperator *op);

// U-VALID: one line per problem; empty when the set can be used.
QStringList validateRules(const RuleSet &rules);

// U-MATCH. An invalid set matches nothing.
bool matchesRules(const RuleSet &rules, const TagSource &track, const RuleContext &context);

// U-JSON. toJson gives compact JSON with keys in a fixed order. fromJson
// returns false and sets *error when the text is not a rule set.
QString rulesToJson(const RuleSet &rules);
bool rulesFromJson(const QString &json, RuleSet *rules, QString *error);

}
