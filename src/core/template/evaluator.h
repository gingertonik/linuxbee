#pragma once
#include <QString>

#include "node.h"
#include "template.h"

namespace linuxbee::tmpl {

struct EvalContext {
    const TagSource &tags;
    const TemplateOptions &options;
};

// Joins the value of every node in the sequence (T-LEX, T-BLANK, T-UNKNOWN).
// topLevel is true only for the template's own sequence, never for an argument.
QString evaluateSequence(const Sequence &sequence, const EvalContext &context, bool topLevel);

// T-COMPARE.
bool evaluateCondition(const Argument &argument, const EvalContext &context);

} // namespace linuxbee::tmpl
