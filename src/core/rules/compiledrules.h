#pragma once
#include <QSharedPointer>
#include <QStringList>

#include "rules/rules.h"

// A rule set made ready for many tracks: checked once, its regular
// expressions compiled once. Contract: docs/spec/compiled-rules.md (RC-x).
// It gives exactly the answers of matchesRules (docs/spec/rules.md).
namespace linuxbee {

class CompiledRules {
public:
    CompiledRules(); // an empty object: not valid, matches nothing
    explicit CompiledRules(const RuleSet &rules);

    bool isValid() const; // RC-VALID
    QStringList problems() const; // what validateRules says of the set
    int patternCount() const; // RC-PATTERNS: the regular expressions held ready

    // RC-MATCH: matchesRules(rules, track, context), without the work that
    // does not depend on the track.
    bool matches(const TagSource &track, const RuleContext &context) const;

private:
    struct Data;
    QSharedPointer<const Data> d; // copies share it; it never changes
};

// RC-COUNT: how often a rule set was checked since the program started: once for
// each CompiledRules made from a set, and once for each call of matchesRules.
// For tests of speed; nothing in the program decides anything by it.
qint64 ruleSetsChecked();

}
