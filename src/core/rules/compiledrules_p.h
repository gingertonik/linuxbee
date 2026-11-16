#pragma once
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include "rules/compiledrules.h"

// Private to a compiled rule set: the data that compiledrules.cpp makes and
// match.cpp asks. Nothing outside those two files includes this header.
namespace linuxbee {

// The regular expressions held ready, keyed by their text (RC-PATTERNS).
using HeldPatterns = QHash<QString, QRegularExpression>;

// RC-COUNT: one more rule set checked; called by the constructor and by matchesRules.
void countRuleSetChecked();

struct CompiledRules::Data {
    RuleSet rules;
    QStringList problems; // what validateRules says of the set; empty when it can be used
    HeldPatterns patterns; // filled when the set is made; never changed after that
};

}
